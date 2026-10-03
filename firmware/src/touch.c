/*
 * Mai Pico Touch Keys
 * WHowe <github.com/whowechina>
 * 
 */

#include "touch.h"

#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "bsp/board.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/time.h"

#include "board_defs.h"

#include "config.h"
#include "mpr121.h"

static uint16_t touch[MAI_TOUCH_SENSOR_COUNT];
static unsigned touch_counts[MAI_TOUCH_ZONE_COUNT];
static bool sensor_ok[MAI_TOUCH_SENSOR_COUNT];
static uint64_t touch_reading;
static uint64_t last_stat_reading;
static unsigned next_sensor;
static uint64_t next_bus_recovery;
static input_history_t raw_history, output_history;
const input_history_t *touch_raw_history(void) { return &raw_history; }
const input_history_t *touch_output_history(void) { return &output_history; }
#define STALE_TIMEOUT_US 20000
#define RECOVERY_INTERVAL_US 500000
static struct {
    mpr121_setup_t setup;
    touch_diagnostics_t diag;
    uint64_t last_good, retry_at, check_ecr_at;
    uint16_t released_once;
    bool configured, read_failed;
} sensors[MAI_TOUCH_SENSOR_COUNT];

bool touch_diagnostics(unsigned sensor, touch_diagnostics_t *out)
{
    if (sensor >= MAI_TOUCH_SENSOR_COUNT || !out) return false;
    *out = sensors[sensor].diag;
    return true;
}

static uint8_t touch_map[MAI_TOUCH_CHANNEL_COUNT];
static uint8_t touch_thresholds[MAI_TOUCH_CHANNEL_COUNT];
static uint8_t release_thresholds[MAI_TOUCH_CHANNEL_COUNT];

static inline uint8_t clamp_threshold(int value)
{
    if (value < 1) {
        return 1;
    }
    if (value > 255) {
        return 255;
    }
    return value;
}

static void touch_i2c_bus_recover()
{
    gpio_init(I2C_SDA);
    gpio_init(I2C_SCL);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    gpio_put(I2C_SDA, 0);
    gpio_put(I2C_SCL, 0);
    gpio_set_dir(I2C_SDA, GPIO_IN);
    gpio_set_dir(I2C_SCL, GPIO_IN);
    sleep_us(10);

    /* Clock a slave out of an interrupted byte without driving a held line high. */
    for (int pulse = 0; pulse < 9 && !gpio_get(I2C_SDA); pulse++) {
        gpio_set_dir(I2C_SCL, GPIO_OUT);
        sleep_us(5);
        gpio_set_dir(I2C_SCL, GPIO_IN);
        sleep_us(5);
    }

    /* Generate a STOP: SDA rises while SCL is released high. */
    gpio_set_dir(I2C_SDA, GPIO_OUT);
    sleep_us(5);
    gpio_set_dir(I2C_SCL, GPIO_IN);
    sleep_us(5);
    gpio_set_dir(I2C_SDA, GPIO_IN);
    sleep_us(10);
}

void touch_sensor_init()
{
    touch_update_config();
}

void touch_init()
{
    touch_i2c_bus_recover();
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    for (unsigned channel = 0; channel < MAI_TOUCH_CHANNEL_COUNT; channel++) {
        touch_map[channel] = config_touch_map_get(mai_cfg, channel);
    }
    touch_sensor_init();
}

void touch_reload_config()
{
    for (unsigned channel = 0; channel < MAI_TOUCH_CHANNEL_COUNT; channel++) {
        touch_map[channel] = config_touch_map_get(mai_cfg, channel);
    }
    touch_update_config();
}

const char *touch_key_name(unsigned key)
{
    static char name[3] = { 0 };
    if (key < 18) {
        name[0] = "ABC"[key / 8];
        name[1] = '1' + key % 8;
    } else if (key < MAI_TOUCH_ZONE_COUNT) {
        name[0] = "DE"[(key - 18) / 8];
        name[1] = '1' + (key - 18) % 8;
    } else {
        return "XX";
    }
    return name;
}

int touch_key_by_name(const char *name)
{
    if (strlen(name) != 2) {
        return -1;
    }
    if (strcasecmp(name, "XX") == 0) {
        return 255;
    }

    int zone = toupper(name[0]) - 'A';
    int id = name[1] - '1';
    if ((zone < 0) || (zone > 4) || (id < 0) || (id > 7)) {
        return -1;
    }
    if ((zone == 2) && (id > 1)) {
        return -1; // C1 and C2 only
    }
    const int offsets[] = { 0, 8, 16, 18, 26 };
    return offsets[zone] + id;
}

int touch_key_channel(unsigned key)
{
    for (int i = 0; i < MAI_TOUCH_CHANNEL_COUNT; i++) {
        if (touch_map[i] == key) {
            return i;
        }
    }
    return -1;
}

unsigned touch_key_from_channel(unsigned channel)
{
    if (channel < MAI_TOUCH_CHANNEL_COUNT) {
        return touch_map[channel];
    }
    return 0xff;
}

void touch_set_map(unsigned sensor, unsigned key)
{
    if (sensor < MAI_TOUCH_CHANNEL_COUNT &&
        (key < MAI_TOUCH_ZONE_COUNT || key == MAI_TOUCH_NOT_CONNECTED)) {
        touch_map[sensor] = key;
        config_touch_map_set(mai_cfg, sensor, key);
        touch_update_config();
        config_changed();
    }
}

static uint64_t mapped_state(bool raw)
{
    uint64_t map = 0;
    for (int m = 0; m < MAI_TOUCH_SENSOR_COUNT; m++) {
        for (int i = 0; i < MAI_TOUCH_CHANNELS_PER_SENSOR; i++) {
            uint16_t state = raw ? sensors[m].diag.raw_state : touch[m];
            if (state & (1 << i)) {
                uint8_t zone = touch_map[m * MAI_TOUCH_CHANNELS_PER_SENSOR + i];
                if (zone < MAI_TOUCH_ZONE_COUNT) {
                    map |= 1ULL << zone;
                }
            }
        }
    }
    return map;
}

static void remap_reading()
{
    touch_reading = mapped_state(false);
    input_history_update(&output_history, touch_reading);
}

static void touch_stat()
{
    uint64_t just_touched = touch_reading & ~last_stat_reading;
    last_stat_reading = touch_reading;

    for (int i = 0; i < MAI_TOUCH_ZONE_COUNT; i++) {
        if (just_touched & (1ULL << i)) {
            touch_counts[i]++;
        }
    }
}

static uint16_t mapped_channels(unsigned sensor)
{
    uint16_t mask = 0;
    for (unsigned i = 0; i < MAI_TOUCH_CHANNELS_PER_SENSOR; i++) {
        if (touch_map[sensor * MAI_TOUCH_CHANNELS_PER_SENSOR + i] < MAI_TOUCH_ZONE_COUNT)
            mask |= 1u << i;
    }
    return mask;
}

static void quarantine(unsigned sensor, uint16_t mask, touch_release_reason_t reason)
{
    uint16_t released = touch[sensor] & mask & mapped_channels(sensor);
    for (unsigned channel = 0; channel < MAI_TOUCH_CHANNELS_PER_SENSOR; channel++) {
        if (released & (1u << channel)) sensors[sensor].diag.forced_releases[reason]++;
    }
    touch[sensor] &= ~mask;
    sensors[sensor].diag.blocked_channels |= mask;
    sensors[sensor].released_once &= ~mask;
}

static void start_setup(unsigned sensor, bool recovery)
{
    unsigned offset = sensor * MAI_TOUCH_CHANNELS_PER_SENSOR;
    mpr121_setup_begin(&sensors[sensor].setup, touch_thresholds + offset,
                       release_thresholds + offset, mai_cfg->sense.filter,
                       mai_cfg->sense.debounce_touch, mai_cfg->sense.debounce_release);
    sensors[sensor].configured = false;
    sensors[sensor].read_failed = false;
    sensors[sensor].diag.configuring = true;
    sensors[sensor].diag.raw_valid = false;
    sensors[sensor].retry_at = time_us_64() + RECOVERY_INTERVAL_US;
    if (recovery) sensors[sensor].diag.recovery_attempts++;
    sensor_ok[sensor] = false;
    quarantine(sensor, 0x0fff, recovery ? TOUCH_RELEASE_RECOVERY : TOUCH_RELEASE_RECONFIGURE);
}

void touch_update()
{
    unsigned sensor = next_sensor;
    next_sensor = (next_sensor + 1) % MAI_TOUCH_SENSOR_COUNT;
    uint64_t now = time_us_64();

    if (sensors[sensor].diag.configuring) {
        int result = mpr121_setup_step(MPR121_BASE_ADDR + sensor, &sensors[sensor].setup);
        if (result != 0) {
            sensors[sensor].diag.configuring = false;
            sensors[sensor].configured = result > 0;
            if (result < 0) {
                sensors[sensor].diag.setup_failures++;
                sensors[sensor].retry_at = now + RECOVERY_INTERVAL_US;
            } else {
                sensors[sensor].last_good = now;
                sensors[sensor].check_ecr_at = now + 250000;
            }
        }
    } else if (sensors[sensor].configured) {
        uint16_t touched, oor;
        bool overcurrent;
        bool valid = mpr121_health(MPR121_BASE_ADDR + sensor, &touched, &oor, &overcurrent);
        sensors[sensor].diag.raw_valid = valid;
        if (valid) {
            sensors[sensor].diag.raw_state = touched;
            sensors[sensor].diag.raw_sample_us = now;
            input_history_update(&raw_history, mapped_state(true));
        }
        bool running = true;
        if (valid && now >= sensors[sensor].check_ecr_at) {
            uint8_t ecr;
            valid = mpr121_read_register(MPR121_BASE_ADDR + sensor, 0x5e, &ecr);
            running = valid && (ecr & 0x3f) == 12;
            sensors[sensor].check_ecr_at = now + 250000;
        }
        sensors[sensor].read_failed = !valid;
        if (!valid) {
            sensors[sensor].diag.i2c_failures++;
            sensor_ok[sensor] = false;
            /* No other core uses I2C. A held idle bus needs clocks, not a soft reset. */
            if (now >= next_bus_recovery && (!gpio_get(I2C_SDA) || !gpio_get(I2C_SCL))) {
                next_bus_recovery = now + RECOVERY_INTERVAL_US;
                i2c_deinit(I2C_PORT);
                touch_i2c_bus_recover();
                i2c_init(I2C_PORT, I2C_FREQ);
                gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
                gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
                sensors[sensor].diag.bus_recoveries++;
            }
        } else {
            sensors[sensor].diag.oor = oor;
            sensors[sensor].diag.overcurrent = overcurrent;
            uint16_t faults = overcurrent || !running ? 0x0fff : oor & 0x0fff;
            sensors[sensor].diag.fault_channels = faults;
            sensors[sensor].last_good = now;
            quarantine(sensor, faults, overcurrent ? TOUCH_RELEASE_OVERCURRENT :
                       !running ? TOUCH_RELEASE_STOPPED : TOUCH_RELEASE_OOR);
            /* Recovery requires two consecutive, valid released samples per channel. */
            uint16_t released = ~touched & ~faults & 0x0fff;
            sensors[sensor].diag.blocked_channels &= ~(released & sensors[sensor].released_once);
            sensors[sensor].released_once = released;
            touch[sensor] = touched & ~sensors[sensor].diag.blocked_channels & ~faults;
            sensor_ok[sensor] = (faults & mapped_channels(sensor)) == 0;
            if (overcurrent || !running) {
                sensors[sensor].configured = false;
            } else if ((faults & mapped_channels(sensor)) && !touch[sensor] &&
                       now >= sensors[sensor].retry_at) {
                /* Do not interrupt a healthy held electrode on the same chip. */
                start_setup(sensor, true);
            }
        }
        if (!valid) sensors[sensor].released_once = 0;
    } else if (now >= sensors[sensor].retry_at) {
        start_setup(sensor, true);
    }

    now = time_us_64();
    for (unsigned m = 0; m < MAI_TOUCH_SENSOR_COUNT; m++) {
        if (sensors[m].configured && sensors[m].read_failed &&
            now - sensors[m].last_good >= STALE_TIMEOUT_US) {
            quarantine(m, 0x0fff, TOUCH_RELEASE_STALE);
            sensors[m].configured = false;
            sensors[m].diag.fault_channels = 0x0fff;
            sensor_ok[m] = false;
        }
    }
    remap_reading();
    touch_stat();
}

bool touch_sensor_ok(unsigned i)
{
    if (i < MAI_TOUCH_SENSOR_COUNT) {
        return sensor_ok[i];
    }
    return false;
}

uint8_t touch_sensor_mask()
{
    uint8_t mask = 0;
    for (int i = 0; i < MAI_TOUCH_SENSOR_COUNT; i++) {
        if (sensor_ok[i]) {
            mask |= 1u << i;
        }
    }
    return mask;
}

bool touch_read_frame(uint16_t *filtered, uint16_t *baseline, int16_t *delta)
{
    if (filtered == NULL || baseline == NULL || delta == NULL) {
        return false;
    }

    bool all_ok = true;
    for (int m = 0; m < MAI_TOUCH_SENSOR_COUNT; m++) {
        int offset = m * MAI_TOUCH_CHANNELS_PER_SENSOR;
        bool frame_ok = mpr121_frame(MPR121_BASE_ADDR + m,
                                    filtered + offset, baseline + offset,
                                    MAI_TOUCH_CHANNELS_PER_SENSOR);
        if (!frame_ok) {
            sensors[m].diag.i2c_failures++;
            all_ok = false;
        }
        for (int i = 0; i < MAI_TOUCH_CHANNELS_PER_SENSOR; i++) {
            delta[offset + i] = (int16_t)baseline[offset + i] -
                                (int16_t)filtered[offset + i];
        }
    }
    return all_ok;
}

const uint16_t *touch_raw()
{
    static uint16_t readout[MAI_TOUCH_CHANNEL_COUNT] = {0};
    // Do not use readout as buffer directly, update readout with buffer when operation finishes
    uint16_t buf[MAI_TOUCH_CHANNEL_COUNT] = {0};
    uint16_t baseline[MAI_TOUCH_CHANNEL_COUNT] = {0};
    int16_t delta[MAI_TOUCH_CHANNEL_COUNT] = {0};
    touch_read_frame(buf, baseline, delta);
    memcpy(readout, buf, sizeof(readout));

    return readout;
}

const uint16_t *map_raw_to_zones(const uint16_t* raw)
{
    static uint16_t zones[MAI_TOUCH_ZONE_COUNT];
    memset(zones, 0, sizeof(zones));

    for (int i = 0; i < MAI_TOUCH_CHANNEL_COUNT; i++) {
        uint8_t zone = touch_map[i];
        if (zone < MAI_TOUCH_ZONE_COUNT) {
            zones[zone] = raw[i];
        }
    }
    
    return zones;
}

const int8_t *unmap_sense_to_raw(const int8_t* memsense)
{
    static int8_t outsense[MAI_TOUCH_CHANNEL_COUNT];
    memset(outsense, 0, sizeof(outsense));
    for (int i = 0; i < MAI_TOUCH_CHANNEL_COUNT; i++) {
        uint8_t zone = touch_map[i];
        if (zone < MAI_TOUCH_ZONE_COUNT) {
            outsense[i] = memsense[zone];
        }
    }
    return outsense;
}

bool touch_touched(unsigned key)
{
    if (key >= MAI_TOUCH_ZONE_COUNT) {
        return 0;
    }
    return touch_reading & (1ULL << key);
}

uint64_t touch_touchmap()
{
    return touch_reading;
}

uint8_t touch_press_threshold(unsigned key)
{
    int channel = touch_key_channel(key);
    return channel >= 0 ? touch_thresholds[channel] : 0;
}

uint8_t touch_release_threshold(unsigned key)
{
    int channel = touch_key_channel(key);
    return channel >= 0 ? release_thresholds[channel] : 0;
}

unsigned touch_count(unsigned key)
{
    if (key >= MAI_TOUCH_ZONE_COUNT) {
        return 0;
    }
    return touch_counts[key];
}

void touch_reset_stat()
{
    memset(touch_counts, 0, sizeof(touch_counts));
}

void touch_update_config()
{
    const int8_t* outsense = unmap_sense_to_raw((const int8_t*)mai_cfg->sense.zones);

    for (int channel = 0; channel < MAI_TOUCH_CHANNEL_COUNT; channel++) {
        uint8_t zone = touch_map[channel];
        if (zone >= MAI_TOUCH_ZONE_COUNT) {
            touch_thresholds[channel] = 255;
            release_thresholds[channel] = 254;
            continue;
        }
        int sensitivity = mai_cfg->sense.global + outsense[channel];
        uint8_t touch_value = clamp_threshold(
            (int)mai_cfg->ext.touch_thresholds[zone] - sensitivity);
        if (touch_value < 2) touch_value = 2;
        uint8_t release_value = clamp_threshold(
            (int)mai_cfg->ext.release_thresholds[zone] - sensitivity / 2);
        if (release_value >= touch_value) {
            release_value = touch_value > 1 ? touch_value - 1 : 1;
        }
        touch_thresholds[channel] = touch_value;
        release_thresholds[channel] = release_value;
    }

    touch_reading = last_stat_reading = 0;
    next_sensor = 0;
    for (unsigned m = 0; m < MAI_TOUCH_SENSOR_COUNT; m++) {
        sensors[m].diag.fault_channels = 0;
        sensors[m].diag.oor = 0;
        sensors[m].diag.overcurrent = false;
        start_setup(m, false);
    }
    input_history_update(&output_history, 0);
}

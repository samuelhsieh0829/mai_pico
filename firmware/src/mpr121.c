/*
 * MPR121 Captive Touch Sensor
 * WHowe <github.com/whowechina>
 *
 */

#include <stdint.h>
#include <string.h>
#include "hardware/i2c.h"
#include "pico/time.h"

#include "mpr121.h"
#include "board_defs.h"

#define IO_TIMEOUT_US 1000

#define TOUCH_THRESHOLD_BASE 22
#define RELEASE_THRESHOLD_BASE 15

/*
 * High-sensitivity profile with enough timing margin for >120 Hz operation:
 * FFI=34 first-stage samples, SFI=4 second-stage samples, ESI=1 ms.
 * With 12 electrodes and a fixed 1 us charge/discharge time, one scan takes
 * 34 * (1 us + 1 us) * 12 = 816 us. Status updates every 4 ms nominally.
 */
#define HIGH_RATE_FFI 3
#define HIGH_RATE_SFI 0
#define HIGH_RATE_ESI 0
#define HIGH_RATE_CDT 2 /* Encoding 2 is 1 us. */
#define AUTOCONFIG_USL 201
#define AUTOCONFIG_LSL 130
#define AUTOCONFIG_TARGET 190

#define MPR121_TOUCH_STATUS_REG 0x00
#define MPR121_OUT_OF_RANGE_STATUS_0_REG 0x02
#define MPR121_OUT_OF_RANGE_STATUS_1_REG 0x03
#define MPR121_ELECTRODE_FILTERED_DATA_REG 0x04
#define MPR121_BASELINE_VALUE_REG 0x1E

#define MPR121_MAX_HALF_DELTA_RISING_REG 0x2B
#define MPR121_NOISE_HALF_DELTA_RISING_REG 0x2C
#define MPR121_NOISE_COUNT_LIMIT_RISING_REG 0x2D
#define MPR121_FILTER_DELAY_COUNT_RISING_REG 0x2E
#define MPR121_MAX_HALF_DELTA_FALLING_REG 0x2F
#define MPR121_NOISE_HALF_DELTA_FALLING_REG 0x30
#define MPR121_NOISE_COUNT_LIMIT_FALLING_REG 0x31
#define MPR121_FILTER_DELAY_COUNT_FALLING_REG 0x32
#define MPR121_NOISE_HALF_DELTA_TOUCHED_REG 0x33
#define MPR121_NOISE_COUNT_LIMIT_TOUCHED_REG 0x34
#define MPR121_FILTER_DELAY_COUNT_TOUCHED_REG 0x35

#define MPR121_TOUCH_THRESHOLD_REG 0x41
#define MPR121_RELEASE_THRESHOLD_REG 0x42

#define MPR121_DEBOUNCE_REG 0x5B
#define MPR121_AFE_CONFIG_REG 0x5C
#define MPR121_FILTER_CONFIG_REG 0x5D
#define MPR121_ELECTRODE_CONFIG_REG 0x5E
#define MPR121_ELECTRODE_CURRENT_REG 0x5F
#define MPR121_ELECTRODE_CHARGE_TIME_REG 0x6C
#define MPR121_GPIO_CTRL_0_REG 0x73
#define MPR121_GPIO_CTRL_1_REG 0x74
#define MPR121_GPIO_DATA_REG 0x75
#define MPR121_GPIO_DIRECTION_REG 0x76
#define MPR121_GPIO_ENABLE_REG 0x77
#define MPR121_GPIO_DATA_SET_REG 0x78
#define MPR121_GPIO_DATA_CLEAR_REG 0x79
#define MPR121_GPIO_DATA_TOGGLE_REG 0x7A
#define MPR121_AUTOCONFIG_CONTROL_0_REG 0x7B
#define MPR121_AUTOCONFIG_CONTROL_1_REG 0x7C
#define MPR121_AUTOCONFIG_USL_REG 0x7D
#define MPR121_AUTOCONFIG_LSL_REG 0x7E
#define MPR121_AUTOCONFIG_TARGET_REG 0x7F
#define MPR121_SOFT_RESET_REG 0x80

static bool write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[] = {reg, val};
    return i2c_write_blocking_until(I2C_PORT, addr, buf, 2, false,
                                    time_us_64() + IO_TIMEOUT_US) == 2;
}

static void setup_reg(mpr121_setup_t *setup, uint8_t reg, uint8_t value)
{
    setup->registers[setup->count][0] = reg;
    setup->registers[setup->count++][1] = value;
}

void mpr121_setup_begin(mpr121_setup_t *setup, const uint8_t *touch,
                        const uint8_t *release, uint8_t filter,
                        uint8_t debounce_touch, uint8_t debounce_release)
{
    memset(setup, 0, sizeof(*setup));
    /* Configure in Stop Mode; start exactly once after all writes verify. */
    static const uint8_t baseline[] = {1, 1, 1, 1, 1, 1, 6, 12, 1, 8, 30};
    for (unsigned i = 0; i < sizeof(baseline); i++) setup_reg(setup, 0x2b + i, baseline[i]);
    for (unsigned i = 0; i < 12; i++) {
        setup_reg(setup, 0x41 + 2 * i, touch[i]);
        setup_reg(setup, 0x42 + 2 * i, release[i]);
    }
    setup_reg(setup, 0x5b, ((debounce_release & 7) << 4) | (debounce_touch & 7));
    setup_reg(setup, 0x5c, (filter & 0xc0) | 16);
    setup_reg(setup, 0x5d, (HIGH_RATE_CDT << 5) | ((filter & 0x30) >> 1) | (filter & 7));
    setup_reg(setup, 0x5e, 0x80);
    setup_reg(setup, 0x7b, (filter & 0xc0) | 0x0b);
    setup_reg(setup, 0x7c, 0);
    setup_reg(setup, 0x7d, AUTOCONFIG_USL);
    setup_reg(setup, 0x7e, AUTOCONFIG_LSL);
    setup_reg(setup, 0x7f, AUTOCONFIG_TARGET);
    setup_reg(setup, 0x5e, 0x8c);
}

int mpr121_setup_step(uint8_t addr, mpr121_setup_t *setup)
{
    if (setup->phase == 0) {
        if (!write_reg(addr, MPR121_SOFT_RESET_REG, 0x63)) return -1;
        setup->reset_until = time_us_64() + 2000;
        setup->phase = 1;
        return 0;
    }
    if (time_us_64() < setup->reset_until) return 0;
    if (setup->index == setup->count) return 1;
    uint8_t reg = setup->registers[setup->index][0];
    uint8_t value = setup->registers[setup->index][1];
    if (setup->phase == 1) {
        if (!write_reg(addr, reg, value)) return -1;
        setup->phase = 2;
    } else {
        uint8_t readback;
        if (!mpr121_read_register(addr, reg, &readback) || readback != value) return -1;
        setup->index++;
        setup->phase = 1;
    }
    return setup->index == setup->count ? 1 : 0;
}

bool mpr121_init(uint8_t addr)
{
    uint8_t touch[12], release[12];
    memset(touch, TOUCH_THRESHOLD_BASE, sizeof(touch));
    memset(release, RELEASE_THRESHOLD_BASE, sizeof(release));
    mpr121_setup_t setup;
    mpr121_setup_begin(&setup, touch, release, HIGH_RATE_FFI << 6, 0, 0);
    int result;
    while ((result = mpr121_setup_step(addr, &setup)) == 0) sleep_ms(1);
    return result > 0;
}

#define ABS(x) ((x) < 0 ? -(x) : (x))

static bool mpr121_read_many(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    if (i2c_write_blocking_until(I2C_PORT, addr, &reg, 1, true,
                                 time_us_64() + IO_TIMEOUT_US) != 1) {
        return false;
    }
    int bytes = i2c_read_blocking_until(I2C_PORT, addr, buf, n, false,
                                        time_us_64() + (1000 + n * 100));
    return bytes >= 0 && (size_t)bytes == n;
}

bool mpr121_read_register(uint8_t addr, uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return false;
    }
    *value = 0;
    return mpr121_read_many(addr, reg, value, 1);
}

static bool mpr121_read_many16(uint8_t addr, uint8_t reg, uint16_t *buf, size_t n)
{
    if (n == 0) {
        return true;
    }
    uint8_t vals[n * 2];
    if (!mpr121_read_many(addr, reg, vals, n * 2)){
        return false;
    }

    for (size_t i = 0; i < n; i++) {
        buf[i] = (vals[i * 2 + 1] << 8) | vals[i * 2];
    }
    return true;
}

uint16_t mpr121_touched(uint8_t addr)
{
    uint16_t touched = 0;
    mpr121_touch_status(addr, &touched);
    return touched;
}

bool mpr121_touch_status(uint8_t addr, uint16_t *touched)
{
    if (touched == NULL) {
        return false;
    }
    *touched = 0;
    return mpr121_read_many16(addr, MPR121_TOUCH_STATUS_REG, touched, 1);
}

bool mpr121_raw(uint8_t addr, uint16_t *raw, int num)
{
    return mpr121_read_many16(addr, MPR121_ELECTRODE_FILTERED_DATA_REG, raw, num);
}

bool mpr121_frame(uint8_t addr, uint16_t *filtered, uint16_t *baseline,
                  int num)
{
    if (filtered == NULL || baseline == NULL || num < 0 || num > 12) {
        return false;
    }

    /*
     * Keep reads inside each register bank. Some compatible MPR121 parts and
     * marginal multi-board buses fail a 38-byte read spanning the proximity
     * registers, while these shorter reads remain reliable.
     */
    uint8_t filtered_frame[12 * 2];
    uint8_t baseline_frame[12];
    if (!mpr121_read_many(addr, MPR121_ELECTRODE_FILTERED_DATA_REG,
                          filtered_frame, sizeof(filtered_frame)) ||
        !mpr121_read_many(addr, MPR121_BASELINE_VALUE_REG,
                          baseline_frame, sizeof(baseline_frame))) {
        memset(filtered, 0, sizeof(*filtered) * num);
        memset(baseline, 0, sizeof(*baseline) * num);
        return false;
    }

    for (int i = 0; i < num; i++) {
        filtered[i] = filtered_frame[i * 2] |
                      (filtered_frame[i * 2 + 1] << 8);
        baseline[i] = ((uint16_t)baseline_frame[i]) << 2;
    }
    return true;
}

static bool write_checked(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t readback;
    return write_reg(addr, reg, value) && mpr121_read_register(addr, reg, &readback) && readback == value;
}

static bool mpr121_stop(uint8_t addr, uint8_t *ecr)
{
    return mpr121_read_register(addr, MPR121_ELECTRODE_CONFIG_REG, ecr) &&
           write_checked(addr, MPR121_ELECTRODE_CONFIG_REG, *ecr & 0xc0);
}

bool mpr121_filter(uint8_t addr, uint8_t ffi, uint8_t sfi, uint8_t esi)
{
    uint8_t ecr, afe, acc, fcr;
    if (!mpr121_stop(addr, &ecr)) return false;
    if (!mpr121_read_register(addr, 0x5c, &afe) ||
        !mpr121_read_register(addr, 0x7b, &acc) ||
        !mpr121_read_register(addr, 0x5d, &fcr) ||
        !write_checked(addr, 0x5c, (afe & 0x3f) | ((ffi & 3) << 6)) ||
        !write_checked(addr, 0x7b, (acc & 0x3f) | ((ffi & 3) << 6)) ||
        !write_checked(addr, 0x5d, (fcr & 0xe0) | ((sfi & 3) << 3) | (esi & 7))) return false;
    return write_checked(addr, 0x5e, ecr);
}

bool mpr121_sense(uint8_t addr, int8_t sense, int8_t *sense_keys, int num)
{
    if (!sense_keys || num < 0 || num > 12) return false;
    uint8_t touch[12], release[12];
    for (int i = 0; i < num; i++) {
        int delta = sense + sense_keys[i];
        int t = TOUCH_THRESHOLD_BASE - delta;
        int r = RELEASE_THRESHOLD_BASE - delta / 2;
        touch[i] = t < 2 ? 2 : t > 255 ? 255 : t;
        release[i] = r < 1 ? 1 : r >= touch[i] ? touch[i] - 1 : r;
    }
    return mpr121_thresholds(addr, touch, release, num);
}

bool mpr121_thresholds(uint8_t addr, const uint8_t *touch,
                       const uint8_t *release, int num)
{
    if (!touch || !release || num < 0 || num > 12) return false;
    /* Datasheet section 5.1 requires Stop Mode for configuration writes. */
    uint8_t ecr;
    if (!mpr121_stop(addr, &ecr)) return false;
    for (int i = 0; i < num; i++) {
        if (!write_checked(addr, 0x41 + i * 2, touch[i]) ||
            !write_checked(addr, 0x42 + i * 2, release[i])) return false;
    }
    return write_checked(addr, 0x5e, ecr);
}

bool mpr121_debounce(uint8_t addr, uint8_t touch, uint8_t release)
{
    uint8_t ecr;
    return mpr121_stop(addr, &ecr) &&
           write_checked(addr, 0x5b, ((release & 7) << 4) | (touch & 7)) &&
           write_checked(addr, 0x5e, ecr);
}

bool mpr121_health(uint8_t addr, uint16_t *touched, uint16_t *oor, bool *overcurrent)
{
    uint8_t status[4];
    if (!touched || !oor || !overcurrent) return false;
    *touched = *oor = 0;
    *overcurrent = false;
    if (!mpr121_read_many(addr, 0, status, sizeof(status))) return false;
    *touched = ((status[1] << 8) | status[0]) & 0x0fff;
    *oor = ((status[3] << 8) | status[2]); /* includes ACFF / ARFF for diagnostics */
    *overcurrent = (status[1] & 0x80) != 0;
    return true;
}

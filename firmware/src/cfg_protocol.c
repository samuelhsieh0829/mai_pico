#include "cfg_protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>

#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"

#include "button.h"
#include "cli.h"
#include "config.h"
#include "save.h"
#include "touch.h"
#include "hid.h"
#include "io.h"
#include "mpr121.h"

#define CFG_STREAM_INTERVAL_US 40000

static const char *button_names[MAI_BUTTON_COUNT] = {
    "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8",
    "Test", "Service", "Navigate", "Coin",
};

static struct {
    bool active;
    bool live_stream;
    bool touch_stream;
    bool gpio_stream;
    uint32_t sequence;
    uint64_t next_stream;
    mai_cfg_t staged;
} ctx;

static void ack(const char *action)
{
    printf("@CFG/1 ACK %s\n", action);
}

static void error(const char *code)
{
    printf("@CFG/1 ERR %s\n", code);
}

static const char *profile_error_code(config_profile_error_t value)
{
    switch (value) {
        case CONFIG_PROFILE_EXTENSION: return "PROFILE_EXTENSION";
        case CONFIG_PROFILE_SENSORS: return "PROFILE_SENSORS";
        case CONFIG_PROFILE_LEDS: return "PROFILE_LEDS";
        case CONFIG_PROFILE_TOUCH_MAP: return "PROFILE_TOUCH_MAP";
        case CONFIG_PROFILE_THRESHOLDS: return "PROFILE_THRESHOLDS";
        case CONFIG_PROFILE_BUTTONS: return "PROFILE_BUTTONS";
        case CONFIG_PROFILE_OK: return "PROFILE_OK";
        default: return "PROFILE_INVALID";
    }
}

static int parse_uint(const char *value)
{
    if (value == NULL || *value == '\0') {
        return -1;
    }
    errno = 0;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < 0 || parsed > INT_MAX) {
        return -1;
    }
    return (int)parsed;
}

static int button_id_by_name(const char *name)
{
    for (int i = 0; i < MAI_BUTTON_COUNT; i++) {
        if (strcasecmp(name, button_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static void abort_transaction(bool announce)
{
    bool was_gpio_streaming = ctx.gpio_stream;
    ctx.active = false;
    ctx.touch_stream = false;
    ctx.gpio_stream = false;
    if (was_gpio_streaming) {
        button_init();
    }
    if (announce) {
        ack("ABORT");
    }
}

bool cfg_is_active()
{
    return ctx.active;
}

static const mai_cfg_t *selected_config()
{
    return ctx.active ? &ctx.staged : mai_cfg;
}

static void print_info()
{
    printf("@CFG/1 INFO serial=%016" PRIx64 " sensors=%02x sensor_count=%d "
           "channels=%d zones=%d buttons=%d leds_per_button=%u "
           "touch_detection=mpr121_status firmware=20260905-inputfix2\n",
           board_id_64(), (unsigned)touch_sensor_mask(), MAI_TOUCH_SENSOR_COUNT,
           MAI_TOUCH_CHANNEL_COUNT, MAI_TOUCH_ZONE_COUNT, MAI_BUTTON_COUNT,
           (unsigned)selected_config()->rgb.per_button);
    printf("@CFG/1 GPIO_LIST");
    const uint8_t *gpios = button_scan_gpios();
    for (int i = 0; i < MAI_BUTTON_COUNT; i++) {
        printf("%c%u", i == 0 ? ' ' : ',', (unsigned)gpios[i]);
    }
    printf("\n");
}

static void dump_config()
{
    const mai_cfg_t *cfg = selected_config();
    print_info();
    for (int zone = 0; zone < MAI_TOUCH_ZONE_COUNT; zone++) {
        int physical = -1;
        for (int channel = 0; channel < MAI_TOUCH_CHANNEL_COUNT; channel++) {
            if (config_touch_map_get(cfg, channel) == zone) {
                physical = channel;
                break;
            }
        }
        int sensor = physical >= 0 ? physical / MAI_TOUCH_CHANNELS_PER_SENSOR : -1;
        int channel = physical >= 0 ? physical % MAI_TOUCH_CHANNELS_PER_SENSOR : -1;
        printf("@CFG/1 TOUCH zone=%s sensor=%d channel=%d touch=%u release=%u\n",
               touch_key_name(zone), sensor, channel,
               (unsigned)cfg->ext.touch_thresholds[zone],
               (unsigned)cfg->ext.release_thresholds[zone]);
    }
    for (int id = 0; id < MAI_BUTTON_COUNT; id++) {
        printf("@CFG/1 BUTTON id=%s gpio=%u active_high=%u\n",
               button_names[id], (unsigned)config_button_gpio(cfg, id),
               (unsigned)config_button_active_high(cfg, id));
    }
    printf("@CFG/1 LEDS per_button=%u\n", (unsigned)cfg->rgb.per_button);
    printf("@CFG/1 END\n");
}

static void print_mpr_diagnostics()
{
    /* Keep one coherent core-0 snapshot; printing must not change what each row describes.
       Static storage avoids consuming the Pico's small main stack with three histories. */
    static input_history_t raw_snapshot, output_snapshot;
    static io_touch_diagnostics_t tx_snapshot;
    static touch_diagnostics_t sensor_snapshot[MAI_TOUCH_SENSOR_COUNT];
    raw_snapshot = *touch_raw_history();
    output_snapshot = *touch_output_history();
    tx_snapshot = *io_touch_diagnostics();
    unsigned raw_valid_mask = 0;
    for (unsigned sensor = 0; sensor < MAI_TOUCH_SENSOR_COUNT; sensor++) {
        memset(&sensor_snapshot[sensor], 0, sizeof(sensor_snapshot[sensor]));
        touch_diagnostics(sensor, &sensor_snapshot[sensor]);
        if (sensor_snapshot[sensor].raw_valid) raw_valid_mask |= 1u << sensor;
    }
    printf("@CFG/1 TOUCH_LAYER layer=raw state=%09" PRIx64 " valid_sensors=%02x\n",
           raw_snapshot.state, raw_valid_mask);
    printf("@CFG/1 TOUCH_LAYER layer=firmware state=%09" PRIx64 "\n", output_snapshot.state);
    printf("@CFG/1 TOUCH_LAYER layer=usb state=%09" PRIx64
           " valid=%u interface=%d packets=%" PRIu32 " last_us=%" PRIu64
           " suspend_requests=%" PRIu32 " usb_resets=%" PRIu32
           " suspend_neutral=%" PRIu32 " reset_neutral=%" PRIu32 "\n",
           tx_snapshot.history.state, tx_snapshot.valid, tx_snapshot.last_interface,
           tx_snapshot.packets, tx_snapshot.last_packet_us, tx_snapshot.suspend_requests,
           tx_snapshot.usb_resets, tx_snapshot.suspend_neutral_packets, tx_snapshot.reset_neutral_packets);
    for (unsigned zone = 0; zone < MAI_TOUCH_ZONE_COUNT; zone++) {
        printf("@CFG/1 TOUCH_EDGE zone=%s raw=%" PRIu32 ":%" PRIu32
               " firmware=%" PRIu32 ":%" PRIu32 " usb=%" PRIu32 ":%" PRIu32 "\n",
               touch_key_name(zone), raw_snapshot.presses[zone], raw_snapshot.releases[zone],
               output_snapshot.presses[zone], output_snapshot.releases[zone],
               tx_snapshot.history.presses[zone], tx_snapshot.history.releases[zone]);
    }
    printf("@CFG/1 INPUT firmware=20260905-inputfix2 timer=%u scan_max_us=%" PRIu32
           " io4_overflows=%" PRIu32 " nkro_overflows=%" PRIu32 "\n",
           button_scanner_is_running(), button_scan_max_gap_us(), button_queue_overflows(0), button_queue_overflows(1));
    for (int sensor = 0; sensor < MAI_TOUCH_SENSOR_COUNT; sensor++) {
        touch_diagnostics_t health = sensor_snapshot[sensor];
        {
            printf("@CFG/1 HEALTH sensor=%d i2c_failures=%" PRIu32
                   " setup_failures=%" PRIu32 " recoveries=%" PRIu32 " bus_recoveries=%" PRIu32
                   " faults=%03x blocked=%03x oor=%04x overcurrent=%u configuring=%u"
                   " raw=%03x raw_valid=%u raw_us=%" PRIu64 "\n",
                   sensor, health.i2c_failures, health.setup_failures, health.recovery_attempts, health.bus_recoveries,
                   health.fault_channels, health.blocked_channels, health.oor,
                   health.overcurrent, health.configuring, health.raw_state, health.raw_valid, health.raw_sample_us);
            printf("@CFG/1 TOUCH_FORCED sensor=%d stale=%" PRIu32 " oor=%" PRIu32
                   " overcurrent=%" PRIu32 " stopped=%" PRIu32
                   " reconfigure=%" PRIu32 " recovery=%" PRIu32 "\n", sensor,
                   health.forced_releases[TOUCH_RELEASE_STALE], health.forced_releases[TOUCH_RELEASE_OOR],
                   health.forced_releases[TOUCH_RELEASE_OVERCURRENT], health.forced_releases[TOUCH_RELEASE_STOPPED],
                   health.forced_releases[TOUCH_RELEASE_RECONFIGURE], health.forced_releases[TOUCH_RELEASE_RECOVERY]);
        }
        uint8_t addr = MPR121_BASE_ADDR + sensor;
        uint8_t ecr = 0, oor0 = 0, oor1 = 0;
        uint8_t afe = 0, filter = 0, autoconfig = 0, autoconfig1 = 0;
        uint8_t target = 0, cdc0 = 0, cdt0 = 0;
        uint16_t valid = 0;
        valid |= mpr121_read_register(addr, 0x5e, &ecr) ? 0x01 : 0;
        valid |= mpr121_read_register(addr, 0x02, &oor0) ? 0x02 : 0;
        valid |= mpr121_read_register(addr, 0x03, &oor1) ? 0x04 : 0;
        valid |= mpr121_read_register(addr, 0x5c, &afe) ? 0x08 : 0;
        valid |= mpr121_read_register(addr, 0x5d, &filter) ? 0x10 : 0;
        valid |= mpr121_read_register(addr, 0x7b, &autoconfig) ? 0x20 : 0;
        valid |= mpr121_read_register(addr, 0x5f, &cdc0) ? 0x40 : 0;
        valid |= mpr121_read_register(addr, 0x7c, &autoconfig1) ? 0x080 : 0;
        valid |= mpr121_read_register(addr, 0x7f, &target) ? 0x100 : 0;
        valid |= mpr121_read_register(addr, 0x6c, &cdt0) ? 0x200 : 0;
        static const uint8_t sfi_samples[] = {4, 6, 10, 18};
        unsigned period_ms = sfi_samples[(filter >> 3) & 0x03] <<
                             (filter & 0x07);
        unsigned nominal_hz = period_ms ? 1000 / period_ms : 0;
        printf("@CFG/1 MPR sensor=%d addr=%02x valid=%03x ecr=%02x oor=%02x%02x "
               "afe=%02x filter=%02x auto=%02x auto1=%02x target=%02x "
               "cdc0=%02x cdt0=%02x nominal_hz=%u\n",
               sensor, addr, valid, ecr, oor1, oor0,
               afe, filter, autoconfig, autoconfig1, target,
               cdc0, cdt0, nominal_hz);
    }
    uint16_t filtered[MAI_TOUCH_CHANNEL_COUNT], baseline[MAI_TOUCH_CHANNEL_COUNT];
    int16_t delta[MAI_TOUCH_CHANNEL_COUNT];
    bool frame_ok = touch_read_frame(filtered, baseline, delta);
    printf("@CFG/1 FRAME valid=%u format=filtered:baseline:delta ", frame_ok);
    for (unsigned channel = 0; channel < MAI_TOUCH_CHANNEL_COUNT; channel++) {
        printf("%s%u:%u:%d", channel ? "," : "", filtered[channel], baseline[channel], delta[channel]);
    }
    printf("\n");

}

static void begin_transaction()
{
    hid_release_all();
    io_suspend();
    ctx.staged = *mai_cfg;
    ctx.active = true;
    ctx.live_stream = false;
    ctx.touch_stream = false;
    ctx.gpio_stream = false;
    ctx.sequence = 0;
    ctx.next_stream = 0;
    ack("BEGIN");
}

static void handle_live(int argc, char *argv[])
{
    if (argc != 1) {
        error("USAGE_LIVE");
        return;
    }

    bool enable;
    if (strcasecmp(argv[0], "on") == 0) {
        enable = true;
    } else if (strcasecmp(argv[0], "off") == 0) {
        enable = false;
    } else {
        error("INVALID_LIVE_STATE");
        return;
    }

    if (enable && ctx.active) {
        error("TRANSACTION_ACTIVE");
        return;
    }

    ctx.live_stream = enable;
    if (enable) {
        ctx.sequence = 0;
        ctx.next_stream = 0;
    }
    ack(enable ? "LIVE_ON" : "LIVE_OFF");
}

static void set_touch(int argc, char *argv[])
{
    if (argc != 5) {
        error("USAGE_SET_TOUCH");
        return;
    }
    int zone = touch_key_by_name(argv[0]);
    int sensor = parse_uint(argv[1]);
    int electrode = parse_uint(argv[2]);
    int touch_threshold = parse_uint(argv[3]);
    int release_threshold = parse_uint(argv[4]);
    if (zone < 0 || zone >= MAI_TOUCH_ZONE_COUNT || sensor < 0 ||
        sensor >= MAI_TOUCH_SENSOR_COUNT || electrode < 0 ||
        electrode >= MAI_TOUCH_CHANNELS_PER_SENSOR ||
        touch_threshold < 2 || touch_threshold > 255 ||
        release_threshold < 1 || release_threshold >= touch_threshold) {
        error("INVALID_TOUCH");
        return;
    }

    int physical = sensor * MAI_TOUCH_CHANNELS_PER_SENSOR + electrode;
    for (int i = 0; i < MAI_TOUCH_CHANNEL_COUNT; i++) {
        if (config_touch_map_get(&ctx.staged, i) == zone) {
            config_touch_map_set(&ctx.staged, i, MAI_TOUCH_NOT_CONNECTED);
        }
    }
    config_touch_map_set(&ctx.staged, physical, zone);
    ctx.staged.ext.touch_thresholds[zone] = touch_threshold;
    ctx.staged.ext.release_thresholds[zone] = release_threshold;
    ack("SET_TOUCH");
}

static void set_button(int argc, char *argv[])
{
    if (argc != 3) {
        error("USAGE_SET_BUTTON");
        return;
    }
    int id = button_id_by_name(argv[0]);
    int gpio = parse_uint(argv[1]);
    int active_high = parse_uint(argv[2]);
    if (id < 0 || gpio < 0 || !button_gpio_allowed(gpio) ||
        (active_high != 0 && active_high != 1)) {
        error("INVALID_BUTTON");
        return;
    }

    uint8_t old_gpio = config_button_gpio(&ctx.staged, id);
    for (int other = 0; other < MAI_BUTTON_COUNT; other++) {
        if (other != id && config_button_gpio(&ctx.staged, other) == gpio) {
            config_button_gpio_set(&ctx.staged, other, old_gpio);
            break;
        }
    }
    config_button_gpio_set(&ctx.staged, id, gpio);
    config_button_active_high_set(&ctx.staged, id, active_high != 0);
    ack("SET_BUTTON");
}

static void set_leds(int argc, char *argv[])
{
    if (argc != 1) {
        error("USAGE_SET_LEDS");
        return;
    }
    int count = parse_uint(argv[0]);
    if (count < 0 || count > 16) {
        error("INVALID_LEDS");
        return;
    }
    ctx.staged.rgb.per_button = count;
    ack("SET_LEDS");
}

static void handle_set(int argc, char *argv[])
{
    if (!ctx.active) {
        error("NO_TRANSACTION");
        return;
    }
    if (argc < 1) {
        error("USAGE_SET");
        return;
    }
    if (strcasecmp(argv[0], "touch") == 0) {
        set_touch(argc - 1, argv + 1);
    } else if (strcasecmp(argv[0], "button") == 0) {
        set_button(argc - 1, argv + 1);
    } else if (strcasecmp(argv[0], "leds") == 0) {
        set_leds(argc - 1, argv + 1);
    } else {
        error("UNKNOWN_SET");
    }
}

static void handle_stream(int argc, char *argv[])
{
    if (!ctx.active) {
        error("NO_TRANSACTION");
        return;
    }
    if (argc != 2) {
        error("USAGE_STREAM");
        return;
    }
    bool enable;
    if (strcasecmp(argv[1], "on") == 0) {
        enable = true;
    } else if (strcasecmp(argv[1], "off") == 0) {
        enable = false;
    } else {
        error("INVALID_STREAM_STATE");
        return;
    }

    if (strcasecmp(argv[0], "touch") == 0) {
        ctx.touch_stream = enable;
        ack(enable ? "STREAM_TOUCH_ON" : "STREAM_TOUCH_OFF");
    } else if (strcasecmp(argv[0], "gpio") == 0) {
        ctx.gpio_stream = enable;
        if (!enable) {
            button_init();
        }
        ack(enable ? "STREAM_GPIO_ON" : "STREAM_GPIO_OFF");
    } else {
        error("UNKNOWN_STREAM");
    }
}

static void commit_transaction()
{
    if (!ctx.active) {
        error("NO_TRANSACTION");
        return;
    }
    /* Sensor presence is runtime health, not profile validity. An offline
       MPR121 must not prevent a button GPIO/polarity-only commit. */
    config_profile_error_t profile_error =
        config_profile_validate(&ctx.staged, false, touch_sensor_mask());
    if (profile_error != CONFIG_PROFILE_OK) {
        error(profile_error_code(profile_error));
        return;
    }
    if (!config_replace(&ctx.staged)) {
        error("FLASH_WRITE");
        return;
    }
    ctx.active = false;
    ctx.touch_stream = false;
    ctx.gpio_stream = false;
    touch_reload_config();
    button_init();
    mai_runtime.key_stuck = button_is_stuck();
    ack("COMMIT");
}

void cfg_command(int argc, char *argv[])
{
    if (argc == 0) {
        error("USAGE");
        return;
    }
    if (strcasecmp(argv[0], "info") == 0) {
        print_info();
    } else if (strcasecmp(argv[0], "diag") == 0 && argc == 1) {
        print_mpr_diagnostics();
    } else if (strcasecmp(argv[0], "dump") == 0) {
        dump_config();
    } else if (strcasecmp(argv[0], "live") == 0) {
        handle_live(argc - 1, argv + 1);
    } else if (strcasecmp(argv[0], "begin") == 0 && argc == 1) {
        begin_transaction();
    } else if (strcasecmp(argv[0], "abort") == 0 && argc == 1) {
        abort_transaction(true);
    } else if (strcasecmp(argv[0], "commit") == 0 && argc == 1) {
        commit_transaction();
    } else if (strcasecmp(argv[0], "set") == 0) {
        handle_set(argc - 1, argv + 1);
    } else if (strcasecmp(argv[0], "stream") == 0) {
        handle_stream(argc - 1, argv + 1);
    } else {
        error("UNKNOWN_COMMAND");
    }
}

static void stream_touch()
{
    uint16_t filtered[MAI_TOUCH_CHANNEL_COUNT];
    uint16_t baseline[MAI_TOUCH_CHANNEL_COUNT];
    int16_t delta[MAI_TOUCH_CHANNEL_COUNT];
    touch_read_frame(filtered, baseline, delta);

    printf("@CFG/1 DATA TOUCH %" PRIu32 " %02x ", ctx.sequence,
           (unsigned)touch_sensor_mask());
    for (int i = 0; i < MAI_TOUCH_CHANNEL_COUNT; i++) {
        printf("%s%u:%d", i == 0 ? "" : ",",
               (unsigned)baseline[i], (int)delta[i]);
    }
    printf("\n");
}

static void stream_gpio()
{
    printf("@CFG/1 DATA GPIO %" PRIu32 " %03x\n", ctx.sequence,
           (unsigned)button_scan_levels());
}

static void stream_live()
{
    uint16_t filtered[MAI_TOUCH_CHANNEL_COUNT];
    uint16_t baseline[MAI_TOUCH_CHANNEL_COUNT];
    int16_t delta[MAI_TOUCH_CHANNEL_COUNT];
    touch_read_frame(filtered, baseline, delta);

    printf("@CFG/1 DATA LIVE %" PRIu32 " %02x %09" PRIx64 " %03x ",
           ctx.sequence, (unsigned)touch_sensor_mask(), touch_touchmap(),
           (unsigned)button_read());
    for (int zone = 0; zone < MAI_TOUCH_ZONE_COUNT; zone++) {
        int channel = touch_key_channel(zone);
        int value = channel >= 0 && channel < MAI_TOUCH_CHANNEL_COUNT
            ? delta[channel] : 0;
        printf("%s%d", zone == 0 ? "" : ",", value);
    }
    printf(" ");
    for (int zone = 0; zone < MAI_TOUCH_ZONE_COUNT; zone++) {
        printf("%s%u", zone == 0 ? "" : ",",
               (unsigned)touch_press_threshold(zone));
    }
    printf(" ");
    for (int zone = 0; zone < MAI_TOUCH_ZONE_COUNT; zone++) {
        printf("%s%u", zone == 0 ? "" : ",",
               (unsigned)touch_release_threshold(zone));
    }
    printf("\n");
}

void cfg_update()
{
    if (!ctx.active && !ctx.live_stream) {
        return;
    }
    if (!stdio_usb_connected()) {
        if (ctx.active) {
            abort_transaction(false);
        }
        ctx.live_stream = false;
        return;
    }

    uint64_t now = time_us_64();
    if (now < ctx.next_stream) {
        return;
    }
    ctx.next_stream = now + CFG_STREAM_INTERVAL_US;
    ctx.sequence++;

    if (ctx.live_stream && !ctx.active) {
        stream_live();
    } else if (ctx.touch_stream) {
        stream_touch();
    }
    if (ctx.gpio_stream) {
        stream_gpio();
    }
}

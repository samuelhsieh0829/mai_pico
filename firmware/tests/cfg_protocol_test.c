#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BOARD_MAI_PICO
#include "../src/config.c"
#include "../src/cfg_protocol.c"

static uint8_t save_storage[252];
static int fail_immediate_saves;
static bool usb_connected = true;
static uint64_t now_us;
static int touch_frame_calls;
static int button_init_calls;
static uint64_t touch_map_value = UINT64_C(0x200000001);
static uint16_t button_value = 0x805;
static const uint8_t fixed_gpios[MAI_BUTTON_COUNT] = BUTTON_DEF;

void *save_alloc(size_t size, void *def, void (*after_load)())
{
    assert(size <= sizeof(save_storage));
    memcpy(save_storage, def, size);
    (void)after_load;
    return save_storage;
}

bool save_request(bool immediately)
{
    if (immediately && fail_immediate_saves > 0) {
        fail_immediate_saves--;
        return false;
    }
    return true;
}

uint32_t board_id_32(void) { return 0x12345678; }
uint64_t board_id_64(void) { return UINT64_C(0x123456789abcdef0); }
uint64_t time_us_64(void) { return now_us; }
bool stdio_usb_connected(void) { return usb_connected; }

uint8_t button_default_gpio(int id)
{
    return id >= 0 && id < MAI_BUTTON_COUNT ? fixed_gpios[id] : 0xff;
}

bool button_gpio_allowed(uint8_t gpio)
{
    for (int i = 0; i < MAI_BUTTON_COUNT; i++) {
        if (fixed_gpios[i] == gpio) return true;
    }
    return false;
}

const uint8_t *button_scan_gpios(void) { return fixed_gpios; }
uint16_t button_scan_levels(void) { return 0x0fff; }
uint16_t button_read(void) { return button_value; }
void button_init(void) { button_init_calls++; }
bool button_is_stuck(void) { return false; }
bool button_scanner_is_running(void) { return true; }
uint32_t button_scan_max_gap_us(void) { return 1000; }
uint32_t button_queue_overflows(unsigned consumer) { (void)consumer; return 0; }
bool touch_diagnostics(unsigned sensor, touch_diagnostics_t *out)
{ (void)sensor; memset(out, 0, sizeof(*out)); return true; }
static input_history_t fake_history;
static io_touch_diagnostics_t fake_tx;
const input_history_t *touch_raw_history(void) { return &fake_history; }
const input_history_t *touch_output_history(void) { return &fake_history; }
const io_touch_diagnostics_t *io_touch_diagnostics(void) { return &fake_tx; }
void hid_release_all(void) {}
void io_suspend(void) {}

static uint8_t sensor_mask = 0x0f;
uint8_t touch_sensor_mask(void) { return sensor_mask; }
void touch_reload_config(void) {}
bool touch_read_frame(uint16_t *filtered, uint16_t *baseline, int16_t *delta)
{
    touch_frame_calls++;
    for (int i = 0; i < MAI_TOUCH_CHANNEL_COUNT; i++) {
        filtered[i] = 700;
        baseline[i] = 800;
        delta[i] = 100;
    }
    return true;
}
uint64_t touch_touchmap(void) { return touch_map_value; }
int touch_key_channel(unsigned key) { return key < MAI_TOUCH_ZONE_COUNT ? (int)key : -1; }
uint8_t touch_press_threshold(unsigned key) { return (uint8_t)(22 + key % 3); }
uint8_t touch_release_threshold(unsigned key) { return (uint8_t)(15 + key % 3); }

bool mpr121_read_register(uint8_t addr, uint8_t reg, uint8_t *value)
{
    *value = (uint8_t)(addr ^ reg);
    return true;
}

const char *touch_key_name(unsigned key)
{
    static char name[3];
    if (key < 16) {
        name[0] = key < 8 ? 'A' : 'B';
        name[1] = '1' + key % 8;
    } else if (key < 18) {
        name[0] = 'C';
        name[1] = '1' + key - 16;
    } else if (key < 34) {
        name[0] = key < 26 ? 'D' : 'E';
        name[1] = '1' + (key - 18) % 8;
    } else {
        return "XX";
    }
    name[2] = '\0';
    return name;
}

int touch_key_by_name(const char *name)
{
    for (int key = 0; key < MAI_TOUCH_ZONE_COUNT; key++) {
        if (strcasecmp(name, touch_key_name(key)) == 0) return key;
    }
    return -1;
}

static void command(int argc, char **argv)
{
    cfg_command(argc, argv);
}

int main(void)
{
    config_init();

    char *live_on[] = {"live", "on"};
    command(2, live_on);
    assert(!cfg_is_active());
    assert(ctx.live_stream);
    cfg_update();
    assert(ctx.sequence == 1);
    assert(touch_frame_calls == 1);

    char *begin[] = {"begin"};
    command(1, begin);
    assert(cfg_is_active());
    assert(!ctx.live_stream);

    char *set_touch[] = {"set", "touch", "A1", "3", "11", "30", "18"};
    command(7, set_touch);
    char *set_button[] = {"set", "button", "B1", "14", "1"};
    command(5, set_button);
    char *commit[] = {"commit"};
    command(1, commit);
    assert(!cfg_is_active());
    assert(config_touch_map_get(mai_cfg, 47) == A1);
    assert(mai_cfg->ext.touch_thresholds[A1] == 30);
    assert(mai_cfg->ext.release_thresholds[A1] == 18);
    assert(config_button_gpio(mai_cfg, 0) == 14);
    assert(config_button_active_high(mai_cfg, 0));
    assert(button_init_calls == 1);

    /* Runtime sensor outages must not reject button-only profile commits. */
    sensor_mask = 0x00;
    command(1, begin);
    char *set_button_low[] = {"set", "button", "B1", "14", "0"};
    command(5, set_button_low);
    command(1, commit);
    assert(!cfg_is_active());
    assert(!config_button_active_high(mai_cfg, 0));
    assert(button_init_calls == 2);

    command(1, begin);
    char *set_leds[] = {"set", "leds", "7"};
    command(3, set_leds);
    fail_immediate_saves = 1;
    command(1, commit);
    assert(cfg_is_active());
    assert(mai_cfg->rgb.per_button == 4);
    char *abort_cmd[] = {"abort"};
    command(1, abort_cmd);
    assert(!cfg_is_active());

    command(2, live_on);
    assert(ctx.live_stream);
    usb_connected = false;
    now_us += CFG_STREAM_INTERVAL_US;
    cfg_update();
    assert(!cfg_is_active());
    assert(!ctx.live_stream);
    assert(mai_cfg->rgb.per_button == 4);
    char *diag_cmd[] = {"diag"};
    command(1, diag_cmd);
    assert(touch_frame_calls == 2);
    puts("cfg_protocol_test: ok");
    return 0;
}

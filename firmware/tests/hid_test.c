#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BOARD_MAI_PICO
#include "../src/button.c"
#include "../src/hid.c"

mai_cfg_t config_storage;
mai_cfg_t *mai_cfg = &config_storage;
mai_runtime_t mai_runtime;
static uint64_t now_us;
static bool levels[30], mounted = true, active;
static bool ready_itf[2] = {true, true}, fail_send[2];
static bool (*timer_callback)(struct repeating_timer *);
static uint16_t report_buttons[1024], report_coins[1024];
static uint8_t report_keys[1024][15];
static unsigned reports[2];
uint64_t time_us_64(void) { return now_us; }
bool cfg_is_active(void) { return active; }
uint8_t config_button_gpio(const mai_cfg_t *cfg, unsigned i) { (void)cfg; return gpio_def[i]; }
bool config_button_active_high(const mai_cfg_t *cfg, unsigned i) { (void)cfg; (void)i; return false; }
void gpio_init(uint gpio) { (void)gpio; }
void gpio_set_function(uint gpio, int f) { (void)gpio; (void)f; }
void gpio_set_dir(uint gpio, bool out) { (void)gpio; (void)out; }
void gpio_pull_up(uint gpio) { levels[gpio] = true; }
void gpio_pull_down(uint gpio) { levels[gpio] = false; }
bool gpio_get(uint gpio) { return levels[gpio]; }
bool add_repeating_timer_us(int64_t delay, bool (*cb)(struct repeating_timer *), void *data, struct repeating_timer *timer)
{ assert(delay == -1000); (void)data; (void)timer; timer_callback = cb; return true; }
bool tud_ready(void) { return mounted; }
bool tud_hid_n_ready(uint8_t itf) { return mounted && ready_itf[itf]; }
bool tud_hid_n_report(uint8_t itf, uint8_t id, const void *data, uint16_t size)
{
    if (fail_send[itf]) return false;
    assert(tud_hid_n_ready(itf));
    unsigned n = reports[itf]++;
    assert(n < 1024);
    if (!itf) {
        assert(id == REPORT_ID_JOYSTICK && size == sizeof(hid_io4));
        memcpy(&report_buttons[n], (const uint8_t *)data + 28, 2);
        memcpy(&report_coins[n], (const uint8_t *)data + 24, 2);
    } else {
        assert(id == 0 && size == sizeof(hid_nkro));
        memcpy(report_keys[n], (const uint8_t *)data + 1, 15);
    }
    return true;
}
uint32_t rgb32(uint32_t r, uint32_t g, uint32_t b, bool gamma) { (void)gamma; return r << 16 | g << 8 | b; }
void rgb_set_banner(uint32_t color) { (void)color; }
static void scan(uint16_t pressed, uint32_t dt)
{
    now_us += dt;
    for (unsigned i = 0; i < MAI_BUTTON_COUNT; i++) levels[gpio_def[i]] = !(pressed & (1u << i));
    assert(timer_callback(&scan_timer));
}
static bool key_set(unsigned report, unsigned button)
{
    unsigned code = (uint8_t)keymap_p1[button];
    return (report_keys[report][code / 8] & (1u << (code % 8))) != 0;
}
static void drain(void)
{
    for (unsigned i = 0; i < 70; i++) hid_update();
    reports[0] = reports[1] = 0;
}
int main(void)
{
    mai_cfg->hid.io4 = mai_cfg->hid.nkro = 1;
    button_init();
    assert(button_start_scanning());
    scan(0, 1000);
    drain();

    /* A main-loop/USB stall must preserve complete short taps and coin edges. */
    ready_itf[0] = ready_itf[1] = false;
    const uint16_t sequence[] = {1, 0, 3, 0, 0x800, 0};
    for (unsigned i = 0; i < 6; i++) scan(sequence[i], 4000);
    hid_update();
    assert(!reports[0] && !reports[1]);
    ready_itf[0] = ready_itf[1] = true;
    for (unsigned i = 0; i < 6; i++) hid_update();
    assert(reports[0] == 6 && reports[1] == 6);
    for (unsigned i = 0; i < 6; i++) assert(report_buttons[i] == native_to_io4(sequence[i]));
    assert(key_set(0, 0) && !key_set(1, 0));
    assert(key_set(2, 0) && key_set(2, 1));
    assert(report_coins[4] == 0x100 && report_coins[5] == 0x100);
    drain();

    /* Clear-board excludes already sampled coins, including queued ones. */
    ready_itf[0] = false;
    scan(0x800, 4000); scan(0, 4000);
    const uint8_t clear_board[] = {REPORT_ID_OUTPUT, 3};
    hid_proc(clear_board, sizeof(clear_board));
    ready_itf[0] = true;
    hid_update(); hid_update();
    assert(report_coins[0] == 0 && report_coins[1] == 0);
    drain(); scan(0x800, 4000); scan(0, 4000); hid_update(); hid_update();
    assert(report_coins[0] == 0x100 && report_coins[1] == 0x100);
    drain();

    /* A busy IO4 endpoint must not gate keyboard traffic. Failed sends retain heads. */
    ready_itf[0] = false;
    scan(1, 4000); hid_update();
    assert(!reports[0] && reports[1] == 1);
    ready_itf[0] = true; fail_send[0] = true;
    button_event_t head, after;
    assert(button_event_peek(0, &head));
    hid_update();
    assert(button_event_peek(0, &after) && head.sequence == after.sequence);
    fail_send[0] = false; hid_update();
    assert(report_buttons[0] == native_to_io4(1));
    scan(0, 4000); drain();

    /* Existing 3 ms freeze suppresses bounce without dropping a later repeat. */
    scan(1, 4000); scan(0, 1000); scan(1, 1000);
    assert(button_read() == 1);
    scan(0, 1000); assert(button_read() == 0);
    scan(1, 3000); assert(button_read() == 1);
    drain();

    /* Overflow resync starts with release; stale acknowledgements cannot erase it. */
    scan(0, 4000);
    assert(button_event_peek(0, &head));
    for (unsigned i = 0; i < 70; i++) scan((i & 1) ? 0 : 1, 4000);
    assert(button_queue_overflows(0) > 0);
    assert(button_event_peek(0, &after) && after.buttons == 0);
    button_event_ack(0, head.sequence);
    assert(button_event_peek(0, &head) && head.sequence == after.sequence);
    drain();

    /* A release requested while endpoints are busy survives configuration pause. */
    scan(1, 4000); drain();
    ready_itf[0] = ready_itf[1] = false;
    hid_release_all(); active = true; hid_update();
    assert(release_pending[0] && release_pending[1]);
    ready_itf[0] = ready_itf[1] = true; hid_update();
    assert(!release_pending[0] && report_buttons[0] == native_to_io4(0));
    active = false; reports[0] = reports[1] = 0;
    hid_update(); hid_update();
    assert(report_buttons[0] == native_to_io4(0) && report_buttons[1] == native_to_io4(1));
    drain();

    /* Reconnect discards stale transitions and explicitly synchronises held state. */
    mounted = false; hid_update();
    scan(0, 4000); scan(2, 4000);
    mounted = true; hid_update(); hid_update();
    assert(report_buttons[0] == native_to_io4(0) && report_buttons[1] == native_to_io4(2));
    drain();
    scan(0, 4000); scan(4, 4000);
    /* The USB lifecycle callback catches a reset even without a polling gap. */
    hid_usb_reset(); hid_update(); hid_update();
    assert(report_buttons[0] == native_to_io4(0) && report_buttons[1] == native_to_io4(4));
    assert(button_scan_max_gap_us() >= 4000);
    puts("hid_test: ok");
    return 0;
}

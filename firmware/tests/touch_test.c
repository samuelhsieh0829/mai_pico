#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BOARD_MAI_PICO
#include "../src/mpr121.c"
#include "mpr121_bus_fake.h"
#include "../src/touch.c"

mai_cfg_t config_storage;
mai_cfg_t *mai_cfg = &config_storage;
static uint8_t mapping[MAI_TOUCH_CHANNEL_COUNT];
static bool bus_stuck;
static unsigned bus_deinits;
uint8_t config_touch_map_get(const mai_cfg_t *cfg, unsigned ch) { (void)cfg; return mapping[ch]; }
bool config_touch_map_set(mai_cfg_t *cfg, unsigned ch, uint8_t zone) { (void)cfg; mapping[ch] = zone; return true; }
void config_changed(void) {}
void gpio_init(uint gpio) { (void)gpio; }
void gpio_set_function(uint gpio, int f) { (void)gpio; (void)f; }
void gpio_set_dir(uint gpio, bool out) { (void)gpio; (void)out; }
void gpio_pull_up(uint gpio) { (void)gpio; }
bool gpio_get(uint gpio) { return !(bus_stuck && gpio == I2C_SDA); }
void gpio_put(uint gpio, bool value) { (void)gpio; (void)value; }
void i2c_deinit(i2c_inst_t *i2c) { (void)i2c; bus_deinits++; }
unsigned i2c_init(i2c_inst_t *i2c, unsigned baudrate) { (void)i2c; bus_stuck = false; return baudrate; }
void sleep_us(uint64_t us) { now_us += us; }
static void tick(unsigned ms)
{
    for (unsigned i = 0; i < ms; i++) { now_us += 1000; touch_update(); }
}
static void set_touch(unsigned sensor, uint16_t mask)
{
    registers[0x5a + sensor][0] = mask;
    registers[0x5a + sensor][1] = mask >> 8;
}
static void setup_defaults(void)
{
    memset(&config_storage, 0, sizeof(config_storage));
    mai_cfg->sense.filter = 0xc0;
    mai_cfg->sense.debounce_touch = 1;
    mai_cfg->sense.debounce_release = 2;
    memset(mai_cfg->ext.touch_thresholds, 22, MAI_TOUCH_ZONE_COUNT);
    memset(mai_cfg->ext.release_thresholds, 15, MAI_TOUCH_ZONE_COUNT);
    for (unsigned i = 0; i < MAI_TOUCH_CHANNEL_COUNT; i++) mapping[i] = i < MAI_TOUCH_ZONE_COUNT ? i : 255;
    touch_init();
    assert(touch_touchmap() == 0 && touch_sensor_mask() == 0);
    tick(500);
    assert(touch_sensor_mask() == 15);
    for (unsigned i = 0; i < 4; i++) {
        assert(registers[0x5a + i][0x5b] == 0x21);
        assert(!sensors[i].diag.configuring);
    }
}
int main(void)
{
    setup_defaults();
    set_touch(0, 1); set_touch(1, 1); tick(8);
    assert(touch_touched(0) && touch_touched(12));
    unsigned before_count = touch_count(0);
    assert(touch_raw_history()->presses[0] == 1 && touch_output_history()->presses[0] == 1);
    assert(touch_raw_history()->presses[12] == 1 && touch_output_history()->presses[12] == 1);

    /* A single failed read and alternating failures never manufacture new presses. */
    while (next_sensor != 0) tick(1);
    fail_addr = 0x5a; tick(1); fail_addr = -1;
    assert(touch_touched(0));
    tick(8); assert(touch_count(0) == before_count);
    unsigned polls = 0;
    for (unsigned i = 0; i < 100; i++) {
        fail_addr = next_sensor == 0 && (++polls & 1) ? 0x5a : -1;
        tick(1);
    }
    fail_addr = -1;
    assert(touch_touched(0) && touch_count(0) == before_count);

    /* A held SDA receives bounded bus recovery without clearing valid held inputs. */
    while (next_sensor != 0) tick(1);
    bus_stuck = true; fail_addr = 0x5a; tick(1); fail_addr = -1;
    assert(bus_deinits == 1 && sensors[0].diag.bus_recoveries == 1);
    assert(touch_touched(0)); tick(8);
    assert(touch_touched(0) && touch_count(0) == before_count);

    /* Stale data releases only the failed chip within the bounded grace period. */
    fail_addr = 0x5a; tick(24);
    assert(!touch_touched(0) && touch_touched(12));
    assert(sensors[0].diag.blocked_channels & 1);
    assert(!sensors[0].diag.raw_valid && sensors[0].diag.raw_state == 1);
    assert(touch_raw_history()->releases[0] == 0 && touch_output_history()->releases[0] == 1);
    assert(sensors[0].diag.forced_releases[TOUCH_RELEASE_STALE] == 1);
    fail_addr = -1;
    /* Simulate the finger remaining on the electrode across reset/calibration. */
    for (unsigned i = 0; i < 1100; i++) { set_touch(0, 1); tick(1); }
    assert(sensors[0].configured && !touch_touched(0));
    assert(touch_count(0) == before_count && touch_touched(12));
    set_touch(0, 0);
    while (next_sensor != 0) tick(1);
    tick(1); assert(sensors[0].diag.blocked_channels & 1);
    tick(4); assert(!(sensors[0].diag.blocked_channels & 1));
    set_touch(0, 1); tick(4); assert(touch_touched(0));
    assert(touch_count(0) == before_count + 1);

    /* Valid sustained holds are never timed out or periodically recalibrated. */
    unsigned recoveries = sensors[0].diag.recovery_attempts;
    tick(30000);
    assert(touch_touched(0) && sensors[0].diag.recovery_attempts == recoveries);

    /* A diagnostic/main-loop delay alone is not an I2C failure. */
    now_us += 100000;
    tick(4);
    assert(touch_touched(0) && touch_touched(12));
    assert(sensors[0].diag.recovery_attempts == recoveries);

    /* OOR quarantines one electrode, leaving a healthy held neighbour untouched. */
    set_touch(0, 3); tick(4);
    registers[0x5a][2] = 1; registers[0x5a][3] = 0x80; tick(8);
    assert(!touch_touched(0) && touch_touched(1));
    assert(sensors[0].configured && sensors[0].diag.oor == 0x8001);
    assert(sensors[0].diag.forced_releases[TOUCH_RELEASE_OOR] == 1);
    tick(600); assert(sensors[0].diag.recovery_attempts == recoveries);
    registers[0x5a][2] = registers[0x5a][3] = 0;
    tick(8); assert(!touch_touched(0));
    set_touch(0, 2); tick(8); set_touch(0, 3); tick(4);
    assert(touch_touched(0) && touch_touched(1));

    /* Diagnostic frame success cannot incorrectly heal a faulted sensor. */
    sensor_ok[0] = false;
    uint16_t filtered[48], baseline[48]; int16_t delta[48];
    assert(touch_read_frame(filtered, baseline, delta));
    assert(!touch_sensor_ok(0));
    tick(4);

    /* Overcurrent/stop are distinguishable from a healthy empty status. */
    set_touch(0, 0x8000); tick(4);
    assert(!touch_touched(0) && !touch_sensor_ok(0));
    assert(sensors[0].diag.overcurrent);
    assert(sensors[0].diag.forced_releases[TOUCH_RELEASE_OVERCURRENT] == 2);
    tick(1100);
    assert(touch_sensor_ok(0));
    registers[0x5a][0x5e] = 0x80;
    tick(260); assert(!touch_sensor_ok(0));
    tick(1100); assert(touch_sensor_ok(0));

    /* Configuration failure is surfaced, held state clears synchronously. */
    set_touch(0, 1); tick(4); assert(touch_touched(0));
    fail_write_reg = 0x41;
    touch_sensor_init();
    assert(!touch_touchmap() && !last_stat_reading);
    assert(sensors[0].diag.forced_releases[TOUCH_RELEASE_RECONFIGURE] == 1);
    tick(500);
    assert(!touch_sensor_mask() && sensors[0].diag.setup_failures);
    unsigned attempts = sensors[0].diag.recovery_attempts;
    tick(40); assert(sensors[0].diag.recovery_attempts == attempts);
    fail_write_reg = -1; tick(1600); assert(touch_sensor_mask() == 15);
    assert(touch_thresholds[0] == 22 && release_thresholds[0] == 15);
    touch_diagnostics_t diag;
    assert(touch_diagnostics(0, &diag) && diag.i2c_failures > 0 && diag.setup_failures > 0);
    /* 4 ms release/press pulses, invisible to a 25 Hz UI, remain in counters. */
    uint32_t raw_p = touch_raw_history()->presses[33], raw_r = touch_raw_history()->releases[33];
    uint32_t out_p = touch_output_history()->presses[33], out_r = touch_output_history()->releases[33];
    set_touch(2, 1u << 9); tick(4);
    set_touch(2, 0); tick(4);
    set_touch(2, 1u << 9); tick(4);
    assert(touch_raw_history()->presses[33] == raw_p + 2);
    assert(touch_raw_history()->releases[33] == raw_r + 1);
    assert(touch_output_history()->presses[33] == out_p + 2);
    assert(touch_output_history()->releases[33] == out_r + 1);
    input_history_t before_raw = *touch_raw_history(), before_out = *touch_output_history();
    touch_diagnostics(2, &diag);
    touch_read_frame(filtered, baseline, delta);
    assert(memcmp(&before_raw, touch_raw_history(), sizeof(before_raw)) == 0);
    assert(memcmp(&before_out, touch_output_history(), sizeof(before_out)) == 0);
    puts("touch_test: ok");
    return 0;
}

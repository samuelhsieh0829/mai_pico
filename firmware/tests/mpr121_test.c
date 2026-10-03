#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BOARD_MAI_PICO
#include "../src/mpr121.c"

#include "mpr121_bus_fake.h"

static void test_init_and_offline_detection(void)
{
    assert(mpr121_init(0x5a));
    assert(registers[0x5a][MPR121_ELECTRODE_CONFIG_REG] == 0x8c);
    assert(registers[0x5a][MPR121_AFE_CONFIG_REG] == 0xd0);
    assert(registers[0x5a][MPR121_FILTER_CONFIG_REG] == 0x40);
    assert(registers[0x5a][MPR121_AUTOCONFIG_CONTROL_0_REG] == 0xcb);
    /* SCTS must stay clear so high-capacitance wired electrodes get a CDTx search. */
    assert(registers[0x5a][MPR121_AUTOCONFIG_CONTROL_1_REG] == 0x00);
    assert(registers[0x5a][MPR121_AUTOCONFIG_USL_REG] == 201);
    assert(registers[0x5a][MPR121_AUTOCONFIG_LSL_REG] == 130);
    assert(registers[0x5a][MPR121_AUTOCONFIG_TARGET_REG] == 190);
    for (int i = 0; i < 12; i++) {
        assert(registers[0x5a][MPR121_TOUCH_THRESHOLD_REG + i * 2] == 22);
        assert(registers[0x5a][MPR121_RELEASE_THRESHOLD_REG + i * 2] == 15);
    }
    fail_addr = 0x5d;
    assert(!mpr121_init(0x5d));
    fail_addr = -1;
}

static void test_per_electrode_threshold_writes(void)
{
    uint8_t touch_values[12];
    uint8_t release_values[12];
    registers[0x5b][MPR121_ELECTRODE_CONFIG_REG] = 0x8c;
    for (int i = 0; i < 12; i++) {
        touch_values[i] = 30 + i;
        release_values[i] = 10 + i;
    }
    mpr121_thresholds(0x5b, touch_values, release_values, 12);
    for (int i = 0; i < 12; i++) {
        assert(registers[0x5b][MPR121_TOUCH_THRESHOLD_REG + i * 2] == touch_values[i]);
        assert(registers[0x5b][MPR121_RELEASE_THRESHOLD_REG + i * 2] == release_values[i]);
    }
    assert(registers[0x5b][MPR121_ELECTRODE_CONFIG_REG] == 0x8c);
}

static void test_frame_and_offline_zero_fill(void)
{
    for (int i = 0; i < 12; i++) {
        uint16_t filtered = 700 + i;
        registers[0x5c][MPR121_ELECTRODE_FILTERED_DATA_REG + i * 2] = filtered & 0xff;
        registers[0x5c][MPR121_ELECTRODE_FILTERED_DATA_REG + i * 2 + 1] = filtered >> 8;
        registers[0x5c][MPR121_BASELINE_VALUE_REG + i] = 200 + i;
    }
    uint16_t filtered[12];
    uint16_t baseline[12];
    assert(mpr121_frame(0x5c, filtered, baseline, 12));
    for (int i = 0; i < 12; i++) {
        assert(filtered[i] == 700 + i);
        assert(baseline[i] == (200 + i) * 4);
    }

    memset(filtered, 0xff, sizeof(filtered));
    memset(baseline, 0xff, sizeof(baseline));
    fail_read = true;
    assert(!mpr121_frame(0x5c, filtered, baseline, 12));
    fail_read = false;
    for (int i = 0; i < 12; i++) {
        assert(filtered[i] == 0);
        assert(baseline[i] == 0);
    }
}

static void test_latched_touch_status(void)
{
    registers[0x5c][MPR121_TOUCH_STATUS_REG] = 0x5a;
    registers[0x5c][MPR121_TOUCH_STATUS_REG + 1] = 0x09;

    uint16_t touched = 0;
    assert(mpr121_touch_status(0x5c, &touched));
    assert(touched == 0x095a);

    fail_read = true;
    touched = 0xffff;
    assert(!mpr121_touch_status(0x5c, &touched));
    assert(touched == 0);
    fail_read = false;
}

static void test_failed_setup_and_bounded_steps(void)
{
    fail_write_reg = 0x41;
    assert(!mpr121_init(0x5a));
    assert(registers[0x5a][0x5e] == 0); /* never run a partial configuration */
    fail_write_reg = -1;
    corrupt_read_reg = 0x5c;
    assert(!mpr121_init(0x5a));
    assert(registers[0x5a][0x5e] == 0);
    corrupt_read_reg = -1;
    uint8_t touch_values[12], release_values[12];
    memset(touch_values, 30, sizeof(touch_values));
    memset(release_values, 18, sizeof(release_values));
    mpr121_setup_t setup;
    mpr121_setup_begin(&setup, touch_values, release_values, 0xc0, 1, 2);
    int result;
    do {
        unsigned before = bus_operations;
        result = mpr121_setup_step(0x5a, &setup);
        /* At most one register write OR address+register read, never a batch. */
        assert(bus_operations - before <= 2);
        now_us += 1000;
    } while (!result);
    assert(result == 1 && registers[0x5a][0x5b] == 0x21);
    assert(registers[0x5a][0x41] == 30 && registers[0x5a][0x42] == 18);
    assert(registers[0x5a][0x5e] == 0x8c);

    fail_write_reg = 0x41;
    assert(!mpr121_thresholds(0x5a, touch_values, release_values, 12));
    fail_write_reg = 0x5b;
    assert(!mpr121_debounce(0x5a, 1, 2));
    fail_write_reg = -1;
    fail_read = true;
    assert(!mpr121_filter(0x5a, 3, 0, 0));
    fail_read = false;
}

static void test_health_flags_and_short_reads(void)
{
    uint16_t touched, oor; bool overcurrent;
    registers[0x5a][0] = 3;
    registers[0x5a][1] = 0x80;
    registers[0x5a][2] = 1;
    registers[0x5a][3] = 0xc0;
    assert(mpr121_health(0x5a, &touched, &oor, &overcurrent));
    assert(touched == 3 && oor == 0xc001 && overcurrent);
    short_read = true;
    assert(!mpr121_health(0x5a, &touched, &oor, &overcurrent));
    assert(!touched && !oor && !overcurrent);
    short_read = false;
}

int main(void)
{
    test_init_and_offline_detection();
    test_per_electrode_threshold_writes();
    test_frame_and_offline_zero_fill();
    test_latched_touch_status();
    test_failed_setup_and_bounded_steps();
    test_health_flags_and_short_reads();
    puts("mpr121_test: ok");
    return 0;
}

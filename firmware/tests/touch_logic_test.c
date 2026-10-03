#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "touch_logic.h"

static void test_touch_and_release_hysteresis(void)
{
    const uint8_t touch_thresholds[] = {22, 30, 40};
    const uint8_t release_thresholds[] = {15, 20, 25};
    int16_t delta[] = {21, 30, 100};

    uint16_t state = touch_apply_readings(
        0, delta, touch_thresholds, release_thresholds, 3);
    assert(state == 0x06);

    /* A pressed electrode remains pressed between its two thresholds. */
    delta[1] = 25;
    delta[2] = 25;
    state = touch_apply_readings(
        state, delta, touch_thresholds, release_thresholds, 3);
    assert(state == 0x06);

    /* It releases only after the reading falls below Release. */
    delta[1] = 19;
    delta[2] = 24;
    state = touch_apply_readings(
        state, delta, touch_thresholds, release_thresholds, 3);
    assert(state == 0);
}

static void test_negative_delta_never_presses(void)
{
    const uint8_t touch_thresholds[] = {2};
    const uint8_t release_thresholds[] = {1};
    const int16_t delta[] = {-100};

    assert(touch_apply_readings(
        0, delta, touch_thresholds, release_thresholds, 1) == 0);
}

int main(void)
{
    test_touch_and_release_hysteresis();
    test_negative_delta_never_presses();
    puts("touch_logic_test: ok");
    return 0;
}

#ifndef TOUCH_LOGIC_H
#define TOUCH_LOGIC_H

#include <stdint.h>

/*
 * Convert filtered-data deltas into electrode states.  A released electrode
 * must cross the touch threshold, while a pressed electrode stays pressed
 * until it falls below the lower release threshold.
 */
static inline uint16_t touch_apply_readings(
    uint16_t previous, const int16_t *delta, const uint8_t *touch_thresholds,
    const uint8_t *release_thresholds, unsigned count)
{
    uint16_t next = previous;

    for (unsigned i = 0; i < count; i++) {
        uint16_t bit = (uint16_t)(1u << i);
        uint8_t threshold = (previous & bit)
            ? release_thresholds[i]
            : touch_thresholds[i];

        if (delta[i] >= threshold) {
            next |= bit;
        } else {
            next &= (uint16_t)~bit;
        }
    }

    return next;
}

#endif

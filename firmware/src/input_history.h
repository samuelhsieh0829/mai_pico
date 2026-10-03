#ifndef INPUT_HISTORY_H
#define INPUT_HISTORY_H

#include <stdint.h>
#include "config.h"

/* Core 0 owns these counters. They observe input; they never filter it. */
typedef struct {
    uint64_t state;
    uint32_t presses[MAI_TOUCH_ZONE_COUNT];
    uint32_t releases[MAI_TOUCH_ZONE_COUNT];
} input_history_t;

static inline void input_history_update(input_history_t *history, uint64_t state)
{
    state &= (UINT64_C(1) << MAI_TOUCH_ZONE_COUNT) - 1;
    uint64_t pressed = state & ~history->state;
    uint64_t released = history->state & ~state;
    for (unsigned zone = 0; zone < MAI_TOUCH_ZONE_COUNT; zone++) {
        if (pressed & (UINT64_C(1) << zone)) history->presses[zone]++;
        if (released & (UINT64_C(1) << zone)) history->releases[zone]++;
    }
    history->state = state;
}

#endif

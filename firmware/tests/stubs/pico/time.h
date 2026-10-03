#ifndef TEST_PICO_TIME_H
#define TEST_PICO_TIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
struct repeating_timer { int unused; };
bool add_repeating_timer_us(int64_t delay, bool (*callback)(struct repeating_timer *), void *data, struct repeating_timer *timer);
void sleep_us(uint64_t us);
uint64_t time_us_64(void);
void sleep_ms(uint32_t ms);

#endif

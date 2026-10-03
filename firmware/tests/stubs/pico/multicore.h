#ifndef TEST_PICO_MULTICORE_H
#define TEST_PICO_MULTICORE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct { int unused; } mutex_t;
bool mutex_enter_timeout_us(mutex_t *mutex, uint64_t timeout_us);
void mutex_exit(mutex_t *mutex);
#endif

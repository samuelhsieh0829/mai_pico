#ifndef TEST_PICO_STDIO_H
#define TEST_PICO_STDIO_H

#include <stdint.h>
#include <stdbool.h>
uint64_t time_us_64(void);
void sleep_ms(uint32_t milliseconds);
bool stdio_usb_connected(void);

#endif

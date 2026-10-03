#ifndef TEST_PICO_BOOTROM_H
#define TEST_PICO_BOOTROM_H

#include <stdint.h>
uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t status);

#endif

#ifndef TEST_HARDWARE_FLASH_H
#define TEST_HARDWARE_FLASH_H

#include <stddef.h>
#include <stdint.h>

#define PICO_FLASH_SIZE_BYTES 8192u
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u

extern uint8_t fake_flash[FLASH_SECTOR_SIZE];
#define XIP_BASE ((uintptr_t)fake_flash - (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE))

void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count);
#endif

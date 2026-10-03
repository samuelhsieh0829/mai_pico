#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool tud_ready(void);
bool tud_hid_n_ready(uint8_t itf);
bool tud_hid_n_report(uint8_t itf, uint8_t id, const void *data, uint16_t size);
bool tud_cdc_n_connected(uint8_t itf);
uint32_t tud_cdc_n_available(uint8_t itf);
uint32_t tud_cdc_n_read(uint8_t itf, void *data, uint32_t size);
uint32_t tud_cdc_n_write_available(uint8_t itf);
uint32_t tud_cdc_n_write(uint8_t itf, const void *data, uint32_t size);
uint32_t tud_cdc_n_write_char(uint8_t itf, char c);
uint32_t tud_cdc_n_write_flush(uint8_t itf);

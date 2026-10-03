/*
 * Mai Controller Buttons
 * WHowe <github.com/whowechina>
 */

#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/flash.h"

void button_init();
bool button_start_scanning(void);
bool button_scanner_is_running(void);

typedef struct {
    uint16_t buttons;
    uint32_t coins;
    uint32_t sequence;
} button_event_t;
bool button_event_peek(unsigned consumer, button_event_t *event);
void button_event_ack(unsigned consumer, uint32_t sequence);
void button_events_resync(unsigned consumer);
uint32_t button_queue_overflows(unsigned consumer);
uint32_t button_scan_max_gap_us(void);
uint32_t button_coin_count(void);

/* if anykey is pressed, no debounce */
bool button_is_stuck();

uint8_t button_num();
void button_update();
uint16_t button_read();
uint8_t button_real_gpio(int id);
uint8_t button_default_gpio(int id);
bool button_gpio_allowed(uint8_t gpio);
uint16_t button_scan_levels();
const uint8_t *button_scan_gpios();

#endif

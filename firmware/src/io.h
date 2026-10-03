/*
 * Game I/O
 * WHowe <github.com/whowechina>
 */

#ifndef IO_H_
#define IO_H_

#include <stdbool.h>
#include "input_history.h"

typedef struct {
    input_history_t history;
    uint32_t packets, suspend_requests, usb_resets;
    uint32_t suspend_neutral_packets, reset_neutral_packets;
    uint64_t last_packet_us;
    int last_interface;
    bool valid;
} io_touch_diagnostics_t;

const io_touch_diagnostics_t *io_touch_diagnostics(void);

void io_update();
void io_suspend();
void io_flush_touch(void);
void io_usb_reset(void);
bool io_is_active();

#endif

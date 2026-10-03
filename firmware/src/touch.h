/*
 * Mai Pico Touch Keys
 * WHowe <github.com/whowechina>
 */

#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include <stdbool.h>
#include "input_history.h"

enum touch_keys {
    A1 = 0, A2, A3, A4, A5, A6, A7, A8,
    B1, B2, B3, B4, B5, B6, B7, B8,
    C1, C2, D1, D2, D3, D4, D5, D6, D7, D8,
    E1, E2, E3, E4, E5, E6, E7, E8,
    XX = 255
};

const char *touch_key_name(unsigned key);
int touch_key_by_name(const char *name);
int touch_key_channel(unsigned key);
unsigned touch_key_from_channel(unsigned channel);

typedef enum {
    TOUCH_RELEASE_STALE,
    TOUCH_RELEASE_OOR,
    TOUCH_RELEASE_OVERCURRENT,
    TOUCH_RELEASE_STOPPED,
    TOUCH_RELEASE_RECONFIGURE,
    TOUCH_RELEASE_RECOVERY,
    TOUCH_RELEASE_REASON_COUNT
} touch_release_reason_t;

typedef struct {
    uint32_t i2c_failures, setup_failures, recovery_attempts, bus_recoveries;
    uint16_t fault_channels, blocked_channels, oor;
    bool overcurrent, configuring;
    uint16_t raw_state;
    bool raw_valid;
    uint64_t raw_sample_us;
    uint32_t forced_releases[TOUCH_RELEASE_REASON_COUNT];
} touch_diagnostics_t;
bool touch_diagnostics(unsigned sensor, touch_diagnostics_t *out);
const input_history_t *touch_raw_history(void);
const input_history_t *touch_output_history(void);
void touch_init();
void touch_sensor_init();
void touch_reload_config();
void touch_update();
bool touch_touched(unsigned key);
uint64_t touch_touchmap();
void touch_set_map(unsigned sensor, unsigned key);
uint8_t touch_press_threshold(unsigned key);
uint8_t touch_release_threshold(unsigned key);

const uint16_t *touch_raw();
const uint16_t *map_raw_to_zones(const uint16_t *raw);
bool touch_sensor_ok(unsigned i);
uint8_t touch_sensor_mask();
bool touch_read_frame(uint16_t *filtered, uint16_t *baseline, int16_t *delta);

void touch_update_config();
unsigned touch_count(unsigned key);
void touch_reset_stat();

#endif

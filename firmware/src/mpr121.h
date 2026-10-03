/*
 * MPR121 Captive Touch Sensor
 * WHowe <github.com/whowechina>
 * 
 */

#ifndef MPR121_H
#define MPR121_H

#include <stdint.h>
#include <stdbool.h>

#define MPR121_BASE_ADDR 0x5A

typedef struct {
    uint8_t registers[64][2];
    unsigned count, index, phase;
    uint64_t reset_until;
} mpr121_setup_t;
void mpr121_setup_begin(mpr121_setup_t *setup, const uint8_t *touch,
                        const uint8_t *release, uint8_t filter,
                        uint8_t debounce_touch, uint8_t debounce_release);
/* One bounded bus operation per step. -1 = failed, 0 = pending, 1 = ready. */
int mpr121_setup_step(uint8_t addr, mpr121_setup_t *setup);
bool mpr121_health(uint8_t addr, uint16_t *touched, uint16_t *oor, bool *overcurrent);
bool mpr121_init(uint8_t addr);

uint16_t mpr121_touched(uint8_t addr);
bool mpr121_touch_status(uint8_t addr, uint16_t *touched);
bool mpr121_raw(uint8_t addr, uint16_t *raw, int num);
bool mpr121_frame(uint8_t addr, uint16_t *filtered, uint16_t *baseline,
                  int num);
bool mpr121_read_register(uint8_t addr, uint8_t reg, uint8_t *value);
bool mpr121_filter(uint8_t addr, uint8_t ffi, uint8_t sfi, uint8_t esi);
bool mpr121_sense(uint8_t addr, int8_t sense, int8_t *sense_keys, int num);
bool mpr121_thresholds(uint8_t addr, const uint8_t *touch,
                       const uint8_t *release, int num);
bool mpr121_debounce(uint8_t addr, uint8_t touch, uint8_t release);

#endif

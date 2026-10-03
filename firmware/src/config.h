/*
 * Controller Config
 * WHowe <github.com/whowechina>
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "board_defs.h"

#define MAI_TOUCH_SENSOR_COUNT 4
#define MAI_TOUCH_CHANNELS_PER_SENSOR 12
#define MAI_TOUCH_CHANNEL_COUNT (MAI_TOUCH_SENSOR_COUNT * MAI_TOUCH_CHANNELS_PER_SENSOR)
#define MAI_TOUCH_ZONE_COUNT 34
#define MAI_TOUCH_LEGACY_CHANNEL_COUNT 36
#define MAI_BUTTON_COUNT 12

#define MAI_TOUCH_DEFAULT_THRESHOLD 22
#define MAI_RELEASE_DEFAULT_THRESHOLD 15
#define MAI_TOUCH_FILTER_DEFAULT 0xC0
#define MAI_TOUCH_NOT_CONNECTED 0xff

#define MAI_CFG_EXT_MAGIC 0x31474643u /* "CFG1" */
#define MAI_CFG_EXT_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t touch_map_extra[MAI_TOUCH_CHANNEL_COUNT - MAI_TOUCH_LEGACY_CHANNEL_COUNT];
    uint8_t touch_thresholds[MAI_TOUCH_ZONE_COUNT];
    uint8_t release_thresholds[MAI_TOUCH_ZONE_COUNT];
    uint16_t button_active_high_mask;
} mai_cfg_ext_t;

typedef struct __attribute__((packed)) {
    struct {
        uint32_t not_used[2];
        uint8_t level;
    } color;
    struct {
        uint8_t filter;
        int8_t global;
        uint8_t debounce_touch;
        uint8_t debounce_release;        
        int8_t zones[MAI_TOUCH_ZONE_COUNT];
    } sense;
    struct {
        uint8_t io4 : 4;
        uint8_t nkro : 4;
    } hid;
    struct {
        uint8_t per_button;
        uint8_t per_cab : 4;
        uint8_t per_banner : 4;
    } rgb;
    struct {
        uint8_t buttons[MAI_BUTTON_COUNT];
        uint8_t touch[MAI_TOUCH_LEGACY_CHANNEL_COUNT];
    } alt;
    struct {
        uint8_t mode : 4;
        uint8_t virtual_aic : 4;
    } aime;
    struct {
        uint8_t main_button_active_high : 1;
        uint8_t aux_button_active_high : 1;
        uint8_t unused_bits : 6;
        uint8_t reserved[3];
    } tweak;
    uint8_t reserved[8];
    /* Appended so pre-CFG/1 flash pages keep their original field offsets. */
    mai_cfg_ext_t ext;
} mai_cfg_t;

_Static_assert(offsetof(mai_cfg_t, ext) == 114,
               "CFG/1 extension must not move legacy flash fields");
_Static_assert(sizeof(mai_cfg_t) <= 252,
               "mai_cfg_t must fit in the flash page payload");

typedef struct {
    uint16_t fps[2];
    bool key_stuck;
    struct {
        bool touch;
        bool led;
    } debug;
} mai_runtime_t;

typedef enum {
    CONFIG_PROFILE_OK = 0,
    CONFIG_PROFILE_EXTENSION,
    CONFIG_PROFILE_SENSORS,
    CONFIG_PROFILE_LEDS,
    CONFIG_PROFILE_TOUCH_MAP,
    CONFIG_PROFILE_THRESHOLDS,
    CONFIG_PROFILE_BUTTONS,
} config_profile_error_t;

extern mai_cfg_t *mai_cfg;
extern mai_runtime_t mai_runtime;

void config_init();
void config_changed(); // Notify the config has changed
void config_factory_reset(); // Reset the config to factory default

uint8_t config_touch_map_get(const mai_cfg_t *cfg, unsigned channel);
bool config_touch_map_set(mai_cfg_t *cfg, unsigned channel, uint8_t zone);
uint8_t config_button_gpio(const mai_cfg_t *cfg, unsigned id);
bool config_button_gpio_set(mai_cfg_t *cfg, unsigned id, uint8_t gpio);
bool config_button_active_high(const mai_cfg_t *cfg, unsigned id);
void config_button_active_high_set(mai_cfg_t *cfg, unsigned id, bool active_high);
void config_button_group_active_high_set(mai_cfg_t *cfg, bool main_group,
                                         bool active_high);
bool config_profile_valid(const mai_cfg_t *cfg, bool require_four_sensors,
                          uint8_t sensor_mask);
config_profile_error_t config_profile_validate(const mai_cfg_t *cfg,
                                               bool require_four_sensors,
                                               uint8_t sensor_mask);
bool config_replace(const mai_cfg_t *cfg);

#endif

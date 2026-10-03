/*
 * Controller Config and Runtime Data
 * WHowe <github.com/whowechina>
 * 
 * Config is a global data structure that stores all the configuration
 * Runtime is something to share between files.
 */

#include <string.h>

#include "config.h"
#include "save.h"
#include "touch.h"
#include "button.h"

mai_cfg_t *mai_cfg;

static mai_cfg_t default_cfg = {
    .color = {
        .level = 127,
    },
    .sense = {
        /* FFI=34 samples, SFI=4 samples, ESI=1 ms: 250 Hz nominal. */
        .filter = MAI_TOUCH_FILTER_DEFAULT,
        .debounce_touch = 1,
        .debounce_release = 2,
     },
    .hid = {
        .io4 = 1,
        .nkro = 0,
    },
    .rgb = {
        .per_button = 4,
        .per_cab = 1,
    },
    .alt = {
        .buttons = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        .touch = TOUCH_MAP,
    },
    .aime = {
        .mode = 0,
        .virtual_aic = 0,
    },
    .tweak = {
        .main_button_active_high = 0,
        .aux_button_active_high = 0,
    },
};

mai_runtime_t mai_runtime;

static inline bool in_range(int val, int min, int max)
{
    return (val >= min) && (val <= max);
}

static void config_init_extension(mai_cfg_t *cfg, bool migrate_legacy)
{
    cfg->ext.magic = MAI_CFG_EXT_MAGIC;
    cfg->ext.version = MAI_CFG_EXT_VERSION;
    memset(cfg->ext.touch_map_extra, MAI_TOUCH_NOT_CONNECTED,
           sizeof(cfg->ext.touch_map_extra));
    memset(cfg->ext.touch_thresholds, MAI_TOUCH_DEFAULT_THRESHOLD,
           sizeof(cfg->ext.touch_thresholds));
    memset(cfg->ext.release_thresholds, MAI_RELEASE_DEFAULT_THRESHOLD,
           sizeof(cfg->ext.release_thresholds));

    cfg->ext.button_active_high_mask = 0;
    if (migrate_legacy) {
        for (int i = 0; i < MAI_BUTTON_COUNT; i++) {
            bool active = i < 8 ? cfg->tweak.main_button_active_high
                                : cfg->tweak.aux_button_active_high;
            if (active) {
                cfg->ext.button_active_high_mask |= 1u << i;
            }
        }
    }
}

static bool config_extension_valid(const mai_cfg_t *cfg)
{
    return cfg->ext.magic == MAI_CFG_EXT_MAGIC &&
           cfg->ext.version == MAI_CFG_EXT_VERSION;
}

uint8_t config_touch_map_get(const mai_cfg_t *cfg, unsigned channel)
{
    if (channel >= MAI_TOUCH_CHANNEL_COUNT) {
        return MAI_TOUCH_NOT_CONNECTED;
    }
    if (channel < MAI_TOUCH_LEGACY_CHANNEL_COUNT) {
        return cfg->alt.touch[channel];
    }
    return cfg->ext.touch_map_extra[channel - MAI_TOUCH_LEGACY_CHANNEL_COUNT];
}

bool config_touch_map_set(mai_cfg_t *cfg, unsigned channel, uint8_t zone)
{
    if (channel >= MAI_TOUCH_CHANNEL_COUNT ||
        (zone >= MAI_TOUCH_ZONE_COUNT && zone != MAI_TOUCH_NOT_CONNECTED)) {
        return false;
    }
    if (channel < MAI_TOUCH_LEGACY_CHANNEL_COUNT) {
        cfg->alt.touch[channel] = zone;
    } else {
        cfg->ext.touch_map_extra[channel - MAI_TOUCH_LEGACY_CHANNEL_COUNT] = zone;
    }
    return true;
}

uint8_t config_button_gpio(const mai_cfg_t *cfg, unsigned id)
{
    if (id >= MAI_BUTTON_COUNT) {
        return 0xff;
    }
    uint8_t gpio = cfg->alt.buttons[id];
    return gpio <= 29 ? gpio : button_default_gpio(id);
}

bool config_button_gpio_set(mai_cfg_t *cfg, unsigned id, uint8_t gpio)
{
    if (id >= MAI_BUTTON_COUNT || !button_gpio_allowed(gpio)) {
        return false;
    }
    cfg->alt.buttons[id] = gpio == button_default_gpio(id) ? 0xff : gpio;
    return true;
}

bool config_button_active_high(const mai_cfg_t *cfg, unsigned id)
{
    if (id >= MAI_BUTTON_COUNT) {
        return false;
    }
    if (!config_extension_valid(cfg)) {
        return id < 8 ? cfg->tweak.main_button_active_high
                      : cfg->tweak.aux_button_active_high;
    }
    return (cfg->ext.button_active_high_mask & (1u << id)) != 0;
}

void config_button_active_high_set(mai_cfg_t *cfg, unsigned id, bool active_high)
{
    if (id >= MAI_BUTTON_COUNT) {
        return;
    }
    if (active_high) {
        cfg->ext.button_active_high_mask |= 1u << id;
    } else {
        cfg->ext.button_active_high_mask &= ~(1u << id);
    }
}

void config_button_group_active_high_set(mai_cfg_t *cfg, bool main_group,
                                         bool active_high)
{
    unsigned first = main_group ? 0 : 8;
    unsigned last = main_group ? 8 : MAI_BUTTON_COUNT;
    for (unsigned i = first; i < last; i++) {
        config_button_active_high_set(cfg, i, active_high);
    }
    if (main_group) {
        cfg->tweak.main_button_active_high = active_high;
    } else {
        cfg->tweak.aux_button_active_high = active_high;
    }
}

static bool touch_map_valid(const mai_cfg_t *cfg)
{
    uint64_t mask = 0;
    for (int i = 0; i < MAI_TOUCH_CHANNEL_COUNT; i++) {
        uint8_t zone = config_touch_map_get(cfg, i);
        if (zone < MAI_TOUCH_ZONE_COUNT) {
            mask |= 1ULL << zone;
        }
    }
    int keys = 0;
    for (int i = 0; i < MAI_TOUCH_ZONE_COUNT; i++) {
        if (mask & (1ULL << i)) {
            keys++;
        }
    }
    return keys > 10; // bad data results in low touch key coverage
}

static void config_loaded()
{
    if (!config_extension_valid(mai_cfg)) {
        config_init_extension(mai_cfg, true);
        config_changed();
    }

    /* Packed FFI[7:6], SFI[5:4], reserved[3], ESI[2:0]. All 128
       combinations accepted by the filter command are valid. */
    if (mai_cfg->sense.filter & 0x08) {
        mai_cfg->sense.filter = default_cfg.sense.filter;
        config_changed();
    }
    if (!in_range(mai_cfg->sense.global, -9, 9)) {
        mai_cfg->sense = default_cfg.sense;
        config_changed();
    }
    for (int i = 0; i < MAI_TOUCH_ZONE_COUNT; i++) {
        if (!in_range(mai_cfg->sense.zones[i], -9, 9)) {
            mai_cfg->sense = default_cfg.sense;
            config_changed();
            break;
        }
    }
    if (!in_range(mai_cfg->sense.debounce_touch, 0, 7) ||
        !in_range(mai_cfg->sense.debounce_release, 0, 7)) {
        mai_cfg->sense = default_cfg.sense;
        config_changed();
    }

    if (!in_range(mai_cfg->rgb.per_button, 0, 16) ||
        !in_range(mai_cfg->rgb.per_cab, 0, 15) ||
        !in_range(mai_cfg->rgb.per_banner, 0, 15)) {
        mai_cfg->rgb = default_cfg.rgb;
        config_changed();
    }

    if (!touch_map_valid(mai_cfg)) {
        memcpy(mai_cfg->alt.touch, default_cfg.alt.touch,
               sizeof(mai_cfg->alt.touch));
        config_changed();
    }
}

config_profile_error_t config_profile_validate(const mai_cfg_t *cfg,
                                               bool require_four_sensors,
                                               uint8_t sensor_mask)
{
    if (!config_extension_valid(cfg)) {
        return CONFIG_PROFILE_EXTENSION;
    }
    if (require_four_sensors && (sensor_mask & 0x0f) != 0x0f) {
        return CONFIG_PROFILE_SENSORS;
    }
    if (cfg->rgb.per_button > 16) {
        return CONFIG_PROFILE_LEDS;
    }

    uint64_t zones = 0;
    for (unsigned channel = 0; channel < MAI_TOUCH_CHANNEL_COUNT; channel++) {
        uint8_t zone = config_touch_map_get(cfg, channel);
        if (zone == MAI_TOUCH_NOT_CONNECTED) {
            continue;
        }
        if (zone >= MAI_TOUCH_ZONE_COUNT || (zones & (1ULL << zone))) {
            return CONFIG_PROFILE_TOUCH_MAP;
        }
        zones |= 1ULL << zone;
    }
    if (zones != ((1ULL << MAI_TOUCH_ZONE_COUNT) - 1)) {
        return CONFIG_PROFILE_TOUCH_MAP;
    }

    for (unsigned zone = 0; zone < MAI_TOUCH_ZONE_COUNT; zone++) {
        uint8_t touch = cfg->ext.touch_thresholds[zone];
        uint8_t release = cfg->ext.release_thresholds[zone];
        if (release == 0 || release >= touch) {
            return CONFIG_PROFILE_THRESHOLDS;
        }
    }

    uint32_t gpio_seen = 0;
    for (unsigned id = 0; id < MAI_BUTTON_COUNT; id++) {
        uint8_t gpio = config_button_gpio(cfg, id);
        if (!button_gpio_allowed(gpio) || (gpio_seen & (1u << gpio))) {
            return CONFIG_PROFILE_BUTTONS;
        }
        gpio_seen |= 1u << gpio;
    }
    return CONFIG_PROFILE_OK;
}

bool config_profile_valid(const mai_cfg_t *cfg, bool require_four_sensors,
                          uint8_t sensor_mask)
{
    return config_profile_validate(cfg, require_four_sensors, sensor_mask) ==
           CONFIG_PROFILE_OK;
}

void config_changed()
{
    save_request(false);
}

void config_factory_reset()
{
    *mai_cfg = default_cfg;
    save_request(true);
}

bool config_replace(const mai_cfg_t *cfg)
{
    mai_cfg_t previous = *mai_cfg;
    *mai_cfg = *cfg;
    if (!save_request(true)) {
        *mai_cfg = previous;
        save_request(true);
        return false;
    }
    return true;
}

void config_init()
{
    config_init_extension(&default_cfg, false);
    mai_cfg = (mai_cfg_t *)save_alloc(sizeof(*mai_cfg), &default_cfg, config_loaded);
}

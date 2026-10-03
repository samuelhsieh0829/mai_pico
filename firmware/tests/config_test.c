#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BOARD_MAI_PICO
#include "../src/config.c"

static uint8_t save_storage[252];
static int fail_immediate_saves;
static unsigned save_requests;

void *save_alloc(size_t size, void *def, void (*after_load)())
{
    assert(size <= sizeof(save_storage));
    memcpy(save_storage, def, size);
    (void)after_load;
    return save_storage;
}

bool save_request(bool immediately)
{
    save_requests++;
    if (immediately && fail_immediate_saves > 0) {
        fail_immediate_saves--;
        return false;
    }
    return true;
}

static const uint8_t fixed_gpios[MAI_BUTTON_COUNT] = BUTTON_DEF;

uint8_t button_default_gpio(int id)
{
    return id >= 0 && id < MAI_BUTTON_COUNT ? fixed_gpios[id] : 0xff;
}

bool button_gpio_allowed(uint8_t gpio)
{
    for (int i = 0; i < MAI_BUTTON_COUNT; i++) {
        if (fixed_gpios[i] == gpio) {
            return true;
        }
    }
    return false;
}

static void test_default_and_48_channel_mapping(void)
{
    config_init();
    assert(sizeof(mai_cfg_t) == 201);
    assert(offsetof(mai_cfg_t, ext) == 114);
    assert(mai_cfg->rgb.per_button == 4);
    assert(mai_cfg->sense.filter == MAI_TOUCH_FILTER_DEFAULT);
    assert(config_profile_valid(mai_cfg, true, 0x0f));
    assert(!config_profile_valid(mai_cfg, true, 0x07));
    assert(config_profile_valid(mai_cfg, false, 0x00));
    assert(config_profile_validate(mai_cfg, true, 0x07) ==
           CONFIG_PROFILE_SENSORS);

    for (int channel = 36; channel < MAI_TOUCH_CHANNEL_COUNT; channel++) {
        assert(config_touch_map_get(mai_cfg, channel) == MAI_TOUCH_NOT_CONNECTED);
    }
    assert(!config_touch_map_set(mai_cfg, 48, 0));
    assert(!config_touch_map_set(mai_cfg, 0, 254));

    for (int channel = 0; channel < MAI_TOUCH_CHANNEL_COUNT; channel++) {
        assert(config_touch_map_set(mai_cfg, channel, MAI_TOUCH_NOT_CONNECTED));
    }
    for (int zone = 0; zone < MAI_TOUCH_ZONE_COUNT; zone++) {
        assert(config_touch_map_set(mai_cfg, zone, zone));
    }
    assert(config_profile_valid(mai_cfg, true, 0x0f));
    assert(config_touch_map_set(mai_cfg, 34, 0));
    assert(!config_profile_valid(mai_cfg, true, 0x0f));
    assert(config_profile_validate(mai_cfg, false, 0) ==
           CONFIG_PROFILE_TOUCH_MAP);
    assert(config_touch_map_set(mai_cfg, 34, MAI_TOUCH_NOT_CONNECTED));
}

static void test_extension_migration_and_thresholds(void)
{
    mai_cfg_t *original = mai_cfg;
    mai_cfg_t legacy = *mai_cfg;
    legacy.ext.magic = 0;
    legacy.tweak.main_button_active_high = 1;
    legacy.tweak.aux_button_active_high = 0;
    mai_cfg = &legacy;
    config_loaded();

    assert(legacy.ext.magic == MAI_CFG_EXT_MAGIC);
    assert(legacy.ext.version == MAI_CFG_EXT_VERSION);
    for (int zone = 0; zone < MAI_TOUCH_ZONE_COUNT; zone++) {
        assert(legacy.ext.touch_thresholds[zone] == MAI_TOUCH_DEFAULT_THRESHOLD);
        assert(legacy.ext.release_thresholds[zone] == MAI_RELEASE_DEFAULT_THRESHOLD);
    }
    for (int id = 0; id < MAI_BUTTON_COUNT; id++) {
        assert(config_button_active_high(&legacy, id) == (id < 8));
    }

    legacy.ext.release_thresholds[0] = legacy.ext.touch_thresholds[0];
    assert(!config_profile_valid(&legacy, true, 0x0f));
    assert(config_profile_validate(&legacy, false, 0) ==
           CONFIG_PROFILE_THRESHOLDS);
    legacy.ext.release_thresholds[0] = MAI_RELEASE_DEFAULT_THRESHOLD;

    legacy.sense.filter = 0x10;
    config_loaded();
    assert(legacy.sense.filter == 0x10);
    mai_cfg = original;
}

static void test_buttons_and_transaction_rollback(void)
{
    for (int id = 0; id < MAI_BUTTON_COUNT; id++) {
        assert(config_button_gpio(mai_cfg, id) == fixed_gpios[id]);
    }
    assert(!config_button_gpio_set(mai_cfg, 0, 28));
    assert(config_button_gpio_set(mai_cfg, 0, fixed_gpios[1]));
    assert(!config_profile_valid(mai_cfg, false, 0));
    assert(config_button_gpio_set(mai_cfg, 0, fixed_gpios[0]));

    mai_cfg_t replacement = *mai_cfg;
    replacement.rgb.per_button = 7;
    mai_cfg_t before = *mai_cfg;
    fail_immediate_saves = 1;
    assert(!config_replace(&replacement));
    assert(memcmp(mai_cfg, &before, sizeof(before)) == 0);
    assert(config_replace(&replacement));
    assert(mai_cfg->rgb.per_button == 7);
}

static void test_filter_load_preserves_custom_sense(void)
{
    mai_cfg_t *original = mai_cfg;
    mai_cfg_t reference = *original;
    reference.sense.global = 4;
    reference.sense.zones[0] = 3;
    reference.sense.zones[33] = -4;
    reference.sense.debounce_touch = 6;
    reference.sense.debounce_release = 5;
    for (unsigned filter = 0; filter <= 255; filter++) {
        mai_cfg_t loaded = reference;
        loaded.sense.filter = filter;
        mai_cfg = &loaded;
        mai_cfg_t expected = loaded;
        if (filter & 8) expected.sense.filter = MAI_TOUCH_FILTER_DEFAULT;
        unsigned before = save_requests;
        config_loaded();
        assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
        assert(save_requests - before == ((filter & 8) ? 1u : 0u));
        before = save_requests;
        config_loaded(); /* A subsequent boot must not dirty valid configuration. */
        assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
        assert(save_requests == before);
    }
    mai_cfg = original;
}

int main(void)
{
    test_default_and_48_channel_mapping();
    test_extension_migration_and_thresholds();
    test_buttons_and_transaction_rollback();
    test_filter_load_preserves_custom_sense();
    puts("config_test: ok");
    return 0;
}

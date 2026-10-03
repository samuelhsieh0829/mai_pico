#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BOARD_MAI_PICO
#include "../src/button.c"

mai_cfg_t config_storage;
mai_cfg_t *mai_cfg = &config_storage;

static uint8_t configured_gpio[MAI_BUTTON_COUNT] = BUTTON_DEF;
static bool configured_active_high[MAI_BUTTON_COUNT];
static bool gpio_level[30];
static bool gpio_pull_up_enabled[30];
static bool gpio_pull_down_enabled[30];
static uint64_t now_us;

uint8_t config_button_gpio(const mai_cfg_t *cfg, unsigned id)
{
    (void)cfg;
    return configured_gpio[id];
}

bool config_button_active_high(const mai_cfg_t *cfg, unsigned id)
{
    (void)cfg;
    return configured_active_high[id];
}

void gpio_init(uint gpio) { (void)gpio; }
void gpio_set_function(uint gpio, int function)
{
    (void)gpio;
    (void)function;
}
void gpio_set_dir(uint gpio, bool out)
{
    (void)gpio;
    (void)out;
}
void gpio_pull_up(uint gpio)
{
    gpio_pull_up_enabled[gpio] = true;
    gpio_pull_down_enabled[gpio] = false;
    gpio_level[gpio] = true;
}
void gpio_pull_down(uint gpio)
{
    gpio_pull_up_enabled[gpio] = false;
    gpio_pull_down_enabled[gpio] = true;
    gpio_level[gpio] = false;
}
bool gpio_get(uint gpio) { return gpio_level[gpio]; }
uint64_t time_us_64(void) { return now_us; }
bool cfg_is_active(void) { return false; }
bool add_repeating_timer_us(int64_t delay, bool (*callback)(struct repeating_timer *), void *data, struct repeating_timer *timer)
{ (void)callback; (void)data; (void)timer; assert(delay == -1000); return true; }


static void test_mapping_and_active_levels_apply_together(void)
{
    configured_gpio[0] = 0;
    configured_gpio[1] = 1;
    configured_active_high[0] = true;
    configured_active_high[1] = false;

    button_init();

    assert(button_real_gpio(0) == 0);
    assert(button_real_gpio(1) == 1);
    assert(gpio_pull_down_enabled[0]);
    assert(gpio_pull_up_enabled[1]);
    assert(!button_is_stuck());

    gpio_level[0] = true;
    gpio_level[1] = false;
    button_update();
    assert((button_read() & 0x0003) == 0x0003);
}

static void test_polarity_changes_only_after_reinitialising_hardware(void)
{
    configured_active_high[0] = false;
    gpio_level[0] = false;

    /* The runtime keeps one coherent GPIO/polarity snapshot. */
    assert(!button_pressed(0));

    button_init();
    assert(gpio_pull_up_enabled[0]);
    gpio_level[0] = false;
    assert(button_pressed(0));
}

int main(void)
{
    memset(&config_storage, 0, sizeof(config_storage));
    test_mapping_and_active_levels_apply_together();
    test_polarity_changes_only_after_reinitialising_hardware();
    puts("button_test: ok");
    return 0;
}

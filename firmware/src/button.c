/*
 * Mai Controller Buttons
 * WHowe <github.com/whowechina>
 * 
 */

#include "button.h"

#include <stdint.h>
#include <stdbool.h>

#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "cfg_protocol.h"

#include "config.h"
#include "board_defs.h"

static const uint8_t gpio_def[] = BUTTON_DEF;
static uint8_t gpio_real[] = BUTTON_DEF;
static bool active_high_real[MAI_BUTTON_COUNT];

#define BUTTON_NUM MAI_BUTTON_COUNT

static_assert(sizeof(gpio_def) == BUTTON_NUM, "BUTTON_DEF must contain 12 GPIOs");

static bool sw_val[BUTTON_NUM]; /* true if pressed */
static uint64_t sw_freeze_time[BUTTON_NUM];
static uint16_t button_reading;
static uint32_t coin_count;
static uint32_t event_sequence;
static uint32_t max_scan_gap;
static uint64_t last_scan;
static bool scanned;
static struct repeating_timer scan_timer;
static bool scanner_running;

#define EVENT_CAPACITY 64
static struct {
    button_event_t events[EVENT_CAPACITY];
    unsigned head, count;
    uint32_t overflows;
} queues[2];

/* Producer runs only in the scan IRQ. Main-core consumers mask that IRQ. */
static void enqueue(unsigned consumer, uint16_t buttons)
{
    unsigned pos = (queues[consumer].head + queues[consumer].count) % EVENT_CAPACITY;
    queues[consumer].events[pos] = (button_event_t){buttons, coin_count, ++event_sequence};
    queues[consumer].count++;
}

static void resync(unsigned consumer)
{
    queues[consumer].head = queues[consumer].count = 0;
    enqueue(consumer, 0);
    if (button_reading) enqueue(consumer, button_reading);
}

void button_events_resync(unsigned consumer)
{
    if (consumer >= 2) return;
    uint32_t irq = save_and_disable_interrupts();
    resync(consumer);
    restore_interrupts(irq);
}

bool button_event_peek(unsigned consumer, button_event_t *event)
{
    if (consumer >= 2 || !event) return false;
    uint32_t irq = save_and_disable_interrupts();
    bool available = queues[consumer].count != 0;
    if (available) *event = queues[consumer].events[queues[consumer].head];
    restore_interrupts(irq);
    return available;
}

void button_event_ack(unsigned consumer, uint32_t sequence)
{
    if (consumer >= 2) return;
    uint32_t irq = save_and_disable_interrupts();
    /* An overflow during USB submission may have replaced the head. */
    if (queues[consumer].count &&
        queues[consumer].events[queues[consumer].head].sequence == sequence) {
        queues[consumer].head = (queues[consumer].head + 1) % EVENT_CAPACITY;
        queues[consumer].count--;
    }
    restore_interrupts(irq);
}

uint32_t button_queue_overflows(unsigned consumer)
{
    return consumer < 2 ? queues[consumer].overflows : 0;
}

uint32_t button_scan_max_gap_us(void) { return max_scan_gap; }
uint32_t button_coin_count(void) { return coin_count; }
bool button_scanner_is_running(void) { return scanner_running; }

void button_init()
{
    uint32_t irq = save_and_disable_interrupts();
    button_reading = 0;
    for (int i = 0; i < BUTTON_NUM; i++)
    {
        sw_val[i] = false;
        sw_freeze_time[i] = 0;
        uint8_t gpio = config_button_gpio(mai_cfg, i);
        bool active_high = config_button_active_high(mai_cfg, i);
        gpio_real[i] = gpio;
        active_high_real[i] = active_high;
        gpio_init(gpio);
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_IN);
        /* Keep an idle input away from its configured active level. */
        if (active_high) {
            gpio_pull_down(gpio);
        } else {
            gpio_pull_up(gpio);
        }
    }
    resync(0);
    resync(1);
    restore_interrupts(irq);
}
static inline bool button_pressed(int id)
{
    bool reading = gpio_get(gpio_real[id]);
    return reading == active_high_real[id];
}

bool button_is_stuck()
{
    for (int i = 0; i < BUTTON_NUM; i++) {
        if (button_pressed(i)) {
            return true;
        }
    }
    return false;
}

uint8_t button_num()
{
    return BUTTON_NUM;
}

uint8_t button_real_gpio(int id)
{
    if (id >= BUTTON_NUM) {
        return 0xff;
    }
    return gpio_real[id];
}

uint8_t button_default_gpio(int id)
{
    if (id >= BUTTON_NUM) {
        return 0xff;
    }
    return gpio_def[id];
}

bool button_gpio_allowed(uint8_t gpio)
{
    for (int i = 0; i < BUTTON_NUM; i++) {
        if (gpio_def[i] == gpio) {
            return true;
        }
    }
    return false;
}

const uint8_t *button_scan_gpios()
{
    return gpio_def;
}

uint16_t button_scan_levels()
{
    uint16_t levels = 0;
    for (int i = 0; i < BUTTON_NUM; i++) {
        uint8_t gpio = gpio_def[i];
        gpio_init(gpio);
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_pull_up(gpio);
        if (gpio_get(gpio)) {
            levels |= 1u << i;
        }
    }
    return levels;
}

/* If a switch flips, it freezes for a while */
#define DEBOUNCE_FREEZE_TIME_US 3000
void button_update()
{
    uint64_t now = time_us_64();
    if (scanned && now - last_scan > max_scan_gap) {
        max_scan_gap = now - last_scan > UINT32_MAX ? UINT32_MAX : (uint32_t)(now - last_scan);
    }
    last_scan = now;
    scanned = true;
    uint16_t buttons = 0;

    for (int i = BUTTON_NUM - 1; i >= 0; i--) {
        bool sw_pressed = button_pressed(i);
        
        if (now >= sw_freeze_time[i]) {
            if (sw_pressed != sw_val[i]) {
                sw_val[i] = sw_pressed;
                sw_freeze_time[i] = now + DEBOUNCE_FREEZE_TIME_US;
            }
        }

        buttons <<= 1;
        if (sw_val[i]) {
            buttons |= 1;
        }
    }

    if (buttons != button_reading) {
        if ((buttons & ~button_reading) & (1u << 11)) coin_count++;
        button_reading = buttons;
        for (unsigned consumer = 0; consumer < 2; consumer++) {
            /* Disabled interfaces must not accumulate a diagnostic overflow. */
            if ((consumer == 0 && !mai_cfg->hid.io4) ||
                (consumer == 1 && !mai_cfg->hid.nkro)) continue;
            if (queues[consumer].count == EVENT_CAPACITY) {
                queues[consumer].overflows++;
                resync(consumer);
            } else {
                enqueue(consumer, buttons);
            }
        }
    }
}

static bool scan_callback(struct repeating_timer *timer)
{
    (void)timer;
    if (!cfg_is_active()) button_update();
    else scanned = false;
    return true;
}

bool button_start_scanning(void)
{
    if (!scanner_running) scanner_running = add_repeating_timer_us(-1000, scan_callback, NULL, &scan_timer);
    return scanner_running;
}

uint16_t button_read()
{
    return button_reading;
}

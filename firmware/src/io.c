#include <stdint.h>
#include <stdbool.h>

#include "io.h"
#include "config.h"
#include "cfg_protocol.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>
#include "tusb.h"
#include "usb_descriptors.h"

#include "touch.h"
#include "rgb.h"

#define IO_TIMEOUT_SEC 300

#define DEBUG(x, ...) { if (mai_runtime.debug.x) { printf(__VA_ARGS__); } }

static struct {
    bool stat;
    uint64_t last_io_time;
    int touch_interface;
} ctx = { false, 0, 0 };

typedef union {
    uint8_t raw[28];
    struct {
        uint8_t body;
        uint8_t bezel;
        uint8_t side;
    };
    struct {
        uint8_t index;
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };
    struct {
        uint8_t start;
        uint8_t len;
        uint8_t skip;
        uint8_t mr;
        uint8_t mg;
        uint8_t mb;
        uint8_t speed;                    
    };
    struct {
        uint8_t addr;
        uint8_t data;
    } eeprom;
    uint8_t rgb[11][3];
} led_data_t;

typedef union {
    uint8_t raw[32];
    struct {
        struct {
            uint8_t dst;
            uint8_t src;
            uint8_t len;
            uint8_t status;
            uint8_t cmd;
            uint8_t report;
        } hdr;
        uint8_t payload[0];
    };
} led_resp_t;

#define SYNC 0xE0
#define ESCAPE 0xD0

typedef struct {
    int interface;
    bool connected;
    bool in_cmd;
    bool is_touch;
    bool escape;
    union {
        uint8_t buf[48];
        struct {
            struct {
                uint8_t dst;
                uint8_t src;
                uint8_t len;
                uint8_t cmd;
            } hdr;
            led_data_t led;
        };
    };
    uint8_t len;
    uint8_t checksum;
} cdc_t;

static cdc_t cdc[2] = {
    { .interface = 1 },
    { .interface = 2 },
};

static void touch_cmd(cdc_t *cdc)
{
    cdc->in_cmd = false;
    if (cdc->len != 4) {
        return;
    }
    cdc->len = 0;
    ctx.last_io_time = time_us_64();

    ctx.touch_interface = cdc->interface;

    switch (cdc->buf[2]) {
        case 'E':
            DEBUG(touch, "Touch RSET\n");
            break;
        case 'L':
            DEBUG(touch, "Touch HALT\n");
            ctx.stat = false;
            break;
        case 'A':
            DEBUG(touch, "Touch STAT\n");
            ctx.stat = true;
            break;
        case 'r':
            DEBUG(touch, "Touch Ratio\n");
            tud_cdc_n_write_char(cdc->interface, '(');
            tud_cdc_n_write_char(cdc->interface, cdc->buf[0]); //L,R
            tud_cdc_n_write_char(cdc->interface, cdc->buf[1]); //sensor
            tud_cdc_n_write_char(cdc->interface, 'r');
            tud_cdc_n_write_char(cdc->interface, cdc->buf[3]); // Ratio
            tud_cdc_n_write_char(cdc->interface, ')');
            tud_cdc_n_write_flush(cdc->interface);
            break;
        case 'k':
            DEBUG(touch, "Touch Sense\n");
            tud_cdc_n_write_char(cdc->interface, '(');
            tud_cdc_n_write_char(cdc->interface, cdc->buf[0]); //L,R
            tud_cdc_n_write_char(cdc->interface, cdc->buf[1]); //sensor
            tud_cdc_n_write_char(cdc->interface, 'k');
            tud_cdc_n_write_char(cdc->interface, cdc->buf[3]); // Ratio
            tud_cdc_n_write_char(cdc->interface, ')');
            tud_cdc_n_write_flush(cdc->interface);
            break;
        default:
            DEBUG(touch, "Touch CMD Unknown: %*s -> ", 4, cdc->buf);
            return;
    }
}

static void led_write(cdc_t *cdc, led_resp_t *resp)
{
    tud_cdc_n_write_char(cdc->interface, SYNC); // SYNC

    uint8_t checksum = 0;
    for (int i = 0; i < resp->hdr.len + 3; i++) {
        uint8_t c = resp->raw[i];
        checksum += c;
        if (c == SYNC || c == ESCAPE) {
            tud_cdc_n_write_char(cdc->interface, ESCAPE);
            tud_cdc_n_write_char(cdc->interface, c - 1);
        } else {
            tud_cdc_n_write_char(cdc->interface, c);
        }
    }
    tud_cdc_n_write_char(cdc->interface, checksum);
    tud_cdc_n_write_flush(cdc->interface);
}

static led_resp_t *led_init_resp(cdc_t *cdc, uint8_t payload_len)
{
    static led_resp_t resp;
    resp.hdr.dst = cdc->hdr.src;
    resp.hdr.src = cdc->hdr.dst;
    resp.hdr.len = payload_len + 3;
    resp.hdr.status = 1;
    resp.hdr.cmd = cdc->hdr.cmd;
    resp.hdr.report = 1;
    return &resp;
}

static void led_ack_ok(cdc_t *cdc)
{
    led_resp_t *resp = led_init_resp(cdc, 0);
    led_write(cdc, resp);
}

static uint8_t led_ram[256];

static void led_set_eeprom(cdc_t *cdc)
{
    led_ram[cdc->led.eeprom.addr] = cdc->led.eeprom.data;
    led_ack_ok(cdc);
}

static void led_get_eeprom(cdc_t *cdc)
{
    led_resp_t *resp = led_init_resp(cdc, 1);
    resp->payload[0] = led_ram[cdc->led.eeprom.addr];
    led_write(cdc, resp);
}

static void led_board_info(cdc_t *cdc)
{
    led_resp_t *resp = led_init_resp(cdc, 10);
    memcpy(resp->payload, "15070-04\xff\x90", 10);
    led_write(cdc, resp);
}

static void led_board_status(cdc_t *cdc)
{
    led_resp_t *resp = led_init_resp(cdc, 4);
    memcpy(resp->payload, "\x00\x00\x00\x00", 4);
    led_write(cdc, resp);
}

static void led_proto_ver(cdc_t *cdc)
{
    led_resp_t *resp = led_init_resp(cdc, 3);
    memcpy(resp->payload, "\x01\x00\x00", 3);
    led_write(cdc, resp);
}

static void led_cmd(cdc_t *cdc)
{
    cdc->in_cmd = false;
    cdc->len = 0;
    ctx.last_io_time = time_us_64();

    uint32_t color;
    switch (cdc->hdr.cmd) {
        case 0x10:
            DEBUG(led, "LED RSET\n");
            for (int i = 0; i < 8; i++) {
                rgb_set_button(i, 0, 0);
            }
            for (int i = 0; i < 3; i++) {
                rgb_set_cab(i, 0);
            }
            break;
        case 0x31:
            color = rgb32(cdc->led.r, cdc->led.g, cdc->led.b, false);
            DEBUG(led, "LED %d:1 %06lx\n", cdc->led.index, (unsigned long)color);
            rgb_set_button(cdc->led.index, color, 0);          
            break;
        case 0x32:
            color = rgb32(cdc->led.mr, cdc->led.mg, cdc->led.mb, false);
            DEBUG(led, "LED %d:%d %06lx\n", cdc->led.start, cdc->led.len, (unsigned long)color);
            for (int i = 0; i < cdc->led.len; i++) {
                rgb_set_button(i + cdc->led.start, color, 0);
            }
            break;
        case 0x33:
            color = rgb32(cdc->led.mr, cdc->led.mg, cdc->led.mb, false);
            DEBUG(led, "LED %d:%d %06lx %d\n", cdc->led.start, cdc->led.len, (unsigned long)color, cdc->led.speed);
            for (int i = 0; i < cdc->led.len; i++) {
                rgb_set_button(i + cdc->led.start, color, cdc->led.speed);
            }
            break;
        case 0x39:
            DEBUG(led, "LED Cabinet %02x %02x %02x\n",
                  cdc->led.body, cdc->led.bezel, cdc->led.side);
            rgb_set_cab(0, gray32(cdc->led.body, false));
            rgb_set_cab(1, gray32(cdc->led.bezel, false));
            rgb_set_cab(2, gray32(cdc->led.side, false));
            break;

        case 0x7b:
            led_set_eeprom(cdc);
            return;
        case 0x7c:
            led_get_eeprom(cdc);
            return;
        case 0xf0:
            led_board_info(cdc);
            return;
        case 0xf1:
            led_board_status(cdc);
            return;
        case 0xf3:
            led_proto_ver(cdc);
            return;

        default:
            DEBUG(led, "Ignoring LED Cmd %02x\n", cdc->hdr.cmd);
            break;
    }

    led_ack_ok(cdc);
}

static inline void assemble_cmd(cdc_t *cdc, uint8_t c)
{
    if (c == SYNC) {
        cdc->len = 0;
        cdc->in_cmd = true;
        cdc->is_touch = false;
        cdc->escape = false;
        cdc->checksum = 0;
        return;
    }

    if ((!cdc->in_cmd || cdc->is_touch) && (c == '{')) {
        cdc->len = 0;
        cdc->in_cmd = true;
        cdc->is_touch = true;
        return;
    }

    if (cdc->is_touch) {
        // Touch cmd
        if (c == '}') {
            touch_cmd(cdc);
        } else if (cdc->len < sizeof(cdc->buf)) {
            cdc->buf[cdc->len] = c;
            cdc->len++;
        }
        return;
    }

    // LED cmd
    if (c == ESCAPE) {
        cdc->escape = true;
        return;
    }
    
    if (cdc->escape) {
        cdc->escape = false;
        c++;
    }

    if ((cdc->len == cdc->hdr.len + 3) && (cdc->checksum == c)) {
        led_cmd(cdc);
        return;
    }
    
    if (cdc->len < sizeof(cdc->buf)) {
        cdc->buf[cdc->len] = c;
        cdc->len++;
        cdc->checksum += c;
    }
}

static void update_itf(cdc_t *cdc)
{
    cdc->connected = tud_cdc_n_connected(cdc->interface);

    if (tud_cdc_n_available(cdc->interface)) {
        uint8_t buf[48];
        uint32_t count = tud_cdc_n_read(cdc->interface, buf, sizeof(buf));
        for (uint32_t i = 0; i < count; i++) {
            assemble_cmd(cdc, buf[i]);
        }
    }
}

static bool touch_release_pending;
static uint8_t touch_tx[9];
static unsigned touch_tx_offset;
static bool touch_tx_pending, touch_tx_release;
static int touch_tx_interface;
static uint64_t last_sent_time;
static io_touch_diagnostics_t tx_diagnostics;
static unsigned release_reason, tx_release_reason;
enum { RELEASE_USB_RESET = 1, RELEASE_SUSPEND = 2 };
const io_touch_diagnostics_t *io_touch_diagnostics(void) { return &tx_diagnostics; }

void io_usb_reset(void)
{
    tx_diagnostics.usb_resets++;
    tx_diagnostics.valid = false;
    release_reason = RELEASE_USB_RESET;
    touch_tx_pending = false;
    touch_tx_offset = 0;
    touch_release_pending = true;
    last_sent_time = 0;
    ctx.stat = false;
    ctx.touch_interface = 0;
    ctx.last_io_time = 0;
    cdc[0] = (cdc_t){.interface = 1};
    cdc[1] = (cdc_t){.interface = 2};
}

void io_flush_touch(void)
{
    if (!tud_ready()) {
        /* USB reset discards the transport FIFO; never resume half a frame. */
        tx_diagnostics.valid = false;
        release_reason = RELEASE_USB_RESET;
        touch_tx_pending = false;
        touch_tx_offset = 0;
        touch_release_pending = true;
        return;
    }
    uint64_t now = time_us_64();
    if (!touch_tx_pending) {
        if (!ctx.touch_interface) return;
        if (!touch_release_pending && (!ctx.stat || cfg_is_active() || now - last_sent_time < 1000)) return;
        if (tud_cdc_n_write_available(ctx.touch_interface) < sizeof(touch_tx)) return;
        touch_tx[0] = '(';
        touch_tx[8] = ')';
        uint64_t touch = touch_release_pending ? 0 : touch_touchmap();
        for (unsigned i = 1; i < 8; i++) {
            touch_tx[i] = touch & 0x1f;
            touch >>= 5;
        }
        touch_tx_release = touch_release_pending;
        tx_release_reason = release_reason;
        touch_tx_interface = ctx.touch_interface;
        touch_tx_offset = 0;
        touch_tx_pending = true;
    }
    unsigned remaining = sizeof(touch_tx) - touch_tx_offset;
    if (tud_cdc_n_write_available(touch_tx_interface) < remaining) return;
    unsigned written = tud_cdc_n_write(touch_tx_interface, touch_tx + touch_tx_offset, remaining);
    if (written > remaining) written = remaining;
    touch_tx_offset += written;
    tud_cdc_n_write_flush(touch_tx_interface);
    if (touch_tx_offset == sizeof(touch_tx)) {
        /* Count only complete packets accepted by TinyUSB, not attempted writes. */
        uint64_t accepted = 0;
        for (unsigned i = 0; i < 7; i++) accepted |= (uint64_t)(touch_tx[i + 1] & 0x1f) << (5 * i);
        input_history_update(&tx_diagnostics.history, accepted);
        tx_diagnostics.packets++;
        tx_diagnostics.valid = true;
        tx_diagnostics.last_packet_us = now;
        tx_diagnostics.last_interface = touch_tx_interface;
        if (touch_tx_release) {
            if (tx_release_reason == RELEASE_SUSPEND) tx_diagnostics.suspend_neutral_packets++;
            if (tx_release_reason == RELEASE_USB_RESET) tx_diagnostics.reset_neutral_packets++;
        }
        last_sent_time = now;
        touch_tx_pending = false;
        if (touch_tx_release) touch_release_pending = false;
    }
}

void io_update()
{
    /* Finish an unexpected short write before any response can share its stream. */
    if (touch_tx_pending) {
        io_flush_touch();
        if (touch_tx_pending) return;
    }
    update_itf(cdc);
    update_itf(cdc + 1);
}

void io_suspend()
{
    tx_diagnostics.suspend_requests++;
    release_reason = RELEASE_SUSPEND;
    touch_release_pending = true;
}

bool io_is_active()
{
    if (ctx.last_io_time == 0) {
        return false;
    }

    return time_us_64() < ctx.last_io_time + IO_TIMEOUT_SEC * 1000000;
}

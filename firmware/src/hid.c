#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "board_defs.h"

#include "tusb.h"
#include "usb_descriptors.h"
#include "button.h"
#include "rgb.h"
#include "config.h"
#include "hid.h"
#include "cfg_protocol.h"
#include "pico/time.h"

struct __attribute__((packed)) {
    uint16_t adcs[8];
    uint16_t spinners[4];
    uint16_t chutes[2];
    uint16_t buttons[2];
    uint8_t system_status;
    uint8_t usb_status;
    uint8_t padding[29];
} hid_io4, sent_hid_io4;

struct __attribute__((packed)) {
    uint8_t modifier;
    uint8_t keymap[15];
} hid_nkro, sent_hid_nkro;

static uint16_t native_to_io4(uint16_t button)
{
    static const int target_pos[] = { 2, 3, 0, 15, 14, 13, 12, 11, 9, 6, 1 };
    uint16_t io4btn = 0;
    for (int i = 0; i < 8; i++) {
        bool pressed = button & (1 << i);
        io4btn |= pressed ? 0 : (1 << target_pos[i]);
    }
    for (int i = 8; i < 11; i++) {
        bool pressed = button & (1 << i);
        io4btn |= pressed ? (1 << target_pos[i]) : 0;
    }
    return io4btn;
}

const char keymap_p1[] = BUTTON_NKRO_MAP_P1;
const char keymap_p2[] = BUTTON_NKRO_MAP_P2;
static bool release_pending[2];
static bool was_ready, was_suspended;
static uint32_t coin_origin;
static uint64_t next_periodic_report;

static void gen_nkro_report(uint16_t buttons)
{
    const char *keymap = (mai_cfg->hid.nkro == 2) ? keymap_p2 : keymap_p1;
    memset(&hid_nkro, 0, sizeof(hid_nkro));
    for (int i = 0; i < button_num(); i++) {
        uint8_t code = keymap[i];
        if ((buttons & (1u << i)) && code / 8 < sizeof(hid_nkro.keymap)) {
            hid_nkro.keymap[code / 8] |= 1u << (code % 8);
        }
    }
}

void hid_usb_reset(void)
{
    was_ready = false;
}

void hid_update()
{
    bool ready = tud_ready();
    bool suspended = cfg_is_active();
    if ((ready && !was_ready) || (!suspended && was_suspended)) {
        button_events_resync(0);
        button_events_resync(1);
    }
    was_ready = ready;
    was_suspended = suspended;
    if (!ready) return;

    for (unsigned itf = 0; itf < 2; itf++) {
        bool enabled = itf == 0 ? mai_cfg->hid.io4 : mai_cfg->hid.nkro;
        if (!enabled || !tud_hid_n_ready(itf)) continue;
        bool release = release_pending[itf];
        if (!release && (suspended || (itf == 1 && mai_runtime.key_stuck))) continue;

        button_event_t event;
        bool queued = !release && button_event_peek(itf, &event);
        if (itf == 0) {
            if (release || queued) {
                hid_io4.buttons[0] = native_to_io4(release ? 0 : event.buttons);
                hid_io4.buttons[1] = native_to_io4(0);
                if (queued) {
                    int32_t coins = (int32_t)(event.coins - coin_origin);
                    hid_io4.chutes[0] = coins > 0 ? (uint16_t)((uint32_t)coins << 8) : 0;
                }
            }
            uint64_t now = time_us_64();
            if (!release && !queued && now < next_periodic_report &&
                memcmp(&hid_io4, &sent_hid_io4, sizeof(hid_io4)) == 0) continue;
            if (!tud_hid_n_report(0, REPORT_ID_JOYSTICK, &hid_io4, sizeof(hid_io4))) continue;
            sent_hid_io4 = hid_io4;
            next_periodic_report = now + 4000;
        } else {
            if (!release && !queued) continue;
            gen_nkro_report(release ? 0 : event.buttons);
            if (!tud_hid_n_report(1, 0, &hid_nkro, sizeof(hid_nkro))) continue;
            sent_hid_nkro = hid_nkro;
        }
        if (release) release_pending[itf] = false;
        else if (queued) button_event_ack(itf, event.sequence);
    }
}

void hid_release_all()
{
    release_pending[0] = release_pending[1] = true;
}

typedef struct __attribute__((packed)) {
    uint8_t report_id;
    uint8_t cmd;
    union {
        struct {
            uint8_t led;
            uint8_t gap;
            uint8_t r1;
            uint8_t r2;
            uint8_t g1;
            uint8_t g2;
            uint8_t b1;
            uint8_t b2;
        };
        uint32_t outbits;
        uint8_t payload[62];
    };
} hid_output_t;

void hid_proc(const uint8_t *buffer, uint8_t len)
{
    if (!buffer || len < 2) return;
    const hid_output_t *data = (const hid_output_t *)buffer;
    if (data->report_id == REPORT_ID_OUTPUT) {
        switch (data->cmd) {
            case 0x01: // Set Timeout
            case 0x02: // Set Sampling Count
                hid_io4.system_status = 0x30;
                break;
            case 0x03: // Clear Board Status
                coin_origin = button_coin_count();
                hid_io4.chutes[0] = 0;
                hid_io4.chutes[1] = 0;
                hid_io4.system_status = 0x00;
                break;
            case 0x04: // General Output
                // Bit 2: QR Reader P2,
                // Bit 5: QR Reader P1,
                // Bit 6: Coin Blocker,
                // Bit 14: Camera RECORD,
                // Bit 15: Camera Ring
                break;
            case 0x41: // RGB Light
                if (len < 10 || data->led != 0xfc) {
                    break;
                }
                uint32_t p1_banner = rgb32(data->r1, data->g1, data->b1, false);
                rgb_set_banner(p1_banner);
                // Note: p2_banner uses data->r2, data->g2, data->b2
                break;
            default:
                printf("USB unknown cmd: %d\n", data->cmd);
                break;
        }
    }
}

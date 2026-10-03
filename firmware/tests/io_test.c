#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/io.c"

mai_runtime_t mai_runtime;
static uint64_t now_us = 1000, touch_value;
static bool mounted = true, active;
static unsigned available = 256, write_limit = 256;
static uint8_t output[8192];
static unsigned output_size, reads;
uint64_t time_us_64(void) { return now_us; }
bool cfg_is_active(void) { return active; }
uint64_t touch_touchmap(void) { return touch_value; }
bool tud_ready(void) { return mounted; }
bool tud_cdc_n_connected(uint8_t itf) { (void)itf; return mounted; }
uint32_t tud_cdc_n_available(uint8_t itf) { (void)itf; return 0; }
uint32_t tud_cdc_n_read(uint8_t itf, void *data, uint32_t size) { (void)itf; (void)data; (void)size; reads++; return 0; }
uint32_t tud_cdc_n_write_available(uint8_t itf) { (void)itf; return available; }
uint32_t tud_cdc_n_write(uint8_t itf, const void *data, uint32_t size)
{
    assert(itf == 1 || itf == 2);
    if (size > available) size = available;
    if (size > write_limit) size = write_limit;
    assert(output_size + size <= sizeof(output));
    memcpy(output + output_size, data, size);
    output_size += size;
    available -= size;
    return size;
}
uint32_t tud_cdc_n_write_char(uint8_t itf, char c) { return tud_cdc_n_write(itf, &c, 1); }
uint32_t tud_cdc_n_write_flush(uint8_t itf) { (void)itf; return 0; }
uint32_t rgb32(uint32_t r, uint32_t g, uint32_t b, bool gamma) { (void)gamma; return r << 16 | g << 8 | b; }
uint32_t gray32(uint32_t c, bool gamma) { (void)gamma; return c; }
void rgb_set_button(unsigned i, uint32_t color, uint8_t speed) { (void)i; (void)color; (void)speed; }
void rgb_set_cab(unsigned i, uint32_t color) { (void)i; (void)color; }
static uint64_t report(unsigned offset)
{
    assert(output_size >= offset + 9 && output[offset] == '(' && output[offset + 8] == ')');
    uint64_t value = 0;
    for (unsigned i = 0; i < 7; i++) value |= (uint64_t)output[offset + i + 1] << (5 * i);
    return value;
}
int main(void)
{
    ctx.touch_interface = 1; ctx.stat = true;
    touch_value = UINT64_C(0x200000001);
    available = 8; io_flush_touch();
    assert(output_size == 0 && last_sent_time == 0 && !touch_tx_pending);
    assert(io_touch_diagnostics()->packets == 0 && !io_touch_diagnostics()->valid);
    available = 9; io_flush_touch();
    assert(output_size == 9 && report(0) == touch_value && last_sent_time == now_us);
    assert(io_touch_diagnostics()->history.presses[0] == 1 && io_touch_diagnostics()->history.presses[33] == 1);
    assert(io_touch_diagnostics()->history.state == touch_value);
    available = 256; io_flush_touch(); assert(output_size == 9);

    /* Insufficient capacity never queues stale held data; next report uses latest scan. */
    now_us += 1000; available = 0; io_flush_touch();
    touch_value = 0; available = 9; io_flush_touch();
    assert(report(9) == 0);
    assert(io_touch_diagnostics()->history.releases[0] == 1 && io_touch_diagnostics()->packets == 2);

    /* Unexpected short writes resume the same packet before another can begin. */
    now_us += 1000; touch_value = 0x1234; available = 256; write_limit = 3;
    io_flush_touch(); assert(output_size == 21 && touch_tx_pending && touch_tx_offset == 3);
    assert(last_sent_time == now_us - 1000);
    assert(io_touch_diagnostics()->packets == 2 && io_touch_diagnostics()->history.state == 0);
    touch_value = 0; write_limit = 0; io_update();
    assert(touch_tx_pending && output_size == 21 && reads == 0);
    write_limit = 256; io_flush_touch();
    assert(!touch_tx_pending && output_size == 27 && report(18) == 0x1234);
    assert(io_touch_diagnostics()->packets == 3 && io_touch_diagnostics()->history.state == 0x1234);

    /* Configuration pause must retry release even when regular game I/O is stopped. */
    available = 0; io_suspend(); active = true; io_flush_touch();
    assert(touch_release_pending);
    assert(io_touch_diagnostics()->suspend_requests == 1 && io_touch_diagnostics()->suspend_neutral_packets == 0);
    available = 256; io_flush_touch();
    assert(!touch_release_pending && report(27) == 0);
    assert(io_touch_diagnostics()->suspend_neutral_packets == 1);
    now_us += 10000; io_flush_touch(); assert(output_size == 36);
    active = false; touch_value = 7; io_flush_touch(); assert(report(36) == 7);

    /* Suspend while a previous report is partial: finish it, then deliver zero. */
    now_us += 1000; write_limit = 2; io_flush_touch();
    io_suspend(); active = true;
    write_limit = 256; available = 256; io_flush_touch(); io_flush_touch();
    assert(report(45) == 7 && report(54) == 0 && !touch_release_pending);

    /* USB reset cancels the old prefix, then sends a full neutral frame. */
    active = false; now_us += 1000; write_limit = 2; io_flush_touch(); assert(touch_tx_pending);
    mounted = false; io_flush_touch();
    output_size = 0; available = 256; write_limit = 256; mounted = true;
    io_flush_touch(); assert(output_size == 9 && report(0) == 0);
    io_usb_reset();
    assert(!io_touch_diagnostics()->valid && io_touch_diagnostics()->usb_resets == 1);
    unsigned size_before = output_size;
    io_flush_touch(); assert(output_size == size_before && !ctx.stat && ctx.touch_interface == 0);
    const char start[] = "{L0A0}";
    for (unsigned i = 0; i < sizeof(start) - 1; i++) assemble_cmd(cdc, start[i]);
    assert(ctx.stat && ctx.touch_interface == 1);
    io_flush_touch(); assert(report(size_before) == 0);
    assert(io_touch_diagnostics()->reset_neutral_packets == 2);
    /* Repeated complete held reports must not count as repeated presses. */
    now_us += 1000; available = 256; touch_value = 1; io_flush_touch();
    uint32_t press_count = io_touch_diagnostics()->history.presses[0];
    uint32_t release_count = io_touch_diagnostics()->history.releases[0];
    for (unsigned i = 0; i < 50; i++) { now_us += 1000; available = 256; io_flush_touch(); }
    assert(io_touch_diagnostics()->history.presses[0] == press_count);
    assert(io_touch_diagnostics()->history.releases[0] == release_count);
    puts("io_test: ok");
    return 0;
}

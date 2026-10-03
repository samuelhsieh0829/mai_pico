#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/save.c"

uint8_t fake_flash[FLASH_SECTOR_SIZE];
static uint64_t fake_time;
static bool fail_program_once;

uint64_t time_us_64(void)
{
    return fake_time;
}

void sleep_ms(uint32_t milliseconds)
{
    fake_time += (uint64_t)milliseconds * 1000;
}

bool mutex_enter_timeout_us(mutex_t *mutex, uint64_t timeout_us)
{
    (void)mutex;
    (void)timeout_us;
    return true;
}

void mutex_exit(mutex_t *mutex)
{
    (void)mutex;
}

uint32_t save_and_disable_interrupts(void)
{
    return 0;
}

void restore_interrupts(uint32_t status)
{
    (void)status;
}

void flash_range_erase(uint32_t offset, size_t count)
{
    assert(offset == PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
    assert(count == FLASH_SECTOR_SIZE);
    memset(fake_flash, 0xff, sizeof(fake_flash));
}

void flash_range_program(uint32_t offset, const uint8_t *data, size_t count)
{
    assert(offset >= PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
    assert(count == FLASH_PAGE_SIZE);
    if (fail_program_once) {
        fail_program_once = false;
        return;
    }
    size_t local = offset - (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
    memcpy(fake_flash + local, data, count);
}

void pico_get_unique_board_id(pico_unique_board_id_t *id)
{
    memset(id, 0x5a, sizeof(*id));
}

static int loaded_calls;
static void after_load(void)
{
    loaded_calls++;
}

int main(void)
{
    memset(fake_flash, 0xff, sizeof(fake_flash));
    uint32_t defaults = 0x11223344;
    uint32_t *value = save_alloc(sizeof(defaults), &defaults, after_load);
    mutex_t lock = {0};
    save_init(0xa5a55a5a, &lock);
    assert(loaded_calls == 1);
    assert(*value == defaults);

    *value = 0x55667788;
    assert(save_request(true));
    assert(data_page == 0);
    assert(memcmp(&old_data, &new_data, sizeof(old_data)) == 0);

    *value = 0xabcdef01;
    fail_program_once = true;
    assert(!save_request(true));
    assert(requesting_save);
    fake_time += SAVE_TIMEOUT_US + 1;
    save_loop();
    assert(!requesting_save);
    assert(*value == 0xabcdef01);
    assert(data_page == 1);
    puts("save_test: ok");
    return 0;
}

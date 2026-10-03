static i2c_inst_t fake_i2c;
i2c_inst_t *i2c1 = &fake_i2c;
static uint8_t registers[128][256];
static uint8_t cursor[128];
static int fail_addr = -1;
static bool fail_read;
static int fail_write_reg = -1;
static int corrupt_read_reg = -1;
static bool short_read;
static unsigned bus_operations;
static uint64_t now_us;

uint64_t time_us_64(void)
{
    return ++now_us;
}

void sleep_ms(uint32_t ms)
{
    now_us += ms * 1000;
}

int i2c_write_blocking_until(i2c_inst_t *i2c, uint8_t addr,
                             const uint8_t *src, size_t len, bool nostop,
                             uint64_t until)
{
    (void)i2c;
    (void)nostop;
    (void)until;
    bus_operations++;
    if (addr == fail_addr || (len == 2 && src[0] == fail_write_reg)) {
        return -1;
    }
    if (len == 1) {
        cursor[addr] = src[0];
    } else if (len == 2) {
        if (src[0] == MPR121_SOFT_RESET_REG && src[1] == 0x63) {
            memset(registers[addr], 0, sizeof(registers[addr]));
            registers[addr][MPR121_FILTER_CONFIG_REG] = 0x24;
        } else if (src[0] == 0x5e || (src[0] >= 0x73 && src[0] <= 0x7a) ||
                   !(registers[addr][0x5e] & 0x3f)) {
            /* Genuine parts ignore configuration writes while running. */
            registers[addr][src[0]] = src[1];
        }
    }
    return (int)len;
}

int i2c_read_blocking_until(i2c_inst_t *i2c, uint8_t addr,
                            uint8_t *dst, size_t len, bool nostop,
                            uint64_t until)
{
    (void)i2c;
    (void)nostop;
    (void)until;
    bus_operations++;
    if (addr == fail_addr || fail_read) {
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        dst[i] = registers[addr][cursor[addr] + i];
    }
    if (cursor[addr] == corrupt_read_reg && len) dst[0] ^= 1;
    if (short_read && len) return (int)len - 1;
    return (int)len;
}


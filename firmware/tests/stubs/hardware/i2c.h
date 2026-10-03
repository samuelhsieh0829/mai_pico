#ifndef TEST_HARDWARE_I2C_H
#define TEST_HARDWARE_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { int unused; } i2c_inst_t;
extern i2c_inst_t *i2c1;

uint64_t time_us_64(void);
void i2c_deinit(i2c_inst_t *i2c);
unsigned i2c_init(i2c_inst_t *i2c, unsigned baudrate);
int i2c_write_blocking_until(i2c_inst_t *i2c, uint8_t addr,
                             const uint8_t *src, size_t len, bool nostop,
                             uint64_t until);
int i2c_read_blocking_until(i2c_inst_t *i2c, uint8_t addr,
                            uint8_t *dst, size_t len, bool nostop,
                            uint64_t until);

#endif

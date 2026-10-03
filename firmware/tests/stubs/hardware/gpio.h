#ifndef TEST_HARDWARE_GPIO_H
#define TEST_HARDWARE_GPIO_H

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

#define GPIO_FUNC_SIO 5
#define GPIO_IN false
#define GPIO_OUT true
#define GPIO_FUNC_I2C 3
void gpio_put(uint gpio, bool value);

void gpio_init(uint gpio);
void gpio_set_function(uint gpio, int function);
void gpio_set_dir(uint gpio, bool out);
void gpio_pull_up(uint gpio);
void gpio_pull_down(uint gpio);
bool gpio_get(uint gpio);

#endif

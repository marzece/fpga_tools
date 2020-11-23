#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "gpio.h"

uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);


uint32_t write_gpio_0_command(uint32_t *args) {
    return write_addr(GPIO_0_BASE, args[0], args[1]);
}

uint32_t read_gpio_0_command(uint32_t *args) {
    return double_read_addr(GPIO_0_BASE, args[0]);
}

uint32_t write_gpio_1_command(uint32_t *args) {
    return write_addr(GPIO_1_BASE, args[0], args[1]);
}

uint32_t read_gpio_1_command(uint32_t *args) {
    return double_read_addr(GPIO_1_BASE, args[0]);
}

uint32_t write_gpio_2_command(uint32_t *args) {
    return write_addr(GPIO_2_BASE, args[0], args[1]);
}

uint32_t read_gpio_2_command(uint32_t *args) {
    return double_read_addr(GPIO_2_BASE, args[0]);
}

uint32_t set_adc_power_command(uint32_t *args) {
    uint32_t current_val = double_read_addr(POWER_UP_GPIO_BASE, 0x0);

    // ADC power is bit 0
    if(args[0]) {
        current_val = current_val | 0b01;
    }
    else {
        current_val = current_val & 0b10;
    }
    write_addr(POWER_UP_GPIO_BASE, 0x0, current_val);
    return 0;
}

uint32_t set_preamp_power_command(uint32_t *args) {
    uint32_t current_val = double_read_addr(POWER_UP_GPIO_BASE, 0x0);

    // pre-amp power is bit 1
    if(args[0]) {
        current_val = current_val | 0b10;
    }
    else {
        current_val = current_val & 0b01;
    }
    write_addr(POWER_UP_GPIO_BASE, 0x0, current_val);
    return 0;
}

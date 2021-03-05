#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "gpio.h"

uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);

AXI_GPIO* new_gpio(const char* name, uint32_t axi_addr) {
    AXI_GPIO* ret = malloc(sizeof(AXI_GPIO));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_gpio_value(AXI_GPIO *gpio, int port_number) {
    uint32_t port_offset = port_number == 0 ? 0x0 : 0x8;
    return double_read_addr(gpio->axi_addr, port_offset);
}

uint32_t write_gpio_value(AXI_GPIO* gpio, int port_number, uint32_t data) {
    uint32_t port_offset = port_number == 0 ? 0x0 : 0x8;
    return write_addr(gpio->axi_addr, port_offset, data);
}

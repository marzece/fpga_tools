#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "gpio.h"

int read_addr(uint32_t, uint32_t, uint32_t*);
int double_read_addr(uint32_t, uint32_t, uint32_t*);
int write_addr(uint32_t, uint32_t, uint32_t);

AXI_GPIO* new_gpio(const char* name, uint32_t axi_addr) {
    AXI_GPIO* ret = malloc(sizeof(AXI_GPIO));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_gpio_value(AXI_GPIO *gpio, int input_port, int port_number) {
    uint32_t port_offset = port_number*4;
    uint32_t ret;

    if(input_port) {
        port_offset |= 1<<11;
    }

    if(double_read_addr(gpio->axi_addr, port_offset, &ret)) {
        // TODO!
         printf("ERROR occurred while doing GPIO read\n");
        return -1;
    }
    return ret;
}

uint32_t write_gpio_value(AXI_GPIO* gpio, int port_number, uint32_t data) {
    uint32_t port_offset = port_number*4;
    return write_addr(gpio->axi_addr, port_offset, data);
}

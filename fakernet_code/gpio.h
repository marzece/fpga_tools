#ifndef __GPIO__
#define __GPIO__
#include <inttypes.h>
#include <stdlib.h>

typedef struct AXI_GPIO {
    const char* name;
    uint32_t axi_addr;
} AXI_GPIO;

AXI_GPIO* new_gpio(const char* name, uint32_t axi_addr);
uint32_t read_gpio_value(AXI_GPIO* gpio, int output_port, int port_number);
uint32_t write_gpio_value(AXI_GPIO* gpio, int port_number, uint32_t data);
#endif

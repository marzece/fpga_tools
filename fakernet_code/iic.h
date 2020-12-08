#ifndef __IIC__
#define __IIC__
#include <inttypes.h>
#include <stdlib.h>
#include "fnet_client.h"

typedef struct AXI_IIC {
    const char* name;
    uint32_t axi_addr;
    int rw_size; // Number of bytes that ought to be read/write at the same time
} AXI_IIC;

AXI_IIC* new_iic(const char* name, uint32_t axi_addr, int rw_size);
uint32_t iic_read(AXI_IIC* iic, uint32_t addr);
int iic_write(AXI_IIC* iic, uint32_t addr, uint32_t data);
uint32_t read_iic_bus(AXI_IIC* iic, uint8_t iic_addr);
uint32_t write_iic_bus(AXI_IIC* iic, uint8_t iic_addr, uint8_t iic_value);
uint32_t read_iic_bus_with_reg(AXI_IIC* iic, uint8_t iic_addr, uint8_t reg_addr);
uint32_t write_iic_bus_with_reg(AXI_IIC* iic, uint8_t iic_addr, uint8_t reg_addr, uint32_t reg_value);
#endif

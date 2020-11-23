#include <inttypes.h>
#include <stdlib.h>
#include "fnet_client.h"

#define IIC_BASE 0x10000
#define IIC_CR_OFFSET  0x100
#define IIC_PIRQ_OFFSET  0x120
#define IIC_TX_FIFO_OFFSET 0x108
#define IIC_RX_FIFO_OFFSET 0x10C
#define IIC_SR_OFFSET  0x104

int iic_write(uint32_t addr, uint32_t data);
uint32_t iic_read(uint32_t addr);
uint32_t double_iic_read(uint32_t addr);
uint32_t read_iic_block_command(uint32_t* args);
uint32_t write_iic_block_command(uint32_t* args);
uint32_t read_iic_bus_command(uint32_t* args);
uint32_t write_iic_bus_command(uint32_t* args);
uint32_t read_iic_bus_with_reg_command(uint32_t* args);
uint32_t write_iic_bus_with_reg_command(uint32_t* args);

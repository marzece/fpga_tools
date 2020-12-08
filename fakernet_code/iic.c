#include "iic.h"

#define IIC_CR_OFFSET  0x100
#define IIC_PIRQ_OFFSET  0x120
#define IIC_TX_FIFO_OFFSET 0x108
#define IIC_RX_FIFO_OFFSET 0x10C
#define IIC_SR_OFFSET  0x104

extern uint32_t read_addr(uint32_t, uint32_t);
extern uint32_t double_read_addr(uint32_t, uint32_t);
extern int write_addr(uint32_t, uint32_t, uint32_t);

AXI_IIC* new_iic(const char* name, uint32_t axi_addr, int rw_size) {
    AXI_IIC* ret = malloc(sizeof(AXI_IIC));
    ret->name=name;
    ret->axi_addr = axi_addr;
    ret->rw_size = rw_size;
    return ret;
}

int iic_write(AXI_IIC* iic, uint32_t addr, uint32_t data) {
    return write_addr(iic->axi_addr, addr, data);
}


uint32_t iic_read(AXI_IIC* iic, uint32_t addr) {
    return double_read_addr(iic->axi_addr, addr);
}


// This is always a single byte read
uint32_t read_iic_bus(AXI_IIC* iic, uint8_t iic_addr) {

    // May as well set the RX_FIFO_PIRQ to max (0xF) (PIRQ = Programmable Interrupt btw)
    // If the Rx FIFO reaches the PIRQ value in interrupt (if enabled) is emitted
    if(iic_write(iic, IIC_PIRQ_OFFSET, 0xF)) {
        printf("Error with first write to IIC PIRQ reg\n");
        return 0;
    }


    if(iic_write(iic, IIC_CR_OFFSET, 0x2)) {
        printf("ERROR with first write to IIC CR\n");
        return 0;

    }
    if(iic_write(iic, IIC_CR_OFFSET, 0x9)) {
        printf("ERROR with second write to IIC CR\n");
        return 0;
    }

    iic_addr = iic_addr & 0xFF;
    uint32_t write_value = iic_addr | 1<<8 | 1<<0; // Set bit8 to send a "start" IIC, bit 0 for a READ
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, write_value)) {
        printf("ERROR with first write to IIC TX FIFO\n");
        return 0;
    }

    // TODO include rw_size here
    write_value = 1<<9 | 1;
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, write_value)) {
        printf("ERROR with second write to TX FIFO\n");
        return 0;

    }
    return iic_read(iic, IIC_RX_FIFO_OFFSET);
}

// This is always a single byte write
uint32_t write_iic_bus(AXI_IIC* iic, uint8_t iic_addr, uint8_t iic_value) {
    int err_loc = 0;
    if(iic_write(iic, IIC_PIRQ_OFFSET, 0xF)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    if(iic_write(iic, IIC_CR_OFFSET, 0x2)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    if(iic_write(iic, IIC_CR_OFFSET, 0x9)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    iic_addr = iic_addr & 0xFE; // Make sure bit 0 is unset, indicates a write
    uint32_t write_value = iic_addr | 1<<8; // Set bit8 to send a "start" IIC,
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, write_value)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    write_value = 1<<9 | iic_value;
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, write_value)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }
    return 0;
}

uint32_t read_iic_bus_with_reg(AXI_IIC* iic, uint8_t iic_addr, uint8_t reg_addr) {
    uint8_t err_loc = 0;
    // May as well set the RX_FIFO_PIRQ to max (0xF) (PIRQ = Programmable Interrupt btw)
    // If the Rx FIFO reaches the PIRQ value in interrupt (if enabled) is emitted

    if(iic->rw_size > 4) {
        printf("Cannot do IIC transaction for more than 4-bytes, %i-bytes was requested\n", iic->rw_size);
        return 0;
    }


    if(iic_write(iic, IIC_PIRQ_OFFSET, 0xF)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

    // The goal here is to read from register 0x0 on the ADC sensor
    // The sensor is I2C address 0xCE
    // First set the correct bits in the control register.
    // I believe the only necessary one is the IIC Enable bit (perhaps should do a TxReset though)
    if(iic_write(iic, IIC_CR_OFFSET, 0x2)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

    if(iic_write(iic, IIC_CR_OFFSET, 0x0)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

    if(iic_write(iic, IIC_TX_FIFO_OFFSET, (iic_addr & 0xFE) | 1<<8)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, reg_addr & 0xFF)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }

    err_loc++;
    // Repeated start with read bit set
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, iic_addr | 1<<8 | 1<<0)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, (1<<9) | iic->rw_size)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;
    if(iic_write(iic, IIC_CR_OFFSET, 0x1)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

    int i;
    uint32_t val=0;
    for(i=0; i<iic->rw_size; i++) {
        //while(double_iic_read(IIC_SR_OFFSET) & 0x40) {
        //   usleep(500);
        //}
        val |= iic_read(iic, IIC_RX_FIFO_OFFSET) << ((iic->rw_size-1)-i)*8;
    }
    return val;
}
uint32_t write_iic_bus_with_reg(AXI_IIC* iic, uint8_t iic_addr, uint8_t reg_addr, uint32_t reg_value) {
    uint8_t err_loc = 0;

    // May as well set the RX_FIFO_PIRQ to max (0xF) (PIRQ = Programmable Interrupt btw)
    // If the Rx FIFO reaches the PIRQ value in interrupt (if enabled) is emitted
    if(iic_write(iic, IIC_PIRQ_OFFSET, 0xF)) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    // The goal here is to read from register 0x0 on the ADC sensor
    // The sensor is I2C address 0xCE
    // First set the correct bits in the control register.
    // I believe the only necessary one is the IIC Enable bit (perhaps should do a TxReset though)
    if(iic_write(iic, IIC_CR_OFFSET, 0x2)) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;
    }
    err_loc++;
    if(iic_write(iic, IIC_CR_OFFSET, 0x0)) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    if(iic_write(iic, IIC_TX_FIFO_OFFSET, (iic_addr & 0xFE) | 1<<8) ) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;

    }
    err_loc++;
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, reg_addr) ) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;
    }
    err_loc++;
    // Uncomment for two bytes write
    // TODO!!!! Make this use rw_size instead of being hardcoded
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, (reg_value >> 8) & 0xFF) ) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;
    }
    err_loc++;
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, (reg_value & 0xFF) | 1 << 9) ) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    if(iic_write(iic, IIC_CR_OFFSET, 0x1) ) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;
    }
    return 0;
}

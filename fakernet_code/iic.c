#include "iic.h"
extern uint32_t read_addr(uint32_t, uint32_t);
extern uint32_t double_read_addr(uint32_t, uint32_t);
extern int write_addr(uint32_t, uint32_t, uint32_t);

int iic_write(uint32_t addr, uint32_t data) {
    return write_addr(IIC_BASE, addr, data);
}

uint32_t iic_read(uint32_t addr) {
    return read_addr(IIC_BASE, addr);
}

uint32_t double_iic_read(uint32_t addr) {
    return double_read_addr(IIC_BASE, addr);
}

uint32_t read_iic_block_command(uint32_t* args) {
    uint32_t offset = args[0];
    return double_iic_read(offset);
}

uint32_t write_iic_block_command(uint32_t* args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    int err = iic_write(offset, data);
    return err;
}

uint32_t read_iic_bus_command(uint32_t* args) {
    // May as well set the RX_FIFO_PIRQ to max (0xF) (PIRQ = Programmable Interrupt btw)
    // If the Rx FIFO reaches the PIRQ value in interrupt (if enabled) is emitted
    uint32_t iic_addr = args[0];
    if(iic_write(IIC_PIRQ_OFFSET, 0xF)) {
        printf("Error with first write to IIC PIRQ reg\n");
        return 0;
    }


    if(iic_write(IIC_CR_OFFSET, 0x2)) {
        printf("ERROR with first write to IIC CR\n");
        return 0;

    }
    if(iic_write(IIC_CR_OFFSET, 0x9)) {
        printf("ERROR with second write to IIC CR\n");
        return 0;
    }

    iic_addr = iic_addr & 0xFF;
    uint32_t write_value = iic_addr | 1<<8 | 1<<0; // Set bit8 to send a "start" IIC, bit 0 for a READ
    if(iic_write(IIC_TX_FIFO_OFFSET, write_value)) {
        printf("ERROR with first write to IIC TX FIFO\n");
        return 0;
    }

    write_value = 1<<9 | 1;
    if(iic_write(IIC_TX_FIFO_OFFSET, write_value)) {
        printf("ERROR with second write to TX FIFO\n");
        return 0;

    }

    return double_iic_read(IIC_RX_FIFO_OFFSET);
}

uint32_t write_iic_bus_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t iic_value = args[1];
    uint8_t err_loc = 0;
    // May as well set the RX_FIFO_PIRQ to max (0xF) (PIRQ = Programmable Interrupt btw)
    // If the Rx FIFO reaches the PIRQ value in interrupt (if enabled) is emitted
    if(iic_write(IIC_PIRQ_OFFSET, 0xF)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    if(iic_write(IIC_CR_OFFSET, 0x2)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    if(iic_write(IIC_CR_OFFSET, 0x9)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    iic_addr = iic_addr & 0xFE; // Make sure bit 0 is unset, indicates a write
    uint32_t write_value = iic_addr | 1<<8; // Set bit8 to send a "start" IIC,
    if(iic_write(IIC_TX_FIFO_OFFSET, write_value)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    write_value = 1<<9 | iic_value;
    if(iic_write(IIC_TX_FIFO_OFFSET, write_value)) {
        printf("ERROR write_iic_bus_command: %i\n", err_loc);
        return -1;
    }

    return 0;
}

uint32_t read_iic_bus_with_reg_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t reg_addr = args[1];
    uint8_t err_loc = 0;
    // May as well set the RX_FIFO_PIRQ to max (0xF) (PIRQ = Programmable Interrupt btw)
    // If the Rx FIFO reaches the PIRQ value in interrupt (if enabled) is emitted
    if(iic_write(IIC_PIRQ_OFFSET, 0xF)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

    const int NBYTES = 1;
    // The goal here is to read from register 0x0 on the ADC sensor
    // The sensor is I2C address 0xCE
    // First set the correct bits in the control register.
    // I believe the only necessary one is the IIC Enable bit (perhaps should do a TxReset though)
    if(iic_write(IIC_CR_OFFSET, 0x2)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

    if(iic_write(IIC_CR_OFFSET, 0x0)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

    if(iic_write(IIC_TX_FIFO_OFFSET, (iic_addr & 0xFE) | 1<<8)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;
    if(iic_write(IIC_TX_FIFO_OFFSET, reg_addr & 0xFF)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }

    err_loc++;
    // Repeated start with read bit set
    if(iic_write(IIC_TX_FIFO_OFFSET, iic_addr | 1<<8 | 1<<0)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;
    if(iic_write(IIC_TX_FIFO_OFFSET, (1<<9) | NBYTES)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;
    if(iic_write(IIC_CR_OFFSET, 0x1)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

    int i;
    uint32_t val=0;
    for(i=0; i<NBYTES; i++) {
        //while(double_iic_read(IIC_SR_OFFSET) & 0x40) {
        //   usleep(500);
        //}
        val |= double_iic_read(IIC_RX_FIFO_OFFSET) << ((NBYTES-1)-i)*8;
    }
    return val;
}

uint32_t write_iic_bus_with_reg_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t reg_addr = args[1];
    uint32_t reg_value = args[2];
    uint8_t err_loc = 0;

    // May as well set the RX_FIFO_PIRQ to max (0xF) (PIRQ = Programmable Interrupt btw)
    // If the Rx FIFO reaches the PIRQ value in interrupt (if enabled) is emitted
    if(iic_write(IIC_PIRQ_OFFSET, 0xF)) {
        printf("ERROR write_iic_bus_with_reg_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;


    // The goal here is to read from register 0x0 on the ADC sensor
    // The sensor is I2C address 0xCE
    // First set the correct bits in the control register.
    // I believe the only necessary one is the IIC Enable bit (perhaps should do a TxReset though)
    if(iic_write(IIC_CR_OFFSET, 0x2)) {
        printf("ERROR write_iic_bus_with_reg_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;
    if(iic_write(IIC_CR_OFFSET, 0x0)) {
        printf("ERROR write_iic_bus_with_reg_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    if( iic_write(IIC_TX_FIFO_OFFSET, (iic_addr & 0xFE) | 1<<8) ) {
        printf("ERROR write_iic_bus_with_reg_command: %i\n", err_loc);
        return -1;

    }
    err_loc++;
    if( iic_write(IIC_TX_FIFO_OFFSET, reg_addr) ) {
        printf("ERROR write_iic_bus_with_reg_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;
    // Uncomment for two bytes write
    if( iic_write(IIC_TX_FIFO_OFFSET, (reg_value >> 8) & 0xFF) ) {
        printf("ERROR write_iic_bus_with_reg_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;
    if( iic_write(IIC_TX_FIFO_OFFSET, (reg_value & 0xFF) | 1 << 9) ) {
        printf("ERROR write_iic_bus_with_reg_command: %i\n", err_loc);
        return -1;
    }
    err_loc++;

    if( iic_write(IIC_CR_OFFSET, 0x1) ) {
        printf("ERROR write_iic_bus_with_reg_command: %i\n", err_loc);
        return -1;
    }
        return 0;
}

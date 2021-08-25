#include "iic.h"

#define IIC_CR_OFFSET  0x100
#define IIC_SR_OFFSET  0x104
#define IIC_TX_FIFO_OFFSET 0x108
#define IIC_RX_FIFO_OFFSET 0x10C
#define IIC_PIRQ_OFFSET  0x120
#define IIC_GPIO_OFFSET  0x124

extern uint32_t read_addr(uint32_t, uint32_t, uint32_t*);
extern uint32_t double_read_addr(uint32_t, uint32_t, uint32_t*);
extern int write_addr(uint32_t, uint32_t, uint32_t);

AXI_IIC* new_iic(const char* name, uint32_t axi_addr, int data_size, int reg_addr_size) {
    AXI_IIC* ret = malloc(sizeof(AXI_IIC));
    ret->name=name;
    ret->axi_addr = axi_addr;
    ret->data_size = data_size;
    ret->reg_addr_size = reg_addr_size;
    return ret;
}

int iic_write(AXI_IIC* iic, uint32_t addr, uint32_t data) {
    return write_addr(iic->axi_addr, addr, data);
}

uint32_t iic_read(AXI_IIC* iic, uint32_t addr) {
    uint32_t ret;
    if(double_read_addr(iic->axi_addr, addr, &ret)) {
        // TODO!!!
        return -1;
    }
    return ret;
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

uint32_t read_iic_gpio(AXI_IIC* iic) {
    return iic_read(iic, IIC_GPIO_OFFSET);
}

uint32_t write_iic_gpio(AXI_IIC* iic, uint32_t value) {
    return iic_write(iic, IIC_GPIO_OFFSET, value);
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

uint32_t read_iic_bus_with_reg(AXI_IIC* iic, uint8_t iic_addr, uint32_t reg_addr) {
    uint8_t err_loc = 0;
    int i;
    // May as well set the RX_FIFO_PIRQ to max (0xF) (PIRQ = Programmable Interrupt btw)
    // If the Rx FIFO reaches the PIRQ value in interrupt (if enabled) is emitted

    if(iic->data_size > 4) {
        printf("Cannot do IIC transaction with data more than 4-bytes, %i-bytes was requested\n", iic->data_size);
        return 0;
    }

    if(iic->reg_addr_size > 4) {
        printf("Cannot do IIC transaction with address more than 4-bytes, %i-bytes was requested\n", iic->data_size);
        return 0;
    }


    if(iic_write(iic, IIC_PIRQ_OFFSET, 0xF)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

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

    for(i=(iic->reg_addr_size-1); i>= 0; i--) {
        // Grab the relevant byte from the reg_address, top bytes first
        uint8_t addr_val = (reg_addr & (0xFF << (i*8))) >> (i*8);
        printf("%i %u\n", i,  addr_val);
        if(iic_write(iic, IIC_TX_FIFO_OFFSET, addr_val)) {
            printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
            return 0;
        }
    }

    err_loc++;
    // Repeated start with read bit set
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, iic_addr | 1<<8 | 1<<0)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;
    if(iic_write(iic, IIC_TX_FIFO_OFFSET, (1<<9) | iic->data_size)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;
    if(iic_write(iic, IIC_CR_OFFSET, 0x1)) {
        printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
        return 0;
    }
    err_loc++;

    uint32_t data_val=0;
    for(i=0; i<iic->data_size; i++) {
        usleep(500);
        data_val |= iic_read(iic, IIC_RX_FIFO_OFFSET) << ((iic->data_size-1)-i)*8;
    }
    return data_val;
}

uint32_t write_iic_bus_with_reg(AXI_IIC* iic, uint8_t iic_addr, uint32_t reg_addr, uint32_t reg_value) {
    uint8_t err_loc = 0;
    int i;

    // May as well set the RX_FIFO_PIRQ to max (0xF) (PIRQ = Programmable Interrupt btw)
    // If the Rx FIFO reaches the PIRQ value in interrupt (if enabled) is emitted
    if(iic_write(iic, IIC_PIRQ_OFFSET, 0xF)) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;
    }
    err_loc++;

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

    for(i=(iic->reg_addr_size-1); i>= 0; i--) {
        // Grab the relevant byte from the reg_address, top bytes first
        uint8_t addr_val = (reg_addr & (0xFF << (i*8))) >> (i*8);
        printf("Writing 0x%x\n", addr_val);
        if(iic_write(iic, IIC_TX_FIFO_OFFSET, addr_val)) {
            printf("ERROR read_iic_bus_with_reg_command: %i\n", err_loc);
            return 0;
        }
    }

    err_loc++;
    for(i=(iic->data_size-1); i>= 0; i--) {
        // Grab the relevant byte from the reg_value, top bytes first
       //uint32_t val = (reg_value & (0xFF << (i*8))) >> (i*8);
        uint32_t val = ((reg_value >> (i*8)) & 0xFF);

        // If this is the last data byte, then set the STOP bit
        if(i==0) {
            val |= 1 << 9;
        }

        printf("Writing 0x%x\n", val);
        if(iic_write(iic, IIC_TX_FIFO_OFFSET, val)) {
            printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
            return -1;
        }
    }

    err_loc++;
    if(iic_write(iic, IIC_CR_OFFSET, 0x1) ) {
        printf("ERROR write_iic_bus_with_reg: %i\n", err_loc);
        return -1;
    }
    return 0;
}

#include <inttypes.h>
#include <stdlib.h>
#include "dac_if.h"

uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);

static int spi_cr_initialized=0;
#define SPI_CR_INITIALIZATION_VALUE 0x86

int write_dac_if(uint32_t offset, uint32_t data) {
    if(!spi_cr_initialized) {
        write_addr(DAC_IF_BASE, SPICR_OFFSET, SPI_CR_INITIALIZATION_VALUE);
        spi_cr_initialized = 1;
    }
    return write_addr(DAC_IF_BASE, offset, data);
}

uint32_t read_dac_if_addr(uint32_t offset) {
    return double_read_addr(DAC_IF_BASE, offset);
}

uint32_t write_dac_if_command(uint32_t *args) {
    return write_dac_if(args[0], args[1]);
}

uint32_t read_dac_if_command(uint32_t *args) {
    return read_dac_if(args[0]);
}

uint32_t write_dac_spi_command(int adc_a, uint32_t* args) {
    // This just for ADC A TODO
    uint32_t command = args[0];
    uint32_t channels = args[1];
    uint32_t data = args[2];

    // First 4 bits are the "command"
    // Second 4 bits is the channel mask
    // Final 16 bits are the actual data
    command &= 0xF;
    channels &= 0xF;
    data &= 0xFFFF;

    uint32_t word1 = (command<<4) | channels;
    uint32_t word2 = data >> 8;
    uint32_t word3 = data & 0xFF;

    // Bit 0 is the select bit, bit 1 is the LDAC, bit 2 is reset
   uint32_t slave_select = 0x1; 

    write_dac_if(SPISSR_OFFSET, slave_select);
    write_dac_if(SPIDTR_OFFSET, word1);
    write_dac_if(SPIDTR_OFFSET, word2);
    write_dac_if(SPIDTR_OFFSET, word3);
    write_dac_if(SPISSR_OFFSET, 0x0);
    return 0;
}

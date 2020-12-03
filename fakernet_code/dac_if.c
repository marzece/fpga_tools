#include <inttypes.h>
#include <stdlib.h>
#include "dac_if.h"

uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);


int write_dac_if(uint32_t offset, uint32_t data) {
    return write_addr(DAC_IF_BASE, offset, data);
}

uint32_t read_dac_if(uint32_t offset) {
    return double_read_addr(DAC_IF_BASE, offset);
}

uint32_t write_dac_if_command(uint32_t *args) {
    return write_dac_if(args[0], args[1]);
}

uint32_t read_dac_if_command(uint32_t *args) {
    return read_dac_if(args[0]);
}

uint32_t write_dac_spi_command(uint32_t* args) {
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


    write_dac_if(SPICR_OFFSET, 0x0C);
    write_dac_if(SPISSR_OFFSET, 0x6);
    write_dac_if(SPIDTR_OFFSET, word1);
    write_dac_if(SPIDTR_OFFSET, word2);
    write_dac_if(SPIDTR_OFFSET, word3);
    write_dac_if(SPICR_OFFSET, 0x0E);
    write_dac_if(SPICR_OFFSET, 0x0C);
    return 0;
}

uint32_t toggle_dac_ldac_command(uint32_t* args) {
    (void)args; // Not used
    // This is a bit of hack. But basically I do an 8-bit SPI transaction, but
    // set the SSR reg to toggle it's 2nd bit and not any other bits
    // LDAC is the second bit of the SS (i.e. bit 1)

    write_dac_if(SPICR_OFFSET, 0x0C);
    write_dac_if(SPISSR_OFFSET, 0x5);
    write_dac_if(SPIDTR_OFFSET, 0);
    write_dac_if(SPICR_OFFSET, 0x0E);
    write_dac_if(SPICR_OFFSET, 0x0C);

    return 0;
}

uint32_t toggle_dac_reset_command(uint32_t* args) {
    (void)args; // Not used
    // This is a bit of hack. But basically I do an 8-bit SPI transaction, but
    // set the SSR reg to toggle it's third bit and not any other bits
    // RESET is the third bit of the SS (i.e. bit 2)

    write_dac_if(SPICR_OFFSET, 0x0C);
    write_dac_if(SPISSR_OFFSET, 0x3);
    write_dac_if(SPIDTR_OFFSET, 0);
    write_dac_if(SPICR_OFFSET, 0x0E);
    write_dac_if(SPICR_OFFSET, 0x0C);

    return 0;
}

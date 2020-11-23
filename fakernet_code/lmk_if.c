#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include "lmk_if.h"

uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);

static int spi_cr_initialized  = 0;
#define SPI_CR_INITIALIZATION_VALUE 0x86

int write_lmk_if(uint32_t offset, uint32_t data) {
    // TODO this way of initializing is really dumb, fixme
    if(!spi_cr_initialized) {
        //write_addr(LMK_IF_BASE, SPICR_OFFSET, SPI_CR_INITIALIZATION_VALUE);
        spi_cr_initialized = 1;
    }
    return write_addr(LMK_IF_BASE, offset, data);
}

uint32_t read_lmk_if(uint32_t offset) {
    return double_read_addr(LMK_IF_BASE, offset);
}

uint32_t write_lmk_if_command(uint32_t* args) {
    return write_lmk_if(args[0], args[1]);
}

uint32_t read_lmk_if_command(uint32_t* args) {
    return read_lmk_if(args[0]);
}

uint32_t write_lmk_spi_command(uint32_t* args) {
    uint32_t rw = args[0];
    uint32_t addr = args[1];
    uint32_t data = args[2];

    // RW is 3 bits and the bottom bits are always zero (pretty sure)
    // so RW only has two valid values.
    rw = rw ? 0x4 : 0x0;
    addr = addr & 0x1FFF;
    data = data & 0xFF;

    uint32_t word1 = (rw << 5) | (addr >> 8);
    uint32_t word2 = addr & 0xFF;
    uint32_t word3 = data;

    // Bit 0 of the SSR is the LMK select line, pull it low to start SPI data
    // transfer

    write_lmk_if(SPICR_OFFSET, 0x04);
    write_lmk_if(SPISSR_OFFSET, 0x0);
    write_lmk_if(SPIDTR_OFFSET, word1);
    write_lmk_if(SPIDTR_OFFSET, word2);
    write_lmk_if(SPIDTR_OFFSET, word3);
    write_lmk_if(SPICR_OFFSET, 0x06);
    //write_lmk_if(SPISSR_OFFSET, 0x3);
    write_lmk_if(SPICR_OFFSET, 0x04);
    return 0;
}

uint32_t lmk_spi_data_available_command(uint32_t* args) {
    return read_lmk_if(SPI_FIFO_OCC_OFFSET);
}
uint32_t lmk_spi_data_pop_command(uint32_t* args) {
    return read_lmk_if(SPIDRR_OFFSET);
}

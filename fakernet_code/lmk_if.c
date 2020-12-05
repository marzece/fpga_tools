#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include "lmk_if.h"
#include "axi_qspi.h"


static AXI_QSPI* lmk_axi_qspi = NULL;

static AXI_QSPI* get_spi_handle() {
    if(!lmk_axi_qspi) {
        lmk_axi_qspi = malloc(sizeof(AXI_QSPI));
        *lmk_axi_qspi = new_axi_qspi("lmk_if", LMK_IF_BASE);
        // Initialze CR values
        lmk_axi_qspi->spi_cr.master=1;
    }
    return lmk_axi_qspi;
}

// Will I ever actually need to clean up my memory? Probably not
void clean_up() {
    free(get_spi_handle());
    lmk_axi_qspi = NULL;
}

int write_lmk_if(uint32_t offset, uint32_t data) {
    return write_qspi_addr(get_spi_handle(), offset, data);
}

uint32_t read_lmk_if(uint32_t offset) {
    return read_qspi_addr(get_spi_handle(), offset);
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
    uint32_t word3 = data & 0xFF;

    uint8_t word_buf[3] = {word1, word2, word3};
    // Bit 0 of the SSR is the LMK select line, pull it low to start SPI data transfer
    uint32_t ssr = 0x0;
    return write_spi(lmk_axi_qspi, ssr, word_buf, 3);
}

uint32_t lmk_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_spi_handle());
}

uint32_t lmk_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_spi_handle());
}

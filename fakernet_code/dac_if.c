#include <inttypes.h>
#include <stdlib.h>
#include "dac_if.h"
#include "axi_qspi.h"

AXI_QSPI* new_dac_spi(const char* name, uint32_t axi_addr) {
    AXI_QSPI* dac_axi_qspi = new_axi_qspi(name, axi_addr);

    // Initialize CR values. The DAC uses falling edges so set the CPOL
    dac_axi_qspi->spi_cr.cpol = 1;
    dac_axi_qspi->spi_cr.master = 1;

    // Maybe I should do a "toggle reset" thing here?
    // For some reason the first spi I send after power up doesn't work.
    // I think it has something to do with switching to CPOL=1 or someting
    // like that...idk. But doing a dummy initial SPI transaction might be
    // a solution to that.
    return dac_axi_qspi;
}

int write_dac_if(AXI_QSPI* qspi, uint32_t offset, uint32_t data) {
    return write_qspi_addr(qspi, offset, data);
}

uint32_t read_dac_if(AXI_QSPI* qspi, uint32_t offset) {
    return read_qspi_addr(qspi, offset);
}

uint32_t write_dac_spi(AXI_QSPI* qspi, uint8_t command, uint8_t channels, uint16_t data) {
    uint32_t word1 = (command<<4) | channels;
    uint32_t word2 = data >> 8;
    uint32_t word3 = data & 0xFF;

    uint8_t word_buf[3] = {word1, word2, word3};
    uint32_t ssr = 0x6;
    return write_spi(qspi, ssr, word_buf, 3);
}

#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include "lmk_if.h"
#include "axi_qspi.h"

AXI_QSPI* new_lmk_spi(const char* name, uint32_t axi_addr) {
    AXI_QSPI *lmk_axi_qspi = new_axi_qspi(name, axi_addr);
    // Initialze CR values
    lmk_axi_qspi->spi_cr.master=1;
    return lmk_axi_qspi;
}

int write_lmk_if(AXI_QSPI* qspi, uint32_t offset, uint32_t data) {
    return write_qspi_addr(qspi, offset, data);
}

uint32_t read_lmk_if(AXI_QSPI* qspi, uint32_t offset) {
    return read_qspi_addr(qspi, offset);
}


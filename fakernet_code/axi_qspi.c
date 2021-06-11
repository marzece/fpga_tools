#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "axi_qspi.h"

#define SRR_OFFSET 0x40
#define SPICR_OFFSET 0x60
#define SPISR_OFFSET 0x64
#define SPI_DTR_OFFSET 0x68
#define SPI_DRR_OFFSET 0x6C
#define SPISSR_OFFSET 0x70
#define TR_OCC_OFFSET 0x74
#define RR_OCC_OFFSET 0x78
#define DGIER_OFFSET 0x1C
#define IPISR_OFFSET 0x20

#define SPI_CR_LSB_FIRST_LOC 9
#define SPI_CR_MASTER_INHIBIT_LOC 8
#define SPI_CR_MANUAL_SLAVE_SELECT_ENABLE_LOC 7
#define SPI_CR_RX_FIFO_RESET_LOC 6
#define SPI_CR_TX_FIFO_RESET_LOC 5
#define SPI_CR_CPHA_LOC 4
#define SPI_CR_CPOL_LOC 3
#define SPI_CR_MASTER_LOC 2
#define SPI_CR_SPI_ENABLE_LOC 1
#define SPI_CR_LOOPBACK_LOC 0

// These functions should be provided by the "main" program
uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);


AXI_QSPI* new_axi_qspi(const char* name, uint32_t axi_addr) {
    AXI_QSPI* ret = malloc(sizeof(AXI_QSPI));
    SPI_CR spi_cr;
    memset(&spi_cr, 0, sizeof(spi_cr));
    ret->name = name;
    ret->axi_addr = axi_addr;
    ret->spi_cr = spi_cr;
    return ret;
}

int write_qspi_addr(AXI_QSPI* qspi, uint32_t offset, uint32_t data) {
    return write_addr(qspi->axi_addr, offset, data);
}

uint32_t read_qspi_addr(AXI_QSPI* qspi, uint32_t offset) {
    return double_read_addr(qspi->axi_addr, offset);
}

uint32_t spi_cr_to_bits(struct SPI_CR spi_cr) {
    uint32_t out = 0;
    out |= spi_cr.lsb_first << SPI_CR_LSB_FIRST_LOC;
    out |= spi_cr.master_inhibit << SPI_CR_MASTER_INHIBIT_LOC;
    out |= spi_cr.manual_slave_select_enable  << SPI_CR_MANUAL_SLAVE_SELECT_ENABLE_LOC ;
    out |= spi_cr.rx_fifo_reset << SPI_CR_RX_FIFO_RESET_LOC;
    out |= spi_cr.tx_fifo_reset << SPI_CR_TX_FIFO_RESET_LOC;
    out |= spi_cr.cpha << SPI_CR_CPHA_LOC;
    out |= spi_cr.cpol << SPI_CR_CPOL_LOC;
    out |= spi_cr.master << SPI_CR_MASTER_LOC;
    out |= spi_cr.spi_enable << SPI_CR_SPI_ENABLE_LOC;
    out |= spi_cr.loop_back << SPI_CR_LOOPBACK_LOC;
    return out;
}

SPI_CR bits_to_spi_cr(uint32_t word) {
    SPI_CR out;
    out.lsb_first = word & (1 << SPI_CR_LSB_FIRST_LOC);
    out.master_inhibit = word & (1 << SPI_CR_MASTER_INHIBIT_LOC);
    out.manual_slave_select_enable  = word & (1 << SPI_CR_MANUAL_SLAVE_SELECT_ENABLE_LOC );
    out.rx_fifo_reset = word & (1 << SPI_CR_RX_FIFO_RESET_LOC);
    out.tx_fifo_reset = word & (1 << SPI_CR_TX_FIFO_RESET_LOC);
    out.cpha = word & (1 << SPI_CR_CPHA_LOC);
    out.cpol = word & (1 << SPI_CR_CPOL_LOC);
    out.master = word & (1 << SPI_CR_MASTER_LOC);
    out.spi_enable = word & (1 << SPI_CR_SPI_ENABLE_LOC);
    out.loop_back = word & (1 << SPI_CR_LOOPBACK_LOC);
    return out;
}

int write_spi(AXI_QSPI* qspi, uint8_t ssr, uint8_t* data, int nwords) {
    int i;
    qspi->spi_cr.spi_enable = 0;
    write_qspi_addr(qspi, SPICR_OFFSET, spi_cr_to_bits(qspi->spi_cr));
    write_qspi_addr(qspi, SPISSR_OFFSET, ssr);
    for(i=0; i<nwords; i++) {
        write_qspi_addr(qspi, SPI_DTR_OFFSET, data[i]);
    }
    qspi->spi_cr.spi_enable = 1;
    write_qspi_addr(qspi, SPICR_OFFSET, spi_cr_to_bits(qspi->spi_cr));
    qspi->spi_cr.spi_enable = 0;
    write_qspi_addr(qspi, SPICR_OFFSET, spi_cr_to_bits(qspi->spi_cr));
    return 0;
}

int spi_drr_pop(AXI_QSPI* qspi) {
    return read_qspi_addr(qspi, SPI_DRR_OFFSET);
}

int spi_drr_data_available(AXI_QSPI* qspi) {
    return read_qspi_addr(qspi, RR_OCC_OFFSET);
}

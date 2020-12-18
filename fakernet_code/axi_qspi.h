#ifndef __AXI_QSPI__
#define __AXI_QSPI__
#include <stdint.h>
#include <inttypes.h>
#include <string.h>


typedef struct SPI_CR {
    int lsb_first;
    int master_inhibit;
    int manual_slave_select_enable;
    int rx_fifo_reset;
    int tx_fifo_reset;
    int cpha;
    int cpol;
    int master;
    int spi_enable;
    int loop_back;
} SPI_CR;

typedef struct AXI_QSPI {
    const char* name;
    uint32_t axi_addr;
    SPI_CR spi_cr;
}AXI_QSPI;

AXI_QSPI* new_axi_qspi(const char* name, uint32_t axi_addr);
int write_qspi_addr(AXI_QSPI* qspi, uint32_t offset, uint32_t data);
uint32_t read_qspi_addr(AXI_QSPI* qspi, uint32_t offset);
uint32_t spi_cr_to_bits(struct SPI_CR spi_cr);
SPI_CR bits_to_spi_cr(uint32_t word);
int write_spi(AXI_QSPI* qspi, uint8_t ssr, uint8_t* data, int nwords);
int spi_drr_pop(AXI_QSPI* qspi);
int spi_drr_data_available(AXI_QSPI* qspi);
#endif

#include <inttypes.h>
#include <stdlib.h>
#define DAC_IF_BASE 0x2180

#define SPICR_OFFSET 0x60
#define SPIDTR_OFFSET 0x68
#define SPISSR_OFFSET 0x70
#define SPI_FIFO_OCC_OFFSET 0x78
#define SPIDRR_OFFSET 0x6C


int write_dac_if(uint32_t offset, uint32_t data);
uint32_t read_dac_if(uint32_t offset);

uint32_t write_dac_if_command(uint32_t *args);
uint32_t read_dac_if_command(uint32_t *args);
uint32_t write_dac_spi_command(uint32_t* args);
uint32_t write_dac_ldac_command(uint32_t* args);
uint32_t toggle_dac_ldac_command(uint32_t* args) ;
uint32_t toggle_dac_reset_command(uint32_t* args) ;

#ifndef __LMK_IF__
#define __LMK_IF__
#include <inttypes.h>
#include <stdlib.h>
#define LMK_IF_BASE 0x4000

int write_lmk_if(uint32_t offset, uint32_t data);
uint32_t read_lmk_if(uint32_t offset);
uint32_t write_lmk_if_command(uint32_t *args);
uint32_t read_lmk_if_command(uint32_t *args);
uint32_t write_lmk_spi_command(uint32_t* args);
uint32_t lmk_spi_data_available_command(uint32_t* args);
uint32_t lmk_spi_data_pop_command(uint32_t* args);
#endif

#ifndef __DAC_IF__
#define __DAC_IF__
#include <inttypes.h>
#include <stdlib.h>
#define DAC_IF_BASE 0x2180



int write_dac_if(uint32_t offset, uint32_t data);
uint32_t read_dac_if(uint32_t offset);

uint32_t write_dac_if_command(uint32_t *args);
uint32_t read_dac_if_command(uint32_t *args);
uint32_t write_dac_spi_command(uint32_t* args);
uint32_t write_dac_ldac_command(uint32_t* args);
uint32_t toggle_dac_ldac_command(uint32_t* args);
uint32_t toggle_dac_reset_command(uint32_t* args);
uint32_t set_ocm_for_channel_command(uint32_t* args);
uint32_t set_bias_for_channel_command(uint32_t* args);
#endif

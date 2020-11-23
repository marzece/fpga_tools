#include <inttypes.h>
#include <stdlib.h>
#define ADS_A_IF_BASE 0x2080
#define ADS_B_IF_BASE 0x2100

#define SPICR_OFFSET 0x60
#define SPIDTR_OFFSET 0x68
#define SPISSR_OFFSET 0x70


int write_ads_a_if(uint32_t offset, uint32_t data);
uint32_t read_ads_a_if(uint32_t offset);
int write_ads_b_if(uint32_t offset, uint32_t data);
uint32_t read_ads_b_if(uint32_t offset);

uint32_t write_ads_a_if_command(uint32_t *args);
uint32_t read_ads_a_if_command(uint32_t *args);
uint32_t write_ads_b_if_command(uint32_t *args);
uint32_t read_ads_b_if_command(uint32_t *args);
uint32_t write_a_adc_spi_command(uint32_t *args);
uint32_t write_b_adc_spi_command(uint32_t *args);

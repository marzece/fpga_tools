#include <inttypes.h>
#include <stdlib.h>

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
uint32_t ads_a_spi_data_available_command(uint32_t*);
uint32_t ads_a_spi_data_pop_command(uint32_t*);
uint32_t ads_b_spi_data_available_command(uint32_t*);
uint32_t ads_b_spi_data_pop_command(uint32_t*);

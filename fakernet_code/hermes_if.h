#ifndef __HERMES_IF__
#define  __HERMES_IF__
#include "server_common.h"
#include <stdlib.h>
#include <stdint.h>
#include "axi_qspi.h"
#include "gpio.h"
#include "iic.h"


uint32_t read_iic_block_command(uint32_t* args);
uint32_t write_iic_block_command(uint32_t* args);
uint32_t read_iic_bus_command(uint32_t* args);
uint32_t write_iic_bus_command(uint32_t* args);
uint32_t read_iic_bus_with_reg_command(uint32_t* args);
uint32_t write_iic_bus_with_reg_command(uint32_t* args);
uint32_t write_gpio_0_command(uint32_t *args);
uint32_t read_gpio_0_command(uint32_t *args);
uint32_t write_gpio_1_command(uint32_t *args);
uint32_t read_gpio_1_command(uint32_t *args);
//uint32_t write_gpio_2_command(uint32_t *args);
//uint32_t read_gpio_2_command(uint32_t *args);
uint32_t set_adc_power_command(uint32_t *args);
uint32_t set_preamp_power_command(uint32_t *args);

uint32_t read_ads_a_if_command(uint32_t* args);
uint32_t write_ads_a_if_command(uint32_t* args);
uint32_t read_ads_b_if_command(uint32_t* args);
uint32_t write_ads_b_if_command(uint32_t* args);
uint32_t write_a_adc_spi_command(uint32_t* args);
uint32_t write_b_adc_spi_command(uint32_t* args);
uint32_t ads_a_spi_data_available_command(uint32_t* args);
uint32_t ads_b_spi_data_available_command(uint32_t* args);
uint32_t ads_a_spi_data_pop_command(uint32_t* args);
uint32_t ads_b_spi_data_pop_command(uint32_t* args);
uint32_t write_lmk_if_command(uint32_t* args);
uint32_t read_lmk_if_command(uint32_t* args);
uint32_t write_lmk_spi_command(uint32_t* args);
uint32_t lmk_spi_data_available_command(uint32_t* args);
uint32_t lmk_spi_data_pop_command(uint32_t* args);
uint32_t read_dac_if_command(uint32_t* args);
uint32_t write_dac_if_command(uint32_t* args);
uint32_t write_dac_spi_command(uint32_t* args);
uint32_t set_bias_for_channel_command(uint32_t* args);
uint32_t set_ocm_for_channel_command(uint32_t* args);
uint32_t toggle_dac_ldac_command(uint32_t* args);
uint32_t toggle_dac_reset_command(uint32_t* args);

struct HERMES_IF {
    AXI_QSPI* lmk;
    AXI_QSPI* adc_a;
    AXI_QSPI* adc_b;
    AXI_QSPI* dac;
    AXI_GPIO* gpio0;
    AXI_GPIO* gpio1;
    AXI_GPIO* gpio2;
    AXI_IIC* iic_main;
};
struct HERMES_IF* hermes;

// TODO I need some way to link between IIC Addresses and R/W sizes for each chip


extern ServerCommand hermes_commands[];
#endif

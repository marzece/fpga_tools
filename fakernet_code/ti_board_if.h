#ifndef __TI_BOARD_IF__
#define __TI_BOARD_IF__

extern ServerCommand ti_commands[];
uint32_t read_gpio_0_command(uint32_t* args);
uint32_t write_gpio_0_command(uint32_t* args);
uint32_t read_ads_if_command(uint32_t* args);
uint32_t write_ads_if_command(uint32_t* args);
uint32_t write_adc_spi_command(uint32_t* args);
uint32_t ads_spi_data_available_command(uint32_t* args);
uint32_t ads_spi_data_pop_command(uint32_t* args);
uint32_t write_lmk_if_command(uint32_t* args);
uint32_t read_lmk_if_command(uint32_t* args);
uint32_t write_lmk_spi_command(uint32_t* args);
uint32_t lmk_spi_data_available_command(uint32_t* args);
uint32_t lmk_spi_data_pop_command(uint32_t* args);
    
#endif


#include <stdlib.h>
#include "ti_board_if.h"
#include "axi_qspi.h"
#include "gpio.h"

// TODO !!!
#define ADC_A_AXI_ADDR 0x0
#define GPIO0_AXI_ADDR 0x0
#define GPIO1_AXI_ADDR 0x0
#define LMK_AXI_ADDR 0x0

struct TI_IF {
    AXI_QSPI* lmk;
    AXI_QSPI* adc;
    AXI_GPIO* gpio0;
};

struct TI_IF* ti_board = NULL;


ServerCommand ti_commands[]=  {
    {"read_gpio0", read_gpio_0_command, 1},
    {"write_gpio0", write_gpio_0_command, 2},
    {"read_ads_a", read_ads_if_command, 1},
    {"write_ads_a", write_ads_if_command, 2},
    {"write_a_adc_spi", write_adc_spi_command, 3},
    {"ads_a_spi_data_available", ads_spi_data_available_command, 0},
    {"ads_a_spi_pop", ads_spi_data_pop_command, 0},
    {"write_lmk_if", write_lmk_if_command, 2},
    {"read_lmk_if", read_lmk_if_command, 1},
    {"write_lmk_spi", write_lmk_spi_command, 3},
    {"lmk_spi_data_available", lmk_spi_data_available_command, 0},
    {"lmk_spi_data_pop", lmk_spi_data_pop_command, 0},
    {"", NULL, 0} // Must be last
};

struct TI_IF* get_ti_handle() {
    if(ti_board == NULL) {
        hermes = malloc(sizeof(struct HERMES_IF));
        hermes->lmk = new_lmk_spi("lmk", LMK_AXI_ADDR);
        hermes->adc_a = new_ads_spi("adc_a", ADC_A_AXI_ADDR);
        hermes->adc_b = new_ads_spi("adc_b", ADC_B_AXI_ADDR);
        hermes->gpio0 = new_gpio("gpio", GPIO0_AXI_ADDR);
    }


}
uint32_t write_gpio_0_command(uint32_t *args) {
    return write_gpio_value(get_ti_handle()->gpio0, args[0], args[1]);
}

uint32_t read_gpio_0_command(uint32_t *args) {
    return read_gpio_value(get_ti_handle()->gpio0, args[0]);
}

uint32_t write_ads_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_qspi_addr(get_ti_handle()->adc_a, offset, data);
}

uint32_t read_ads_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    return read_qspi_addr(get_ti_handle()->adc_a, offset);
}

uint32_t write_adc_spi_command(uint32_t* args) {
    return write_adc_spi(get_ti_handle()->adc_a, args);
}

uint32_t ads_a_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_ti_handle()->adc_a);
}

uint32_t ads_a_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_ti_handle()->adc_a);
}


uint32_t write_lmk_if_command(uint32_t* args) {
    return write_lmk_if(get_ti_handle()->lmk, args[0], args[1]);
}

uint32_t read_lmk_if_command(uint32_t* args) {
    return read_lmk_if(get_ti_handle()->lmk, args[0]);
}

uint32_t write_lmk_spi_command(uint32_t* args) {
    uint32_t rw = args[0];
    uint32_t addr = args[1];
    uint32_t data = args[2];

    // RW is 3 bits and the bottom bits are always zero (pretty sure)
    // so RW only has two valid values.
    rw = rw ? 0x4 : 0x0;
    addr = addr & 0x1FFF;
    data = data & 0xFF;

    uint32_t word1 = (rw << 5) | (addr >> 8);
    uint32_t word2 = addr & 0xFF;
    uint32_t word3 = data & 0xFF;

    uint8_t word_buf[3] = {word1, word2, word3};
    // Bit 0 of the SSR is the LMK select line, pull it low to start SPI data transfer
    uint32_t ssr = 0x0;
    return write_spi(get_ti_handle()->lmk, ssr, word_buf, 3);
}

uint32_t lmk_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_ti_handle()->lmk);
}

uint32_t lmk_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_ti_handle()->lmk);
}


#include <stdlib.h>
#include "server_common.h"
#include "ti_board_if.h"
#include "axi_qspi.h"
#include "ads_if.h"
#include "lmk_if.h"

#define ADC_AXI_ADDR 0x0
#define LMK_AXI_ADDR 0x0
#define IIC_AXI_ADDR 0x10000

struct TI_IF {
    AXI_QSPI* lmk;
    AXI_QSPI* adc;
};

struct TI_IF* ti_board = NULL;

//uint32_t read_ads_if_command(uint32_t* args);
//uint32_t write_ads_if_command(uint32_t* args);
//uint32_t write_adc_spi_command(uint32_t* args);
//uint32_t ads_spi_data_available_command(uint32_t* args);
//uint32_t ads_spi_data_pop_command(uint32_t* args);
//uint32_t write_lmk_if_command(uint32_t* args);
//uint32_t read_lmk_if_command(uint32_t* args);
//uint32_t write_lmk_spi_command(uint32_t* args);
//uint32_t lmk_spi_data_available_command(uint32_t* args);
//uint32_t lmk_spi_data_pop_command(uint32_t* args);

const uint32_t TI_SAFE_READ_ADDRESS = IIC_AXI_ADDR + 0x104; // Should be the status register for the iic core

struct TI_IF* get_ti_handle() {
    if(ti_board == NULL) {
        ti_board = malloc(sizeof(struct TI_IF));
        ti_board->lmk = new_lmk_spi("lmk", LMK_AXI_ADDR);
        ti_board->adc = new_ads_spi("adc", ADC_AXI_ADDR);
    }

    return ti_board;
}

static uint32_t write_ads_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_qspi_addr(get_ti_handle()->adc, offset, data);
}

static uint32_t read_ads_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    return read_qspi_addr(get_ti_handle()->adc, offset);
}

static uint32_t write_adc_spi_command(uint32_t* args) {
    uint32_t wmpch = args[0];
    uint32_t adc_addr = args[1];
    uint32_t adc_data = args[2];

    wmpch &= 0xF;
    adc_addr &= 0xFFF;
    adc_data &= 0xFF;

    uint8_t word_buf[3];

    word_buf[0] = (wmpch<<4) | (adc_addr >> 8);
    word_buf[1] = adc_addr & 0xFF;
    word_buf[2] = adc_data;

    uint8_t ssr = 0x6;
    return write_spi(get_ti_handle()->adc, ssr, word_buf, 3);
    //return write_adc_spi(get_ti_handle()->adc, args);
}

static uint32_t ads_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_ti_handle()->adc);
}

static uint32_t ads_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_ti_handle()->adc);
}


static uint32_t write_lmk_if_command(uint32_t* args) {
    return write_lmk_if(get_ti_handle()->lmk, args[0], args[1]);
}

static uint32_t read_lmk_if_command(uint32_t* args) {
    return read_lmk_if(get_ti_handle()->lmk, args[0]);
}

static uint32_t write_lmk_spi_command(uint32_t* args) {
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
    uint32_t ssr = 0x5;
    return write_spi(get_ti_handle()->lmk, ssr, word_buf, 3);
}

 static uint32_t lmk_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_ti_handle()->lmk);
}

static uint32_t lmk_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_ti_handle()->lmk);
}

ServerCommand ti_commands[]=  {
    {"read_ads", read_ads_if_command, 1},
    {"write_ads", write_ads_if_command, 2},
    {"write_adc_spi", write_adc_spi_command, 3},
    {"ads_spi_data_available", ads_spi_data_available_command, 0},
    {"ads_spi_pop", ads_spi_data_pop_command, 0},
    {"write_lmk_if", write_lmk_if_command, 2},
    {"read_lmk_if", read_lmk_if_command, 1},
    {"write_lmk_spi", write_lmk_spi_command, 3},
    {"lmk_spi_data_available", lmk_spi_data_available_command, 0},
    {"lmk_spi_data_pop", lmk_spi_data_pop_command, 0},
    {"", NULL, 0} // Must be last
};

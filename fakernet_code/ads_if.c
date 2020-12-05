#include <inttypes.h>
#include <stdlib.h>
#include "ads_if.h"
#include "axi_qspi.h"
#define ADS_A_IF_BASE 0x2080
#define ADS_B_IF_BASE 0x2100

enum ADS_ID{
    ADS_A_ID,
    ADS_B_ID,
};

static AXI_QSPI* ads_a_axi_qspi = NULL;
static AXI_QSPI* ads_b_axi_qspi = NULL;

static AXI_QSPI* get_spi_handle(enum ADS_ID id) {
    AXI_QSPI** axi_qspi_pntr = id==ADS_A_ID ? &ads_a_axi_qspi : &ads_b_axi_qspi;

    if(!*axi_qspi_pntr) {
        *axi_qspi_pntr = malloc(sizeof(AXI_QSPI));
        const char* name = id == ADS_A_ID ? "ads_a_if" : "ads_b_if";
        uint32_t axi_addr = id == ADS_A_ID ? ADS_A_IF_BASE : ADS_B_IF_BASE;
        **axi_qspi_pntr = new_axi_qspi(name, axi_addr);
        // Initialze CR values
        (*axi_qspi_pntr)->spi_cr.master=1;
    }
    return *axi_qspi_pntr;
}
int write_ads_a_if(uint32_t offset, uint32_t data) {
    return write_qspi_addr(get_spi_handle(ADS_A_ID), offset, data);
}

uint32_t read_ads_a_if(uint32_t offset) {
    return read_qspi_addr(get_spi_handle(ADS_A_ID), offset);
}

int write_ads_b_if(uint32_t offset, uint32_t data) {
    return write_qspi_addr(get_spi_handle(ADS_B_ID), offset, data);
}

uint32_t read_ads_b_if(uint32_t offset) {
    return read_qspi_addr(get_spi_handle(ADS_B_ID), offset);
}

uint32_t write_ads_a_if_command(uint32_t *args) {
    return write_ads_a_if(args[0], args[1]);
}

uint32_t read_ads_a_if_command(uint32_t *args) {
    return read_ads_a_if(args[0]);
}

uint32_t write_ads_b_if_command(uint32_t *args) {
    return write_ads_b_if(args[0], args[1]);
}

uint32_t read_ads_b_if_command(uint32_t *args) {
    return read_ads_b_if(args[0]);
}

uint32_t write_adc_spi(enum ADS_ID id, uint32_t* args) {
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

    AXI_QSPI* handle = get_spi_handle(id);

    uint8_t ssr = 0x0;
    return write_spi(handle, ssr, word_buf, 3);
}

uint32_t write_a_adc_spi_command(uint32_t* args) {
    return write_adc_spi(ADS_A_ID, args);
}

uint32_t write_b_adc_spi_command(uint32_t* args) {
    return write_adc_spi(ADS_B_ID, args);
}

uint32_t ads_a_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_spi_handle(ADS_A_ID));
}

uint32_t ads_a_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_spi_handle(ADS_A_ID));
}

uint32_t ads_b_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_spi_handle(ADS_B_ID));
}

uint32_t ads_b_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_spi_handle(ADS_B_ID));
}

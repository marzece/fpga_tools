#include <inttypes.h>
#include <stdlib.h>
#include "ads_if.h"

uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);

static int spi_a_cr_initialized=0;
static int spi_b_cr_initialized=0;
#define SPI_CR_INITIALIZATION_VALUE 0x90

int write_ads_a_if(uint32_t offset, uint32_t data) {
    if(!spi_a_cr_initialized) {
        write_addr(ADS_A_IF_BASE, SPICR_OFFSET, SPI_CR_INITIALIZATION_VALUE);
        spi_a_cr_initialized = 1;
    }
    return write_addr(ADS_A_IF_BASE, offset, data);
}

uint32_t read_ads_a_if(uint32_t offset) {
    return double_read_addr(ADS_A_IF_BASE, offset);
}

int write_ads_b_if(uint32_t offset, uint32_t data) {
    if(!spi_b_cr_initialized) {
        write_addr(ADS_B_IF_BASE, SPICR_OFFSET, SPI_CR_INITIALIZATION_VALUE);
        spi_b_cr_initialized = 1;
    }
    return write_addr(ADS_B_IF_BASE, offset, data);
}

uint32_t read_ads_b_if(uint32_t offset) {
    return double_read_addr(ADS_B_IF_BASE, offset);
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

uint32_t write_adc_spi(int adc_a, uint32_t* args) {
    // This just for ADC A TODO
    uint32_t wmpch = args[0];
    uint32_t adc_addr = args[1];
    uint32_t adc_data = args[2];

    wmpch &= 0xF;
    adc_addr &= 0xFFF;
    adc_data &= 0xFF;

    uint32_t word1 = (wmpch<<4) | (adc_addr >> 8);
    uint32_t word2 = adc_addr & 0xFF;
    uint32_t word3 = adc_data;

    // Only bit 1 of the slave select register is used
    uint32_t slave_select = 0x1; 

    // TODO this is dumb, come up with a way to not write every command twice
    if(adc_a) {
        write_ads_a_if(SPISSR_OFFSET, slave_select);
        write_ads_a_if(SPIDTR_OFFSET, word1);
        write_ads_a_if(SPIDTR_OFFSET, word2);
        write_ads_a_if(SPIDTR_OFFSET, word3);
        write_ads_a_if(SPISSR_OFFSET, 0x0);
    }
    else {
        // if not writing to ADC A, then we're writing to ADC B
        write_ads_b_if(SPISSR_OFFSET, slave_select);
        write_ads_b_if(SPIDTR_OFFSET, word1);
        write_ads_b_if(SPIDTR_OFFSET, word2);
        write_ads_b_if(SPIDTR_OFFSET, word3);
        write_ads_b_if(SPISSR_OFFSET, 0x0);
    }
    return 0;
}

uint32_t write_a_adc_spi_command(uint32_t* args) {
    return write_adc_spi(1, args);
}

uint32_t write_b_adc_spi_command(uint32_t* args) {
    return write_adc_spi(0, args);
}

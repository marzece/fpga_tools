#include <inttypes.h>
#include <stdlib.h>
#include "ads_if.h"
#include "axi_qspi.h"

//enum ADS_ID{
//    ADS_A_ID,
//    ADS_B_ID,
//};


//static AXI_QSPI* ads_a_axi_qspi = NULL;
//static AXI_QSPI* ads_b_axi_qspi = NULL;

//static AXI_QSPI* get_spi_handle(enum ADS_ID id) {
//    AXI_QSPI** axi_qspi_pntr = id==ADS_A_ID ? &ads_a_axi_qspi : &ads_b_axi_qspi;
//
//    if(!*axi_qspi_pntr) {
//        *axi_qspi_pntr = malloc(sizeof(AXI_QSPI));
//        const char* name = id == ADS_A_ID ? "ads_a_if" : "ads_b_if";
//        uint32_t axi_addr = id == ADS_A_ID ? ADS_A_IF_BASE : ADS_B_IF_BASE;
//        **axi_qspi_pntr = new_axi_qspi(name, axi_addr);
//        // Initialze CR values
//        (*axi_qspi_pntr)->spi_cr.master=1;
//    }
//    return *axi_qspi_pntr;
//}

int write_ads_if(AXI_QSPI* adc, uint32_t offset, uint32_t data) {
    return write_qspi_addr(adc, offset, data);
}

uint32_t read_ads_if(AXI_QSPI* adc, uint32_t offset) {
    return read_qspi_addr(adc, offset);
}

uint32_t write_adc_spi(AXI_QSPI* qspi, uint32_t* args) {
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

    uint8_t ssr = 0x0;
    return write_spi(qspi, ssr, word_buf, 3);
}

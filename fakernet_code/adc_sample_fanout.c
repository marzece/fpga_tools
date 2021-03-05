#include "adc_sample_fanout.h"
uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);

// Only a single address can be read from/written to, at 0x10
static const uint32_t offset = 0x10;

AXI_ADC_SAMPLE_FANOUT* new_adc_sample_fanout(const char* name, uint32_t axi_addr) {
    AXI_ADC_SAMPLE_FANOUT* ret = malloc(sizeof(AXI_ADC_SAMPLE_FANOUT));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_fanout_value(AXI_ADC_SAMPLE_FANOUT *handle) {
    return double_read_addr(handle->axi_addr, offset);
}

uint32_t write_fanout_value(AXI_ADC_SAMPLE_FANOUT* handle, uint32_t data) {
    return write_addr(handle->axi_addr, offset, data);
}

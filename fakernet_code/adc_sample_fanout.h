#ifndef __AXI_ADC_SAMPLE_FANOUT__
#define __AXI_ADC_SAMPLE_FANOUT__
#include <inttypes.h>
#include <stdlib.h>

typedef struct AXI_ADC_SAMPLE_FANOUT {
    const char* name;
    uint32_t axi_addr;
} AXI_ADC_SAMPLE_FANOUT;

AXI_ADC_SAMPLE_FANOUT* new_adc_sample_fanout(const char* name, uint32_t axi_addr);
uint32_t read_fanout_value(AXI_ADC_SAMPLE_FANOUT* handle);
uint32_t write_fanout_value(AXI_ADC_SAMPLE_FANOUT* handle, uint32_t data);
#endif

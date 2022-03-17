#ifndef __DATA_PIPELINE__
#define __DATA_PIPELINE__
#include <inttypes.h>
#include <stdlib.h>

typedef struct AXI_DATA_PIPELINE {
    const char* name;
    uint32_t axi_addr;
} AXI_DATA_PIPELINE;

AXI_DATA_PIPELINE* new_data_pipeline_if(const char* name, uint32_t axi_addr);
uint32_t read_data_pipeline_value(AXI_DATA_PIPELINE *dp_axi, uint32_t offset);
uint32_t write_data_pipeline_value(AXI_DATA_PIPELINE* dp_axi, uint32_t offset, uint32_t data);
uint32_t write_trig_sum_width(AXI_DATA_PIPELINE* dp_axi, uint32_t value);
uint32_t read_trig_sum_width(AXI_DATA_PIPELINE* dp_axi);
uint32_t read_threshold(AXI_DATA_PIPELINE* dp_axi);
uint32_t write_threshold(AXI_DATA_PIPELINE* dp_axi, uint32_t data) ;
uint32_t read_channel_depth(AXI_DATA_PIPELINE* dp_axi, int channel);
uint32_t write_channel_depth(AXI_DATA_PIPELINE* dp_axi, int channel, uint32_t val);
uint32_t write_channel_mask(AXI_DATA_PIPELINE* dp_axi, uint32_t mask);
uint32_t read_channel_mask(AXI_DATA_PIPELINE* dp_axi);
uint32_t read_invalid_count(AXI_DATA_PIPELINE* dp_axi);
uint32_t read_fifo_status_reg(AXI_DATA_PIPELINE* dp_axi);
uint32_t read_local_trigger_enable(AXI_DATA_PIPELINE* dp_axi);
uint32_t write_local_trigger_enable(AXI_DATA_PIPELINE* dp_axi, uint32_t val);
uint32_t read_local_trigger_mode(AXI_DATA_PIPELINE* dp_axi);
uint32_t write_local_trigger_mode(AXI_DATA_PIPELINE* dp_axi, uint32_t val);
uint32_t read_local_trigger_length(AXI_DATA_PIPELINE* dp_axi);
uint32_t write_local_trigger_length(AXI_DATA_PIPELINE* dp_axi, uint32_t val);
uint32_t read_local_trigger_count_reset_value(AXI_DATA_PIPELINE* dp_axi);
uint32_t write_local_trigger_count_reset_value(AXI_DATA_PIPELINE* dp_axi, uint32_t val);
uint32_t read_trigger_enable(AXI_DATA_PIPELINE *dp_axi);
uint32_t write_trigger_enable(AXI_DATA_PIPELINE *dp_axi, uint32_t val);
#endif

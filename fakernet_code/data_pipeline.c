#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "data_pipeline.h"


uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);

#define  RESET_REG_OFFSET          0x0
#define  THRESHOLD_REG_OFFSET      0x4
#define  CHANNEL_MASK_OFFSET       0x8
#define  DEPTH_ADDR_OFFSET         0xC
#define  DEPTH_ADDR_WIDTH          0x4
#define  DEVICE_NUMBER_REG_OFFSET  0x80
#define  FIFO_STATUS_REG_OFFSET    0x84
#define  INVALID_COUNT_REG_OFFSET  0x88


AXI_DATA_PIPELINE* new_data_pipeline_if(const char* name, uint32_t axi_addr) {
    AXI_DATA_PIPELINE* ret = malloc(sizeof(AXI_DATA_PIPELINE));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_data_pipeline_value(AXI_DATA_PIPELINE *dp_axi, uint32_t offset) {
    return double_read_addr(dp_axi->axi_addr, offset);
}

uint32_t write_data_pipeline_value(AXI_DATA_PIPELINE* dp_axi, uint32_t offset, uint32_t data) {
    return write_addr(dp_axi->axi_addr, offset, data);
}

uint32_t read_threshold(AXI_DATA_PIPELINE* dp_axi) {
    return read_data_pipeline_value(dp_axi, THRESHOLD_REG_OFFSET);
}

uint32_t write_threshold(AXI_DATA_PIPELINE* dp_axi, uint32_t data) {
    return write_data_pipeline_value(dp_axi, THRESHOLD_REG_OFFSET, data);
}

uint32_t read_channel_mask(AXI_DATA_PIPELINE* dp_axi) {
    return read_data_pipeline_value(dp_axi, CHANNEL_MASK_OFFSET);

}

uint32_t write_channel_mask(AXI_DATA_PIPELINE* dp_axi, uint32_t mask) {
    return write_data_pipeline_value(dp_axi, CHANNEL_MASK_OFFSET, mask);
}

uint32_t write_channel_depth(AXI_DATA_PIPELINE* dp_axi, int channel, uint32_t val) {
    uint32_t offset = DEPTH_ADDR_OFFSET + DEPTH_ADDR_WIDTH*channel;
    return write_data_pipeline_value(dp_axi, offset, val);
}

uint32_t read_channel_depth(AXI_DATA_PIPELINE* dp_axi, int channel) {
    uint32_t offset = DEPTH_ADDR_OFFSET + DEPTH_ADDR_WIDTH*channel;
    return read_data_pipeline_value(dp_axi, offset);
}

uint32_t write_reset_reg(AXI_DATA_PIPELINE* dp_axi, int reset_mask) {
    uint32_t offset = RESET_REG_OFFSET;
    return write_data_pipeline_value(dp_axi, offset, reset_mask);
}

uint32_t read_fifo_status_reg(AXI_DATA_PIPELINE* dp_axi) {
    uint32_t offset = FIFO_STATUS_REG_OFFSET;
    return read_data_pipeline_value(dp_axi, offset);
}

uint32_t read_invalid_count(AXI_DATA_PIPELINE* dp_axi) {
    uint32_t offset = INVALID_COUNT_REG_OFFSET;
    return read_data_pipeline_value(dp_axi, offset);
}

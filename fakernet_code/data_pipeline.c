#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "data_pipeline.h"


int read_addr(uint32_t, uint32_t, uint32_t*);
int double_read_addr(uint32_t, uint32_t, uint32_t*);
int write_addr(uint32_t, uint32_t, uint32_t);

#define  RESET_REG_OFFSET                 0x0
#define  THRESHOLD_REG_OFFSET             0x4
#define  CHANNEL_MASK_OFFSET              0x8
#define  DEVICE_NUMBER_REG_OFFSET         0xC
#define  FIFO_STATUS_REG_OFFSET           0x10
#define  INVALID_COUNT_REG_OFFSET         0x14
#define  LOCAL_TRIGGER_ENABLE_OFFSET      0x18
#define  LOCAL_TRIGGER_MODE_OFFSET        0x1C
#define  LOCAL_TRIGGER_LENGTH_OFFSET      0x20
#define  LOCAL_TRIGGER_COUNT_RESET_OFFST  0x24
#define  TRIGGER_ENABLE_OFFSET            0x28
#define  TRIGGER_SUM_WIDTH_OFFSET         0x2C
#define  GLOBAL_DEPTH_ADDR_OFFSET         0x34
#define  DEPTH_ADDR_OFFSET                0x100
#define  DEPTH_ADDR_WIDTH                 0x4
#define  BUILD_INFO_OFFSET                0x804
//#define REGISTER_FILE_STATUS_OFFSET       0x800


AXI_DATA_PIPELINE* new_data_pipeline_if(const char* name, uint32_t axi_addr) {
    AXI_DATA_PIPELINE* ret = malloc(sizeof(AXI_DATA_PIPELINE));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_data_pipeline_value(AXI_DATA_PIPELINE *dp_axi, uint32_t offset) {
    uint32_t ret;
    if(double_read_addr(dp_axi->axi_addr, offset, &ret)) {
        // TODO!!!
        return -1;
    }
    return ret;
}

uint32_t write_data_pipeline_value(AXI_DATA_PIPELINE* dp_axi, uint32_t offset, uint32_t data) {
    return write_addr(dp_axi->axi_addr, offset, data);
}

uint32_t read_trig_sum_width(AXI_DATA_PIPELINE* dp_axi) {
    return read_data_pipeline_value(dp_axi, TRIGGER_SUM_WIDTH_OFFSET);
}

uint32_t write_trig_sum_width(AXI_DATA_PIPELINE* dp_axi, uint32_t value) {
    return write_data_pipeline_value(dp_axi, TRIGGER_SUM_WIDTH_OFFSET, value);
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

uint32_t write_global_depth(AXI_DATA_PIPELINE* dp_axi, uint32_t val) {
    uint32_t offset = GLOBAL_DEPTH_ADDR_OFFSET;
    return write_data_pipeline_value(dp_axi, offset, val);
}

uint32_t read_global_depth(AXI_DATA_PIPELINE* dp_axi) {
    uint32_t offset = GLOBAL_DEPTH_ADDR_OFFSET;
    return read_data_pipeline_value(dp_axi, offset);
}

uint32_t read_fifo_status_reg(AXI_DATA_PIPELINE* dp_axi) {
    uint32_t offset = FIFO_STATUS_REG_OFFSET;
    return read_data_pipeline_value(dp_axi, offset);
}

uint32_t read_invalid_count(AXI_DATA_PIPELINE* dp_axi) {
    uint32_t offset = INVALID_COUNT_REG_OFFSET;
    return read_data_pipeline_value(dp_axi, offset);
}

uint32_t read_local_trigger_enable(AXI_DATA_PIPELINE* dp_axi) {
    return read_data_pipeline_value(dp_axi, LOCAL_TRIGGER_ENABLE_OFFSET);
}

uint32_t write_local_trigger_enable(AXI_DATA_PIPELINE* dp_axi, uint32_t val) {
    return write_data_pipeline_value(dp_axi, LOCAL_TRIGGER_ENABLE_OFFSET, val);
}

uint32_t read_local_trigger_mode(AXI_DATA_PIPELINE* dp_axi) {
    return read_data_pipeline_value(dp_axi, LOCAL_TRIGGER_MODE_OFFSET);
}

uint32_t write_local_trigger_mode(AXI_DATA_PIPELINE* dp_axi, uint32_t val) {
    return write_data_pipeline_value(dp_axi, LOCAL_TRIGGER_MODE_OFFSET, val);
}

uint32_t read_local_trigger_length(AXI_DATA_PIPELINE* dp_axi) {
    return read_data_pipeline_value(dp_axi, LOCAL_TRIGGER_LENGTH_OFFSET);
}

uint32_t write_local_trigger_length(AXI_DATA_PIPELINE* dp_axi, uint32_t val) {
    return write_data_pipeline_value(dp_axi, LOCAL_TRIGGER_LENGTH_OFFSET, val);
}

uint32_t read_local_trigger_count_reset_value(AXI_DATA_PIPELINE* dp_axi) {
    return read_data_pipeline_value(dp_axi, LOCAL_TRIGGER_COUNT_RESET_OFFST);
}

uint32_t write_local_trigger_count_reset_value(AXI_DATA_PIPELINE* dp_axi, uint32_t val) {
    return write_data_pipeline_value(dp_axi, LOCAL_TRIGGER_COUNT_RESET_OFFST, val);
}

uint32_t read_trigger_enable(AXI_DATA_PIPELINE *dp_axi) {
    return read_data_pipeline_value(dp_axi, TRIGGER_ENABLE_OFFSET);
}

uint32_t write_trigger_enable(AXI_DATA_PIPELINE *dp_axi, uint32_t val) {
    return write_data_pipeline_value(dp_axi, TRIGGER_ENABLE_OFFSET, val);
}

uint32_t read_build_info(AXI_DATA_PIPELINE *dp_axi) {
    /* In case I ever want to translate the build timestamp below is the code to do that.
     * The documentation for the timestamp comes from Xilinx UG570, the section
     * describing the USR_ACCESSE2 register.*/
    uint32_t timestamp = read_data_pipeline_value(dp_axi, BUILD_INFO_OFFSET);
    return timestamp;
    // int day = (timestamp>>27);
    // int month = (timestamp>>23) & 0xF;
    // int year = (timestamp>>17) & 0x3F;
    // int hour = (timestamp>>12) & 0x1F;
    // int minute = (timestamp>>6) & 0x3F;
    // int second = timestamp & 0x3F;
}

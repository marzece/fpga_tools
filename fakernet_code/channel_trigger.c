#include "channel_trigger.h"
#include <inttypes.h>
#include <stdlib.h>

uint32_t read_addr(uint32_t, uint32_t);
uint32_t double_read_addr(uint32_t, uint32_t);
int write_addr(uint32_t, uint32_t, uint32_t);

// Theres only one valid AXI address, it's for the threshold value.
#define THRESHOLD_OFFSET 0x10

AXI_CHANNEL_TRIGGER* new_channel_trigger(const char* name, uint32_t axi_addr){
    AXI_CHANNEL_TRIGGER* ret = malloc(sizeof(AXI_CHANNEL_TRIGGER));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_channel_trigger_value(AXI_CHANNEL_TRIGGER* ct) {
    return double_read_addr(ct->axi_addr,THRESHOLD_OFFSET);
}

uint32_t write_channel_trigger_value(AXI_CHANNEL_TRIGGER* ct, uint32_t data) {
    return write_addr(ct->axi_addr, THRESHOLD_OFFSET, data);
}

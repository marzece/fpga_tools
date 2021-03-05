#ifndef __CHANNEL_TRIGGER__
#define __CHANNEL_TRIGGER__
#include <inttypes.h>
#include <stdlib.h>

typedef struct AXI_CHANNEL_TRIGGER {
    const char* name;
    uint32_t axi_addr;
} AXI_CHANNEL_TRIGGER;

AXI_CHANNEL_TRIGGER* new_channel_trigger(const char* name, uint32_t axi_addr);
uint32_t read_channel_trigger_value(AXI_CHANNEL_TRIGGER* ct);
uint32_t write_channel_trigger_value(AXI_CHANNEL_TRIGGER* ct, uint32_t data);
//uint32_t set_threshold(AXI_CHANNEL_TRIGGER* ct, uint32_t threshold);
#endif

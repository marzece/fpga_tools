
#ifndef __JESD__
#define __JESD__
#include <inttypes.h>
#include <stdlib.h>

typedef struct AXI_JESD {
    const char* name;
    uint32_t axi_addr;
} AXI_JESD;

AXI_JESD* new_jesd(const char* name, uint32_t axi_addr);
uint32_t read_jesd(AXI_JESD* jesd, uint32_t offset);
uint32_t write_jesd(AXI_JESD* jesd, uint32_t offset, uint32_t data);
uint32_t jesd_read_error_rate(AXI_JESD* jesd, uint32_t* results);
uint32_t jesd_reset(AXI_JESD* jesd);
uint32_t jesd_is_synced(AXI_JESD* jesd);
uint32_t set_sync_error_reporting(AXI_JESD* jesd, uint32_t state);
uint32_t read_error_register(AXI_JESD* jesd, unsigned int channel);
#endif

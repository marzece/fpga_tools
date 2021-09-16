#ifndef __JESD_PHY__
#define __JESD_PHY__
#include <inttypes.h>
#include <stdlib.h>

typedef struct AXI_JESD_PHY {
    const char* name;
    uint32_t axi_addr;
} AXI_JESD_PHY;

AXI_JESD_PHY* new_jesd_phy(const char* name, uint32_t axi_addr);
uint32_t read_jesd_phy(AXI_JESD_PHY* jesd, uint32_t offset);
uint32_t write_jesd_phy(AXI_JESD_PHY* jesd, uint32_t offset, uint32_t data);
int write_lpmen(AXI_JESD_PHY* jesd_phy, uint32_t val);
uint32_t read_lpmen(AXI_JESD_PHY* jesd_phy);
#endif

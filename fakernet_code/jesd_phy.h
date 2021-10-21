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
int reset_lpmen(AXI_JESD_PHY* jesd_phy);
uint32_t read_insertion_loss(AXI_JESD_PHY* jesd_phy);
int write_insertion_loss(AXI_JESD_PHY* jesd_phy, uint32_t loss);
int read_sync_header_max(AXI_JESD_PHY* jesd_phy, uint32_t loss);
uint32_t read_drp_common(AXI_JESD_PHY* jesd_phy, uint32_t drp_address);
uint32_t read_drp_transceiver(AXI_JESD_PHY* jesd_phy, uint32_t drp_address);
uint32_t write_drp_common(AXI_JESD_PHY* jesd_phy, uint32_t drp_address, uint16_t data);
uint32_t write_drp_transceiver(AXI_JESD_PHY* jesd_phy, uint32_t drp_address, uint16_t data);
uint32_t read_jesd_phy_drp_interface_selector(AXI_JESD_PHY* jesd_phy);
uint32_t write_jesd_phy_drp_interface_selector(AXI_JESD_PHY* jesd_phy, uint32_t interface);
#endif

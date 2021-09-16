#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "jesd_phy.h"

#define JESD_PHY_VERSION_OFFSET 0x0
#define JESD_PHY_CONFIG_OFFSET 0x4
#define JESD_PHY_LPMEN_OFFSET 0x608
#define JESD_PHY_LPDFE_RESET  0x60C

int read_addr(uint32_t, uint32_t, uint32_t*);
int double_read_addr(uint32_t, uint32_t, uint32_t*);
int write_addr(uint32_t, uint32_t, uint32_t);

AXI_JESD_PHY* new_jesd_phy(const char* name, uint32_t axi_addr) {
    AXI_JESD_PHY* ret = malloc(sizeof(AXI_JESD_PHY));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_jesd_phy(AXI_JESD_PHY* jesd_phy, uint32_t offset) {
    uint32_t ret;
    if(double_read_addr(jesd_phy->axi_addr, offset, &ret)) {
        // TODO!
        return -1;
    }
    return ret;
}

uint32_t write_jesd_phy(AXI_JESD_PHY* jesd_phy, uint32_t offset, uint32_t data) {
    return write_addr(jesd_phy->axi_addr, offset, data);
}

int write_lpmen(AXI_JESD_PHY* jesd_phy, uint32_t val) {
    return write_jesd_phy(jesd_phy, JESD_PHY_LPMEN_OFFSET, val);
}

uint32_t read_lpmen(AXI_JESD_PHY* jesd_phy) {

    return read_jesd_phy(jesd_phy, JESD_PHY_LPMEN_OFFSET);

}

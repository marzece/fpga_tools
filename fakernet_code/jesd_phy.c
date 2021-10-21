#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "jesd_phy.h"

#define JESD_PHY_VERSION_OFFSET 0x0
#define JESD_PHY_CONFIG_OFFSET 0x4
#define JESD_PHY_GT_INTERFACE_SELECTOR 0x24
#define JESD_PHY_LPMEN_OFFSET 0x608
#define JESD_PHY_LPDFE_RESET  0x60C
#define JESD_PHY_INS_LOSS_OFFSET 0xD4
#define JESD_PHY_RX_LPDFE_RESET_OFFSET 0xD4

#define JESD_PHY_DRP_COMMON_BASE 0x100
#define JESD_PHY_DRP_TRANCEIVER_BASE 0x200
#define JESD_PHY_DRP_ADDR_OFFSET 0x4
#define JESD_PHY_DRP_WRITE_DATA_OFFSET 0x8
#define JESD_PHY_DRP_READ_DATA_OFFSET 0xC
#define JESD_PHY_DRP_RESET_OFFSET 0x10
#define JESD_PHY_DRP_ACCESS_STATUS_OFFSET 0x14
#define JESD_PHY_DRP_ACCESS_COMPLETE_OFFSET 0x1C

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

uint32_t read_insertion_loss(AXI_JESD_PHY* jesd_phy) {
    return read_jesd_phy(jesd_phy, JESD_PHY_INS_LOSS_OFFSET);
}

int reset_lpmen(AXI_JESD_PHY* jesd_phy) {
    int ret;
    ret = write_jesd_phy(jesd_phy, JESD_PHY_RX_LPDFE_RESET_OFFSET, 1);
    ret |= write_jesd_phy(jesd_phy, JESD_PHY_RX_LPDFE_RESET_OFFSET, 0);
    return ret;
}

int  write_insertion_loss(AXI_JESD_PHY* jesd_phy, uint32_t loss) {
    return write_jesd_phy(jesd_phy, JESD_PHY_INS_LOSS_OFFSET, loss);
}

uint32_t write_drp(AXI_JESD_PHY* jesd_phy, uint32_t base, uint32_t drp_address, uint16_t data) {
    // First write data to the DRP data address
    if(write_jesd_phy(jesd_phy, base + JESD_PHY_DRP_WRITE_DATA_OFFSET , data)) {
        // Error 
        printf("Error while writing to DRP data\n");
        return -1;
    }

    // Then write drp_addres the DRP Address register, and ensure the write bit (bit 31) is checked
    drp_address = (drp_address & 0x3FFFFFFF) | (1<<31);
    if(write_jesd_phy(jesd_phy, base + JESD_PHY_DRP_ADDR_OFFSET , drp_address)) {
        // Error
        printf("Error while writing to DRP address\n");
        return -1;
    }
    // Poll the access complete bit
    while(read_jesd_phy(jesd_phy, base + JESD_PHY_DRP_ACCESS_STATUS_OFFSET) != 0) {
        usleep(1);
    }

    return 0;
}

uint32_t read_drp(AXI_JESD_PHY* jesd_phy, uint32_t base, uint32_t drp_address) {
    // First write drp_addres the DRP Address register, and ensure the read bit (bit 30)
    // is checked
    drp_address = (drp_address & 0x3FFFFFFF) | (1<<30);
    if(write_jesd_phy(jesd_phy, base + JESD_PHY_DRP_ADDR_OFFSET , drp_address)) {
        // Error
        printf("Error while writing to DRP address\n");
        return -1;
    }

    // Poll the access complete bit
    while(read_jesd_phy(jesd_phy, base + JESD_PHY_DRP_ACCESS_STATUS_OFFSET) != 0) {
        usleep(1);
    }

    return read_jesd_phy(jesd_phy, base + JESD_PHY_DRP_READ_DATA_OFFSET);
}

uint32_t read_drp_common(AXI_JESD_PHY* jesd_phy, uint32_t drp_address) {
    return read_drp(jesd_phy, JESD_PHY_DRP_COMMON_BASE, drp_address);
}

uint32_t read_drp_transceiver(AXI_JESD_PHY* jesd_phy, uint32_t drp_address) {
    return read_drp(jesd_phy, JESD_PHY_DRP_TRANCEIVER_BASE, drp_address);
}

uint32_t write_drp_common(AXI_JESD_PHY* jesd_phy, uint32_t drp_address, uint16_t data) {
    return write_drp(jesd_phy, JESD_PHY_DRP_COMMON_BASE, drp_address, data);
}

uint32_t write_drp_transceiver(AXI_JESD_PHY* jesd_phy, uint32_t drp_address, uint16_t data) {
    return write_drp(jesd_phy, JESD_PHY_DRP_TRANCEIVER_BASE, drp_address, data);
}

uint32_t write_jesd_phy_drp_interface_selector(AXI_JESD_PHY* jesd_phy, uint32_t interface) {
    return write_jesd_phy(jesd_phy, JESD_PHY_GT_INTERFACE_SELECTOR, interface);
}

uint32_t read_jesd_phy_drp_interface_selector(AXI_JESD_PHY* jesd_phy) {
    return read_jesd_phy(jesd_phy, JESD_PHY_GT_INTERFACE_SELECTOR);
}

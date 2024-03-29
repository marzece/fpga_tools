#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "jesd.h"

#define NUM_CHANNELS                         4
#define JESD_VERSION_OFFSET                  0x0
#define JESD_RESET_OFFSET                    0x4
#define JESD_ILA_SUPPORT_OFFSET              0x8
#define JESD_SCRAMBLING_OFFSET               0xC
#define JESD_SYSREF_HANDLING_OFFSET          0x10
#define JESD_TEST_MODE_OFFSET                0x18
#define JESD_LINK_STATUS_OFFSET              0x1C
#define JESD_OCTETS_PER_FRAME_OFFSET         0x20
#define JESD_FRAMES_PER_MULTIFRAME_OFFSET    0x24
#define JESD_LANES_IN_USE_OFFSET             0x28
#define JESD_SUBCLASS_MODE_OFFSET            0x2C
#define JESD_RX_BUFFER_DELAY_OFFSET          0x30
#define JESD_ERROR_REPORTING_OFFSET          0x34
#define JESD_SYNC_STATUS_OFFSET              0x38
#define JESD_DEBUG_STATUS_OFFSET             0x3C

# define JESD_ILA_BASE                       0x800
# define JESD_ILA_WIDTH                      0x40
#define JESD_ILA_ERROR_COUNT_OFFSET          0x24

// SYNC STATUS BIT DEFNs
#define JESD_SYNC_STATUS_SYNC_BIT 0x1
#define JESD_SYNC_STATUS_SYSREF_BIT 0x10000

int read_addr(uint32_t, uint32_t, uint32_t*);
int double_read_addr(uint32_t, uint32_t, uint32_t*);
int write_addr(uint32_t, uint32_t, uint32_t);

static uint32_t jesd_ila_offset(unsigned int channel) {
    if(channel >= NUM_CHANNELS) {
        printf("Bad channel given to jesd_ila_offset: %u\n", channel);
        return 0;
    }
    return JESD_ILA_BASE + JESD_ILA_WIDTH*channel;
}

// Put this in a union to gurantee the data is packed contiguously?
// Idk if that actuall works....I should probably just not do this.
struct ILA_Config_Data {
    union {
        struct {
            uint32_t ila_config[8];
            uint32_t test_mode_error_count;
            uint32_t link_error_count;
            uint32_t test_mode_ila_count;
            uint32_t test_mode_multiframe_count;
            uint32_t buffer_adjust;
        };
        uint32_t data[13];
    }; 
};


AXI_JESD* new_jesd(const char* name, uint32_t axi_addr) {
    AXI_JESD* ret = malloc(sizeof(AXI_JESD));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_jesd(AXI_JESD* jesd, uint32_t offset) {
    uint32_t ret;
    if(double_read_addr(jesd->axi_addr, offset, &ret)) {
        // TODO!
        return -1;
    }
    return ret;
}

uint32_t write_jesd(AXI_JESD* jesd, uint32_t offset, uint32_t data) {
    return write_addr(jesd->axi_addr, offset, data);
}

uint32_t jesd_reset(AXI_JESD* jesd) {
    return write_jesd(jesd, JESD_RESET_OFFSET, 0x1);
}

uint32_t set_sync_error_reporting(AXI_JESD* jesd, uint32_t state) {
    const int ERROR_SYNC_REPORT_ENABLE = 0x100;
    uint32_t error_reporting = read_jesd(jesd, JESD_ERROR_REPORTING_OFFSET);
    if(state) {
        error_reporting |= ERROR_SYNC_REPORT_ENABLE;
    }
    else {
        error_reporting &= 0xFFFEFFFF;
    }
    return write_jesd(jesd, JESD_ERROR_REPORTING_OFFSET, error_reporting);
}

uint32_t read_error_register(AXI_JESD* jesd, unsigned int channel) {
        uint32_t ila_base = jesd_ila_offset(channel);
        return read_jesd(jesd, ila_base + JESD_ILA_ERROR_COUNT_OFFSET);
}

uint32_t jesd_error_count(AXI_JESD* jesd, uint32_t *resp, int enable_error_reporting) {
    int i;
    const int ERROR_COUNTING_ENABLE = 0x1;
    //const int ERROR_SYNC_REPORT_ENABLE = 0x100;
    if(enable_error_reporting) {
        uint32_t error_reporting = read_jesd(jesd, JESD_ERROR_REPORTING_OFFSET);
        if(!(error_reporting & ERROR_COUNTING_ENABLE)) {
            write_jesd(jesd, JESD_ERROR_REPORTING_OFFSET, error_reporting | ERROR_COUNTING_ENABLE);
        }
    }

    for(i=0; i < NUM_CHANNELS; i++) {
        resp[i] = read_error_register(jesd, i);
    }
    return 0;
}

uint32_t jesd_read_error_rate(AXI_JESD* jesd, uint32_t *resp) {
    int i;
    const int ERROR_COUNTING_ENABLE = 0x1;
    uint32_t first[4];
    uint32_t second[4];
    //const int ERROR_SYNC_REPORT_ENABLE = 0x100;
    uint32_t error_reporting = read_jesd(jesd, JESD_ERROR_REPORTING_OFFSET);
    if(!(error_reporting & ERROR_COUNTING_ENABLE)) {
        write_jesd(jesd, JESD_ERROR_REPORTING_OFFSET, error_reporting | ERROR_COUNTING_ENABLE);
    }
    jesd_error_count(jesd, first, 0);
    usleep(500e3);
    jesd_error_count(jesd, second, 0);

    for(i=0; i < NUM_CHANNELS; i++) {
        // Don't need to worry aobut rollover, since everything here is a 32-bit
        // unsigned int the subtraction will work even across a rolloever
        resp[i] = second[i] - first[i];
    }
    return 0;
}

uint32_t jesd_is_synced(AXI_JESD* jesd) {
    uint32_t val = read_jesd(jesd, JESD_SYNC_STATUS_OFFSET);
    return (val & JESD_SYNC_STATUS_SYNC_BIT);
}

uint32_t write_jesd_buffer_delay(AXI_JESD* jesd, uint32_t value) {
    return write_jesd(jesd, JESD_RX_BUFFER_DELAY_OFFSET, value);
}

uint32_t read_jesd_buffer_delay(AXI_JESD* jesd) {
    return read_jesd(jesd, JESD_RX_BUFFER_DELAY_OFFSET);
}

struct ILA_Config_Data read_ila_config(AXI_JESD* jesd, unsigned int channel) {
    int i;
    struct ILA_Config_Data ila_config;
    memset(&ila_config, 0, sizeof(struct ILA_Config_Data));

    uint32_t base = jesd_ila_offset(channel);
    if(!base) {
        return ila_config;
    }
    for(i=0; i < 13; i++) {
        ila_config.data[i] = read_jesd(jesd, base + 0x4*i);
    }
    return ila_config;
}

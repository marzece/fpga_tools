#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "clock_wiz.h"

int read_addr(uint32_t, uint32_t, uint32_t*);
int double_read_addr(uint32_t, uint32_t, uint32_t*);
int write_addr(uint32_t, uint32_t, uint32_t);

#define WIDTH_BETWEEN_CLOCKS    12
#define RESET_OFFSET            0x0
#define RESET_VALUE             0xA
#define DIVIDER_OFFSET          0x208
#define PHASE_OFFSET            0x20C
#define DUTY_OFFSET             0x210
#define CLOCK_CHANGES_OFFSET    0x25C

AXI_CLOCK_WIZ* new_clock_wiz(const char* name, uint32_t axi_addr) {
    AXI_CLOCK_WIZ* ret = malloc(sizeof(AXI_CLOCK_WIZ));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t clock_wiz_read_phase(AXI_CLOCK_WIZ *wiz, int which_clock) {
    uint32_t offset = PHASE_OFFSET + which_clock*WIDTH_BETWEEN_CLOCKS;
    uint32_t ret;
    if(double_read_addr(wiz->axi_addr, offset, &ret)) {
        // TODO handle errors better
        return -1;
    }
    return ret;
}

uint32_t clock_wiz_write_phase(AXI_CLOCK_WIZ* wiz, int which_clock, uint32_t value) {
    uint32_t offset = PHASE_OFFSET + which_clock*WIDTH_BETWEEN_CLOCKS;
    return write_addr(wiz->axi_addr, offset, value);
}

uint32_t clock_wiz_register_changes(AXI_CLOCK_WIZ* wiz) {
    return write_addr(wiz->axi_addr, CLOCK_CHANGES_OFFSET, 3);
}

uint32_t clock_wiz_reset(AXI_CLOCK_WIZ* wiz) {
    return write_addr(wiz->axi_addr, RESET_OFFSET, RESET_VALUE);
}

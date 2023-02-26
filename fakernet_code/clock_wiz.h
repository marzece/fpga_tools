#ifndef __CLOCK_WIZ__
#define __CLOCK_WIZ__
#include <inttypes.h>
#include <stdlib.h>

typedef struct AXI_CLOCK_WIZ {
    const char* name;
    uint32_t axi_addr;
} AXI_CLOCK_WIZ;

AXI_CLOCK_WIZ* new_clock_wiz(const char* name, uint32_t axi_addr);
uint32_t clock_wiz_read_phase(AXI_CLOCK_WIZ* wiz, int which_clock);
uint32_t clock_wiz_write_phase(AXI_CLOCK_WIZ* wiz, int which_clock, uint32_t value);
uint32_t clock_wiz_register_changes(AXI_CLOCK_WIZ* wiz);
uint32_t clock_wiz_reset(AXI_CLOCK_WIZ* wiz);
uint32_t read_clock_wiz_reg(AXI_CLOCK_WIZ* wiz, uint32_t offset);
#endif

#ifndef __RESET_GEN_IF__
#define __RESET_GEN_IF__
#include <inttypes.h>
#include <stdlib.h>

typedef struct AXI_RESET_GEN {
    const char* name;
    uint32_t axi_addr;
} AXI_RESET_GEN;

AXI_RESET_GEN* new_reset_gen(const char* name, uint32_t axi_addr);
uint32_t write_reset_gen_length(AXI_RESET_GEN *reset_gen, uint32_t length);
uint32_t read_reset_gen_length(AXI_RESET_GEN *reset_gen);
uint32_t write_reset_gen_mask(AXI_RESET_GEN *reset_gen, uint32_t mask);
uint32_t read_reset_gen_mask(AXI_RESET_GEN *reset_gen);
uint32_t reset_gen_do_reset(AXI_RESET_GEN *reset_gen);
#endif


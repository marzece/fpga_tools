#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "reset_gen_if.h"

#define RESET_GEN_DO_RESET_OFFSET   0x0
#define RESET_GEN_LENGTH_OFFSET     0x8
#define RESET_GEN_MASK_OFFSET       0x4

int read_addr(uint32_t, uint32_t, uint32_t*);
int double_read_addr(uint32_t, uint32_t, uint32_t*);
int write_addr(uint32_t, uint32_t, uint32_t);

AXI_RESET_GEN* new_reset_gen(const char* name, uint32_t axi_addr) {
    AXI_RESET_GEN* ret = malloc(sizeof(AXI_RESET_GEN));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_reset_gen(uint32_t addr, uint32_t offset) {
    uint32_t ret;
    if(double_read_addr(addr, offset, &ret)) {
        // TODO add better error reporting
        return -1;
    }
    return ret;
}

uint32_t write_reset_gen(uint32_t addr, uint32_t offset, uint32_t value) {
    return write_addr(addr, offset, value);
}

uint32_t write_reset_gen_length(AXI_RESET_GEN *reset_gen, uint32_t length) {
    return write_addr(reset_gen->axi_addr, RESET_GEN_LENGTH_OFFSET, length);
}

uint32_t read_reset_gen_length(AXI_RESET_GEN *reset_gen) {
    return read_reset_gen(reset_gen->axi_addr, RESET_GEN_LENGTH_OFFSET);
}

uint32_t write_reset_gen_mask(AXI_RESET_GEN *reset_gen, uint32_t mask) {
    return write_reset_gen(reset_gen->axi_addr, RESET_GEN_MASK_OFFSET, mask);
}

uint32_t read_reset_gen_mask(AXI_RESET_GEN *reset_gen) {
    return read_reset_gen(reset_gen->axi_addr, RESET_GEN_MASK_OFFSET);
}

uint32_t reset_gen_do_reset(AXI_RESET_GEN *reset_gen) {
    return write_reset_gen(reset_gen->axi_addr, RESET_GEN_DO_RESET_OFFSET, 1);
}

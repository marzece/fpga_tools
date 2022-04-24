#ifndef __LMK_IF__
#define __LMK_IF__
#include <inttypes.h>
#include <stdlib.h>
#include "axi_qspi.h"

AXI_QSPI* new_lmk_spi(const char* name, uint32_t axi_addr);
int write_lmk_if(AXI_QSPI* qspi, uint32_t offset, uint32_t data);
uint32_t read_lmk_if(AXI_QSPI* qspi, uint32_t offset);
uint32_t write_lmk_spi(AXI_QSPI* lmk, uint32_t rw, uint32_t addr, uint32_t data);
// High(er) level function
uint32_t clear_pll2_dld_status_reg(AXI_QSPI* lmk);
uint32_t clear_pll1_dld_status_reg(AXI_QSPI* lmk);
uint32_t read_pll2_dld_status_reg(AXI_QSPI* lmk);
uint32_t read_pll1_dld_status_reg(AXI_QSPI* lmk);
uint32_t read_lmk_dac(AXI_QSPI* lmk);
uint32_t write_lmk_dac(AXI_QSPI* lmk, uint32_t value);
#endif

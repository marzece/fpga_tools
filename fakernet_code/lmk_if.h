#ifndef __LMK_IF__
#define __LMK_IF__
#include <inttypes.h>
#include <stdlib.h>
#include "axi_qspi.h"

AXI_QSPI* new_lmk_spi(const char* name, uint32_t axi_addr);
int write_lmk_if(AXI_QSPI* qspi, uint32_t offset, uint32_t data);
uint32_t read_lmk_if(AXI_QSPI* qspi, uint32_t offset);
#endif

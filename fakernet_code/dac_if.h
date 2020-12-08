#ifndef __DAC_IF__
#define __DAC_IF__
#include <inttypes.h>
#include <stdlib.h>
#include "axi_qspi.h"

typedef enum DAC_COMMAND {
    NO_OP = 0x0,
    UPDATE_INPUT_REG = 0x1,
    UPDATE_DAC_REG = 0x2,
    UPDATE_INPUT_AND_DAC_REG = 0x3,
    POWER_DOWN = 0x4,
    UPDATE_LDAC_MASK = 0x5,
    RESET =  0x6,
    REFERENCE_DISABLE =  0x7,
    DAISYCHAIN_ENABLE =  0x8,
    READ_BACK_ENABLE =  0x9,
    UPDATE_ALL_INPUT =  0xa,
    UPDATE_ALL_INPUT_AND_DAC =  0xb,
    DC_NO_OP =  0xF,
} DAC_COMMAND;

AXI_QSPI* new_dac_spi(const char* name, uint32_t axi_addr);
int write_dac_if(AXI_QSPI* qspi, uint32_t offset, uint32_t data);
uint32_t read_dac_if(AXI_QSPI* qspi, uint32_t offset);
uint32_t write_dac_spi(AXI_QSPI* qspi, uint8_t command, uint8_t channels, uint16_t data);

#endif

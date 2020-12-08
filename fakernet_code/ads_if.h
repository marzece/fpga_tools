#include <inttypes.h>
#include <stdlib.h>
#include "axi_qspi.h"

uint32_t read_ads_if(AXI_QSPI* adc, uint32_t offset);
int write_ads_if(AXI_QSPI* adc, uint32_t offset, uint32_t data);
uint32_t write_adc_spi(AXI_QSPI* qspi, uint32_t* args);


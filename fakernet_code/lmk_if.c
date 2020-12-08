#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include "lmk_if.h"
#include "axi_qspi.h"

//static AXI_QSPI* lmk_axi_qspi = NULL;
//
//static AXI_QSPI* get_spi_handle() {
//    if(!lmk_axi_qspi) {
//        lmk_axi_qspi = malloc(sizeof(AXI_QSPI));
//        *lmk_axi_qspi = new_axi_qspi("lmk_if", LMK_IF_BASE);
//        // Initialze CR values
//        lmk_axi_qspi->spi_cr.master=1;
//    }
//    return lmk_axi_qspi;
//}
//
//// Will I ever actually need to clean up my memory? Probably not
//void clean_up() {
//    free(get_spi_handle());
//    lmk_axi_qspi = NULL;
//}

int write_lmk_if(AXI_QSPI* qspi, uint32_t offset, uint32_t data) {
    return write_qspi_addr(qspi, offset, data);
}

uint32_t read_lmk_if(AXI_QSPI* qspi, uint32_t offset) {
    return read_qspi_addr(qspi, offset);
}


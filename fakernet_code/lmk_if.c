#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include "lmk_if.h"
#include "axi_qspi.h"

// TODO this should be removed once an error string is used
// instead of printf
#include <stdio.h>

AXI_QSPI* new_lmk_spi(const char* name, uint32_t axi_addr) {
    AXI_QSPI *lmk_axi_qspi = new_axi_qspi(name, axi_addr);
    // Initialze CR values
    lmk_axi_qspi->spi_cr.master=1;
    return lmk_axi_qspi;
}

int write_lmk_if(AXI_QSPI* qspi, uint32_t offset, uint32_t data) {
    return write_qspi_addr(qspi, offset, data);
}

uint32_t read_lmk_if(AXI_QSPI* qspi, uint32_t offset) {
    return read_qspi_addr(qspi, offset);
}

// This write a single data byte to the LMK chip at the given register address/
// If RW is 1 a read operation is performed, if RW is 0  a write operation is done
uint32_t write_lmk_spi(AXI_QSPI* lmk, uint32_t rw, uint32_t addr, uint32_t data) {
    // RW is 3 bits and the bottom bits are always zero (pretty sure)
    // so RW only has two valid values.
    rw = rw ? 0x4 : 0x0;
    addr = addr & 0x1FFF;
    data = data & 0xFF;

    uint8_t word1 = (rw << 5) | (addr >> 8);
    uint8_t word2 = addr & 0xFF;
    uint8_t word3 = data & 0xFF;

    uint8_t word_buf[3] = {word1, word2, word3};
    // Bit 0 of the SSR is the LMK select line, pull it low to start SPI data transfer
    uint32_t ssr = 0x0;
    write_spi(lmk, ssr, word_buf, 3);
    return 0;
}

// TODO this function should probably be implmented in axi_qspi.c instead of here
uint32_t write_then_read_lmk_spi(AXI_QSPI* lmk, uint32_t rw, uint32_t addr, uint32_t data) {

    int loop_count =0;
    write_lmk_spi(lmk, rw, addr, data);

    // Bit 2 of the status register is 1 when the TX fifo is empty...
    uint32_t status;
    for(loop_count =0; loop_count<20;loop_count++) {
        status = read_qspi_status(lmk);
        if((status & (1<<2)) != 0 ) {
            break;
        }
        usleep(10000);
    }
    if((status & (1<<2)) == 0 ) {
        // TODO SHOULD NOT BE PRINTF-ing HERE!
        printf("ERROR: SPI write took too long to complete\n");
        return -1;
    }

    uint32_t ret;
    // Each LMK write consists of 3 8-bit writes...so I need to do 3 reads from the
    // readback FIFO
    ret = spi_drr_pop(lmk);
    ret = ret << 8;
    usleep(1000);
    ret |= spi_drr_pop(lmk);
    ret = ret << 8;
    usleep(1000);
    ret |= spi_drr_pop(lmk);
    return ret;
}

// High(er) Level Functioons
uint32_t read_pll1_dld_status_reg(AXI_QSPI* lmk) {
    const uint32_t pll1_status_register = 0x182;
    return write_then_read_lmk_spi(lmk, 1, pll1_status_register, 0);
}

uint32_t read_pll2_dld_status_reg(AXI_QSPI* lmk) {
    const uint32_t pll2_status_register = 0x183;
    return write_then_read_lmk_spi(lmk, 1, pll2_status_register, 0);
}

uint32_t clear_pll1_dld_status_reg(AXI_QSPI* lmk) {
    const uint32_t pll1_status_register = 0x182;
    write_then_read_lmk_spi(lmk, 0, pll1_status_register, 1);
    usleep(5000);
    // For some reason you have to do a read between the writes.
    // I don't know why, it doesn't make sense, but it makes things work...
    write_then_read_lmk_spi(lmk, 1 , pll1_status_register, 0);
    usleep(5000);
    return write_then_read_lmk_spi(lmk, 0, pll1_status_register, 0);
}

uint32_t clear_pll2_dld_status_reg(AXI_QSPI* lmk) {
    const uint32_t pll2_status_register = 0x183;
    write_then_read_lmk_spi(lmk, 0, pll2_status_register, 1);
    usleep(5000);
    // For some reason you have to do a read between the writes.
    // I don't know why, it doesn't make sense, but it makes things work...
    write_then_read_lmk_spi(lmk, 1 , pll2_status_register, 0);
    usleep(5000);
    return write_then_read_lmk_spi(lmk, 0, pll2_status_register, 0);
}

uint32_t read_lmk_dac(AXI_QSPI* lmk) {
    const uint32_t rb_dac_reg_1 = 0x184;
    const uint32_t rb_dac_reg_2 = 0x185;
    uint32_t a = write_then_read_lmk_spi(lmk, 1, rb_dac_reg_1, 0);
    uint32_t b = write_then_read_lmk_spi(lmk, 1, rb_dac_reg_2, 0);
    return (((a& 0xC0) << 2) | (b &0xFF));
}

uint32_t write_lmk_dac(AXI_QSPI* lmk, uint32_t value) {
    const uint32_t man_dac_reg1 = 0x14B;
    const uint32_t man_dac_reg2 = 0x14C;
    uint32_t initial_value, reg1_value, reg2_value;
    // First need to readback the value in 0x14B, b/c only the bottom 2 bits are for the DAC,
    // want to leave the other bits untouched
    initial_value  = write_then_read_lmk_spi(lmk, 1, man_dac_reg1, 0);
    initial_value &= 0xFC;

    reg1_value = initial_value | ((value & 0x300) >> 8);
    reg2_value = value & 0xFF;

    write_then_read_lmk_spi(lmk, 0, man_dac_reg1, reg1_value);
    write_then_read_lmk_spi(lmk, 0, man_dac_reg2, reg2_value);
    return 0;
}

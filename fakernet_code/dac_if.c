#include <inttypes.h>
#include <stdlib.h>
#include "dac_if.h"

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

// HERMES Specific...TODO this should live in its ow HEREMES specific code
uint32_t OCM_CHANNELS[8] = {0, 1, 2, 3, 8, 9, 10, 11};
uint32_t BIAS_CHANNELS[8] = {7, 6, 5, 4, 12, 13, 14, 15};

static AXI_QSPI* dac_axi_qspi = NULL;
static AXI_QSPI* get_spi_handle() {
    if(!dac_axi_qspi) {
        dac_axi_qspi = malloc(sizeof(AXI_QSPI));
        *dac_axi_qspi = new_axi_qspi("dac_if", DAC_IF_BASE);

        // Initialize CR values. The DAC uses falling edges so set the CPOL
        dac_axi_qspi->spi_cr.cpol = 1;
        dac_axi_qspi->spi_cr.master = 1;

        // Maybe I should do a "toggle reset" thing here?
        // For some reason the first spi I send after power up doesn't work.
        // I think it has something to do with switching to CPOL=1 or someting
        // like that...idk. But doing a dummy initial SPI transaction might be
        // a solution to that.
    }
    return dac_axi_qspi;
}

int write_dac_if(uint32_t offset, uint32_t data) {
    return write_qspi_addr(get_spi_handle(), offset, data);
}

uint32_t read_dac_if(uint32_t offset) {
    return read_qspi_addr(get_spi_handle(), offset);
}

uint32_t write_dac_if_command(uint32_t *args) {
    return write_dac_if(args[0], args[1]);
}

uint32_t read_dac_if_command(uint32_t *args) {
    return read_dac_if(args[0]);
}

uint32_t write_dac_spi(uint8_t command, uint8_t channels, uint16_t data) {
    uint32_t word1 = (command<<4) | channels;
    uint32_t word2 = data >> 8;
    uint32_t word3 = data & 0xFF;

    uint8_t word_buf[3] = {word1, word2, word3};
    uint32_t ssr = 0x6;
    return write_spi(get_spi_handle(), ssr, word_buf, 3);
}

uint32_t write_dac_spi_command(uint32_t* args) {
    uint32_t command = args[0];
    uint32_t channels = args[1];
    uint32_t data = args[2];

    // First 4 bits are the "command"
    // Second 4 bits is the channel mask
    // Final 16 bits are the actual data
    command &= 0xF;
    channels &= 0xF;
    data &= 0xFFFF;
    return write_dac_spi(command, channels, data);
}

uint32_t toggle_dac_ldac_command(uint32_t* args) {
    (void)args; // Not used
    // This is a bit of hack. But basically I do an 8-bit SPI transaction, but
    // set the SSR reg to toggle it's 2nd bit and not any other bits
    // LDAC is the second bit of the SS (i.e. bit 1)

    uint8_t ssr = 0x05;
    uint8_t word_buf = 0x0;

    return write_spi(get_spi_handle(), ssr, &word_buf, 1);
}

uint32_t toggle_dac_reset_command(uint32_t* args) {
    (void)args; // Not used
    // This is a bit of hack. But basically I do an 8-bit SPI transaction, but
    // set the SSR reg to toggle it's third bit and not any other bits
    // RESET is the third bit of the SS (i.e. bit 2)

    uint8_t ssr = 0x3;
    uint8_t word_buf = 0x0;
    return write_spi(get_spi_handle(), ssr, &word_buf, 1);
}

uint32_t set_ocm_for_channel_command(uint32_t* args) {
    // Arg0 is HERMES channel 0-7. NOT! the DAC channel.
    // Arg1 is the 16-bit DAC value in bits not voltage
    uint32_t channel = args[0];
    uint16_t value = args[1];


    if(channel > 7) {
        // out of range
        return (uint32_t) -1;
    }
    uint8_t dac_channel = OCM_CHANNELS[channel];
    return write_dac_spi(UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
}

uint32_t set_bias_for_channel_command(uint32_t* args) {
    // Arg0 is HERMES channel 0-7. NOT! the DAC channel.
    // Arg1 is the 16-bit DAC value in bits not voltage
    uint32_t channel = args[0];
    uint16_t value = args[1];


    if(channel > 7) {
        // out of range
        return (uint32_t) -1;
    }
    uint8_t dac_channel = BIAS_CHANNELS[channel];
    return write_dac_spi(UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
}

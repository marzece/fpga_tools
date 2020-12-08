#include "hermes_if.h"
#include "gpio.h"
#include "iic.h"
#include "lmk_if.h"
#include "dac_if.h"
#include "ads_if.h"

#define ADC_A_AXI_ADDR 0x2080
#define ADC_B_AXI_ADDR 0x2100
#define GPIO0_AXI_ADDR 0x0000
#define GPIO1_AXI_ADDR 0x1000
//#define GPIO_2_BASE 0x3000
#define LMK_AXI_ADDR 0x4000
#define DAC_AXI_ADDR 0x2180
#define IIC_AXI_ADDR -1 // TODO!
const uint32_t SAFE_READ_ADDRESS = GPIO0_AXI_ADDR;

struct HERMES_IF {
    AXI_QSPI* lmk;
    AXI_QSPI* adc_a;
    AXI_QSPI* adc_b;
    AXI_QSPI* dac;
    AXI_GPIO* gpio0;
    AXI_GPIO* gpio1;
    AXI_GPIO* gpio2;
    AXI_IIC* iic_main;
};
static struct HERMES_IF* hermes = NULL;

// HERMES DAC channel to PMT channel mapping
static uint32_t OCM_CHANNELS[8] = {0, 1, 2, 3, 8, 9, 10, 11};
static uint32_t BIAS_CHANNELS[8] = {7, 6, 5, 4, 12, 13, 14, 15};


static struct HERMES_IF* get_hermes_handle() {
    if(hermes == NULL) {
        hermes = malloc(sizeof(struct HERMES_IF));
        hermes->lmk = new_lmk_spi("lmk", LMK_AXI_ADDR);
        hermes->adc_a = new_ads_spi("adc_a", ADC_A_AXI_ADDR);
        hermes->adc_b = new_ads_spi("adc_b", ADC_B_AXI_ADDR);
        hermes->dac = new_dac_spi("dac", DAC_AXI_ADDR);
        hermes->gpio0 = new_gpio("power_up_cntrl", GPIO0_AXI_ADDR);
        hermes->gpio1 = new_gpio("gpio_xtra", GPIO1_AXI_ADDR);
        hermes->iic_main = new_iic("iic_main", IIC_AXI_ADDR, 0);
    }
    return hermes;
}

ServerCommand hermes_commands[]=  {
    {"read_iic_reg", read_iic_block_command, 1},
    {"write_iic_reg", write_iic_block_command, 2},
    {"read_iic_bus", read_iic_bus_command, 1},
    {"write_iic_bus", write_iic_bus_command, 2},
    {"read_iic_bus_with_reg", read_iic_bus_with_reg_command, 2},
    {"write_iic_bus_with_reg", write_iic_bus_with_reg_command, 3},
    {"read_gpio0", read_gpio_0_command, 1},
    {"write_gpio0", write_gpio_0_command, 2},
    {"read_gpio1", read_gpio_1_command, 1},
    {"write_gpio1", write_gpio_1_command, 2},
    //{"read_gpio2", read_gpio_2_command, 1},
    //{"write_gpio2", write_gpio_2_command, 2},
    {"set_preamp_power", set_preamp_power_command, 1},
    {"set_adc_power", set_adc_power_command, 1},
    {"read_ads_a", read_ads_a_if_command, 1},
    {"write_ads_a", write_ads_a_if_command, 2},
    {"read_ads_b", read_ads_b_if_command, 1},
    {"write_ads_b", write_ads_b_if_command, 2},
    {"write_a_adc_spi", write_a_adc_spi_command, 3},
    {"write_b_adc_spi", write_b_adc_spi_command, 3},
    {"ads_a_spi_data_available", ads_a_spi_data_available_command, 0},
    {"ads_b_spi_data_available", ads_b_spi_data_available_command, 0},
    {"ads_a_spi_pop", ads_a_spi_data_pop_command, 0},
    {"ads_b_spi_pop", ads_b_spi_data_pop_command, 0},
    {"write_lmk_if", write_lmk_if_command, 2},
    {"read_lmk_if", read_lmk_if_command, 1},
    {"write_lmk_spi", write_lmk_spi_command, 3},
    {"lmk_spi_data_available", lmk_spi_data_available_command, 0},
    {"lmk_spi_data_pop", lmk_spi_data_pop_command, 0},
    {"read_dac_if", read_dac_if_command, 1},
    {"write_dac_if", write_dac_if_command, 2},
    {"write_dac_spi", write_dac_spi_command, 3},
    {"set_bias_for_channel", set_bias_for_channel_command, 2},
    {"set_ocm_for_channel", set_ocm_for_channel_command, 2},
    {"toggle_dac_ldac", toggle_dac_ldac_command, 0},
    {"toggle_dac_reset", toggle_dac_reset_command, 0},
    {"", NULL, 0} // Must be last
};

uint32_t write_gpio_0_command(uint32_t *args) {
    return write_gpio_value(get_hermes_handle()->gpio0, args[0], args[1]);
}

uint32_t read_gpio_0_command(uint32_t *args) {
    return read_gpio_value(get_hermes_handle()->gpio0, args[0]);
}

uint32_t write_gpio_1_command(uint32_t *args) {
    return write_gpio_value(get_hermes_handle()->gpio1, args[0], args[1]);
}

uint32_t read_gpio_1_command(uint32_t *args) {
    return read_gpio_value(get_hermes_handle()->gpio1, args[0]);
}

//uint32_t write_gpio_2_command(uint32_t *args) {
//    return write_addr(get_hermes_handle()->gpio2, args[0], args[1]);
//}
//
//uint32_t read_gpio_2_command(uint32_t *args) {
//    return double_read_addr(get_hermes_handle()->gpio2, args[0]);
//}

uint32_t set_adc_power_command(uint32_t *args) {
    uint32_t current_val = read_gpio_value(get_hermes_handle()->gpio0, 0);

    // ADC power is bit 0
    if(args[0]) {
        current_val = current_val | 0b01;
    }
    else {
        current_val = current_val & 0b10;
    }
    write_gpio_value(get_hermes_handle()->gpio0, 0x0, current_val);
    return 0;
}

uint32_t set_preamp_power_command(uint32_t *args) {
    uint32_t current_val = read_gpio_value(get_hermes_handle()->gpio0, 0);

    // pre-amp power is bit 1
    if(args[0]) {
        current_val = current_val | 0b10;
    }
    else {
        current_val = current_val & 0b01;
    }
    write_gpio_value(get_hermes_handle()->gpio0, 0, current_val);
    return 0;
}

// IIC commands
uint32_t read_iic_block_command(uint32_t* args) {
    uint32_t offset = args[0];
    return iic_read(get_hermes_handle()->iic_main, offset);
}

uint32_t write_iic_block_command(uint32_t* args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    int err = iic_write(get_hermes_handle()->iic_main, offset, data);
    return err;
}

uint32_t read_iic_bus_command(uint32_t* args) {
    return read_iic_bus(get_hermes_handle()->iic_main, args[0]);
}

uint32_t write_iic_bus_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t iic_value = args[1];
    return write_iic_bus(get_hermes_handle()->iic_main, iic_addr, iic_value);
}

uint32_t read_iic_bus_with_reg_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t reg_addr = args[1];
    return read_iic_bus_with_reg(get_hermes_handle()->iic_main, iic_addr, reg_addr);
}

uint32_t write_iic_bus_with_reg_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t reg_addr = args[1];
    uint32_t reg_value = args[2];
    return write_iic_bus_with_reg(get_hermes_handle()->iic_main, iic_addr, reg_addr, reg_value); 
}

uint32_t write_ads_a_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_qspi_addr(get_hermes_handle()->adc_a, offset, data);
}

uint32_t read_ads_a_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    return read_qspi_addr(get_hermes_handle()->adc_a, offset);
}

uint32_t write_ads_b_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_qspi_addr(get_hermes_handle()->adc_b, offset, data);
}

uint32_t read_ads_b_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    return read_qspi_addr(get_hermes_handle()->adc_b, offset);
}

uint32_t write_a_adc_spi_command(uint32_t* args) {
    return write_adc_spi(get_hermes_handle()->adc_a, args);
}
uint32_t write_b_adc_spi_command(uint32_t* args) {
    return write_adc_spi(get_hermes_handle()->adc_b, args);
}

uint32_t ads_a_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_hermes_handle()->adc_a);
}

uint32_t ads_a_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_hermes_handle()->adc_a);
}

uint32_t ads_b_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_hermes_handle()->adc_b);
}

uint32_t ads_b_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_hermes_handle()->adc_b);
}

uint32_t write_lmk_if_command(uint32_t* args) {
    return write_lmk_if(get_hermes_handle()->lmk, args[0], args[1]);
}

uint32_t read_lmk_if_command(uint32_t* args) {
    return read_lmk_if(get_hermes_handle()->lmk, args[0]);
}

uint32_t write_lmk_spi_command(uint32_t* args) {
    uint32_t rw = args[0];
    uint32_t addr = args[1];
    uint32_t data = args[2];

    // RW is 3 bits and the bottom bits are always zero (pretty sure)
    // so RW only has two valid values.
    rw = rw ? 0x4 : 0x0;
    addr = addr & 0x1FFF;
    data = data & 0xFF;

    uint32_t word1 = (rw << 5) | (addr >> 8);
    uint32_t word2 = addr & 0xFF;
    uint32_t word3 = data & 0xFF;

    uint8_t word_buf[3] = {word1, word2, word3};
    // Bit 0 of the SSR is the LMK select line, pull it low to start SPI data transfer
    uint32_t ssr = 0x0;
    return write_spi(get_hermes_handle()->lmk, ssr, word_buf, 3);
}

uint32_t lmk_spi_data_available_command(uint32_t* args) {
    (void) args;
    return spi_drr_data_available(get_hermes_handle()->lmk);
}

uint32_t lmk_spi_data_pop_command(uint32_t* args) {
    (void) args;
    return spi_drr_pop(get_hermes_handle()->lmk);
}

uint32_t write_dac_if_command(uint32_t *args) {
    return write_dac_if(get_hermes_handle()->dac, args[0], args[1]);
}

uint32_t read_dac_if_command(uint32_t *args) {
    return read_dac_if(get_hermes_handle()->dac, args[0]);
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
    return write_dac_spi(get_hermes_handle()->dac, command, channels, data);
}

uint32_t toggle_dac_ldac_command(uint32_t* args) {
    (void)args; // Not used
    // This is a bit of hack. But basically I do an 8-bit SPI transaction, but
    // set the SSR reg to toggle it's 2nd bit and not any other bits
    // LDAC is the second bit of the SS (i.e. bit 1)

    uint8_t ssr = 0x05;
    uint8_t word_buf = 0x0;

    return write_spi(get_hermes_handle()->dac, ssr, &word_buf, 1);
}

uint32_t toggle_dac_reset_command(uint32_t* args) {
    (void)args; // Not used
    // This is a bit of hack. But basically I do an 8-bit SPI transaction, but
    // set the SSR reg to toggle it's third bit and not any other bits
    // RESET is the third bit of the SS (i.e. bit 2)

    uint8_t ssr = 0x3;
    uint8_t word_buf = 0x0;
    return write_spi(get_hermes_handle()->dac, ssr, &word_buf, 1);
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
    return write_dac_spi(get_hermes_handle()->dac, UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
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
    return write_dac_spi(get_hermes_handle()->dac, UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
}

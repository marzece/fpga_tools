#include <assert.h>
#include "hermes_if.h"
#include "gpio.h"
#include "iic.h"
#include "lmk_if.h"
#include "dac_if.h"
#include "ads_if.h"
#include "jesd.h"
#include "data_pipeline.h"

#define  ADC_A_AXI_ADDR          0x0000
#define  ADC_B_AXI_ADDR          0x10000
#define  GPIO0_AXI_ADDR          0x1000
#define  GPIO1_AXI_ADDR          0x2000
#define  GPIO2_AXI_ADDR          0x3000
#define  GPIO3_AXI_ADDR          0x4000
#define  GPIO4_AXI_ADDR          0xB000
#define  LMK_AXI_ADDR            0x6000
#define  DAC_AXI_ADDR            0x7000
#define  IIC_AXI_ADDR            0x5000
#define  JESD_A_AXI_ADDR         0x9000
#define  JESD_B_AXI_ADDR         0xA000
#define  DATA_PIPELINE_0_ADDR    0x8000

const uint32_t HERMES_SAFE_READ_ADDRESS = GPIO0_AXI_ADDR;

struct HERMES_IF {
    AXI_QSPI* lmk;
    AXI_QSPI* adc_a;
    AXI_QSPI* adc_b;
    AXI_QSPI* dac;
    AXI_GPIO* gpio0;
    AXI_GPIO* gpio1;
    AXI_GPIO* gpio2;
    AXI_GPIO* gpio3;
    AXI_GPIO* gpio4;
    AXI_IIC* iic_main;
    AXI_JESD* jesd_a;
    AXI_JESD* jesd_b;
    AXI_DATA_PIPELINE* pipeline;
};

static struct HERMES_IF* hermes = NULL;

// HERMES DAC channel to PMT channel mapping
static uint32_t OCM_CHANNELS[8] = {0, 1, 2, 3, 8, 9, 10, 11};
static uint32_t BIAS_CHANNELS[8] = {7, 6, 5, 4, 12, 13, 14, 15};

const char* debug_bits[] = {
"JESD-A TVALID",
"BIG_AXIS_COPY_0 IN TREADY",
"BIG_AXIS_COPY_0 OUT_A TVALID",
"DC HIGH",
"BIG_AXIS_COPY_0 OUT_B TVALID",
"BIG_AXIS_COPY_3 IN TREADY",
"JESD_DATA_MERGER_0 OUT TVALID",
"DC HIGH",
"BIG_AXIS_COPY_3 OUT_A TVALID",
"produce_trigger_vars_0 IN TREADY",
"BIG_AXIS_COPY_3 OUT_B TVALID",
"produce_trigger_vars_0 IN16 TREADY",
"BIG_AXIS_COPY_2 OUT_A TVALID",
"produce_trigger_vars_0 IN17 TREADY",
"BIG_AXIS_COPY_2 OUT_B TVALID",
"produce_trigger_vars_0 IN18 TREADY",
"produce_trigger_vars_0 IN19 TVALID",
"AXIS_DWIDTH_CONVERTER TREADY",
"produce_trigger_vars_0 CONTROL_OUT TVALID",
"DC HIGH",
"JESD_B TVALID",
"BIG_AXIS_COPY_1 IN TREADY",
"BIG_AXIS_COPY_1 OUT TVALID",
"DC HIGH",
"BIG_AXIS_COPY_1 OUT_B TVALID",
"BIG_AXIS_COPY_2 in TREADY" };


static struct HERMES_IF* get_hermes_handle() {
    if(hermes == NULL) {
        hermes = malloc(sizeof(struct HERMES_IF));
        hermes->lmk = new_lmk_spi("lmk", LMK_AXI_ADDR);
        hermes->adc_a = new_ads_spi("adc_a", ADC_A_AXI_ADDR);
        hermes->adc_b = new_ads_spi("adc_b", ADC_B_AXI_ADDR);
        hermes->dac = new_dac_spi("dac", DAC_AXI_ADDR);
        hermes->gpio0 = new_gpio("power_up_cntrl", GPIO0_AXI_ADDR);
        hermes->gpio1 = new_gpio("gpio_sync_counter", GPIO1_AXI_ADDR);
        hermes->gpio2 = new_gpio("gpio_reset", GPIO2_AXI_ADDR);
        hermes->gpio3 = new_gpio("gpio_axis_debug", GPIO3_AXI_ADDR);
        hermes->gpio4 = new_gpio("gpio_fifo_fullness", GPIO4_AXI_ADDR);
        hermes->iic_main = new_iic("iic_main", IIC_AXI_ADDR, 1);
        hermes->jesd_a = new_jesd("jesd_a", JESD_A_AXI_ADDR);
        hermes->jesd_b = new_jesd("jesd_b", JESD_B_AXI_ADDR);
        hermes->pipeline = new_data_pipeline_if("dp0", DATA_PIPELINE_0_ADDR);
    }
    return hermes;
}

static uint32_t write_gpio_0_command(uint32_t *args) {
    return write_gpio_value(get_hermes_handle()->gpio0, args[0], args[1]);
}

static uint32_t read_gpio_0_command(uint32_t *args) {
    return read_gpio_value(get_hermes_handle()->gpio0, args[0]);
}

static uint32_t write_gpio_1_command(uint32_t *args) {
    return write_gpio_value(get_hermes_handle()->gpio1, args[0], args[1]);
}

static uint32_t read_gpio_1_command(uint32_t *args) {
    return read_gpio_value(get_hermes_handle()->gpio1, args[0]);
}

uint32_t write_gpio_2_command(uint32_t *args) {
    return write_gpio_value(get_hermes_handle()->gpio2, args[0], args[1]);
}
uint32_t write_gpio_4_command(uint32_t *args) {
    return write_gpio_value(get_hermes_handle()->gpio4, args[0], args[1]);
}
//
uint32_t read_gpio_2_command(uint32_t *args) {
    return read_gpio_value(get_hermes_handle()->gpio2, args[0]);
}
uint32_t read_gpio_4_command(uint32_t *args) {
    return read_gpio_value(get_hermes_handle()->gpio4, args[0]);
}

static uint32_t set_adc_power_command(uint32_t *args) {
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

static uint32_t set_preamp_power_command(uint32_t *args) {
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
static uint32_t read_iic_block_command(uint32_t* args) {
    uint32_t offset = args[0];
    return iic_read(get_hermes_handle()->iic_main, offset);
}

static uint32_t write_iic_block_command(uint32_t* args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    int err = iic_write(get_hermes_handle()->iic_main, offset, data);
    return err;
}

static uint32_t read_iic_bus_command(uint32_t* args) {
    return read_iic_bus(get_hermes_handle()->iic_main, args[0]);
}

static uint32_t write_iic_bus_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t iic_value = args[1];
    return write_iic_bus(get_hermes_handle()->iic_main, iic_addr, iic_value);
}

static uint32_t read_iic_bus_with_reg_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t reg_addr = args[1];
    return read_iic_bus_with_reg(get_hermes_handle()->iic_main, iic_addr, reg_addr);
}

static uint32_t write_iic_bus_with_reg_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t reg_addr = args[1];
    uint32_t reg_value = args[2];
    return write_iic_bus_with_reg(get_hermes_handle()->iic_main, iic_addr, reg_addr, reg_value); 
}


static uint32_t write_ads_a_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_qspi_addr(get_hermes_handle()->adc_a, offset, data);
}

static uint32_t read_ads_a_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    return read_qspi_addr(get_hermes_handle()->adc_a, offset);
}

static uint32_t write_ads_b_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_qspi_addr(get_hermes_handle()->adc_b, offset, data);
}

static uint32_t read_ads_b_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    return read_qspi_addr(get_hermes_handle()->adc_b, offset);
}

static uint32_t write_both_adcs_spi_command(uint32_t* args) {
    uint32_t ret = 0;
    ret |= write_adc_spi(get_hermes_handle()->adc_a, args);
    ret |= write_adc_spi(get_hermes_handle()->adc_b, args);
    return ret;
}

static uint32_t write_a_adc_spi_command(uint32_t* args) {
    return write_adc_spi(get_hermes_handle()->adc_a, args);
}

static uint32_t write_b_adc_spi_command(uint32_t* args) {
    return write_adc_spi(get_hermes_handle()->adc_b, args);
}

static uint32_t ads_a_spi_data_available_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_data_available(get_hermes_handle()->adc_a);
}

static uint32_t ads_a_spi_data_pop_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_pop(get_hermes_handle()->adc_a);
}


static uint32_t ads_b_spi_data_available_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_data_available(get_hermes_handle()->adc_b);
}

static uint32_t ads_b_spi_data_pop_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_pop(get_hermes_handle()->adc_b);
}

static uint32_t write_lmk_if_command(uint32_t* args) {
    return write_lmk_if(get_hermes_handle()->lmk, args[0], args[1]);
}

static uint32_t read_lmk_if_command(uint32_t* args) {
    return read_lmk_if(get_hermes_handle()->lmk, args[0]);
}

static uint32_t write_lmk_spi_command(uint32_t* args) {
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

static uint32_t lmk_spi_data_available_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_data_available(get_hermes_handle()->lmk);
}

static uint32_t lmk_spi_data_pop_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_pop(get_hermes_handle()->lmk);
}

static uint32_t write_dac_if_command(uint32_t *args) {
    return write_dac_if(get_hermes_handle()->dac, args[0], args[1]);
}

static uint32_t read_dac_if_command(uint32_t *args) {
    return read_dac_if(get_hermes_handle()->dac, args[0]);
}


static uint32_t write_dac_spi_command(uint32_t* args) {
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

static uint32_t toggle_dac_ldac_command(uint32_t* args) {
    UNUSED(args);
    // This is a bit of hack. But basically I do an 8-bit SPI transaction, but
    // set the SSR reg to toggle it's 2nd bit and not any other bits
    // LDAC is the second bit of the SS (i.e. bit 1)

    uint8_t ssr = 0x05;
    uint8_t word_buf = 0x0;

    return write_spi(get_hermes_handle()->dac, ssr, &word_buf, 1);
}

static uint32_t toggle_dac_reset_command(uint32_t* args) {
    UNUSED(args);
    // This is a bit of hack. But basically I do an 8-bit SPI transaction, but
    // set the SSR reg to toggle it's third bit and not any other bits
    // RESET is the third bit of the SS (i.e. bit 2)

    uint8_t ssr = 0x3;
    uint8_t word_buf = 0x0;
    return write_spi(get_hermes_handle()->dac, ssr, &word_buf, 1);
}

static uint32_t set_ocm_for_channel_command(uint32_t* args) {
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

uint32_t set_bias_for_all_channels_commands(uint32_t* args) {
    uint16_t value = args[0];
    uint32_t ret = 0;
    int i;
    for(i=0; i < 8; i++) {
        uint8_t dac_channel = BIAS_CHANNELS[i];
        ret |= write_dac_spi(get_hermes_handle()->dac, UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
    }
    return ret;

}
uint32_t set_ocm_for_all_channels_commands(uint32_t* args) {
    uint16_t value = args[0];
    uint32_t ret = 0;
    int i;
    for(i=0; i < 8; i++) {
        uint8_t dac_channel = OCM_CHANNELS[i];
        ret |= write_dac_spi(get_hermes_handle()->dac, UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
    }
    return ret;
}

static uint32_t set_bias_for_channel_command(uint32_t* args) {
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

static uint32_t jesd_a_read_command(uint32_t* args) {
    uint32_t offset = args[0];
    return read_jesd(get_hermes_handle()->jesd_a, offset);
}

static uint32_t jesd_a_write_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_jesd(get_hermes_handle()->jesd_a, offset, data);
}

static uint32_t jesd_b_read_command(uint32_t* args) {
    uint32_t offset = args[0];
    return read_jesd(get_hermes_handle()->jesd_b, offset);
}

static uint32_t jesd_b_write_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_jesd(get_hermes_handle()->jesd_b, offset, data);
}

static uint32_t jesd_a_error_rate_command(uint32_t* args) {
    return jesd_read_error_rate(get_hermes_handle()->jesd_a, args);
}

static uint32_t jesd_b_error_rate_command(uint32_t* args) {
    return jesd_read_error_rate(get_hermes_handle()->jesd_b, args);
}

static uint32_t jesd_a_reset_command(uint32_t* args) {
    UNUSED(args);
    return jesd_reset(get_hermes_handle()->jesd_a);
}

static uint32_t jesd_b_reset_command(uint32_t* args) {
    UNUSED(args);
    return jesd_reset(get_hermes_handle()->jesd_b);
}

static uint32_t jesd_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    uint32_t GPIO_DATA_OFFSET = 0x0;
    // Just toggle the signal up then down...should do a reset
    write_gpio_value(get_hermes_handle()->gpio2, GPIO_DATA_OFFSET, 0x1);
    usleep(200);
    write_gpio_value(get_hermes_handle()->gpio2, GPIO_DATA_OFFSET, 0x0);
    return 0;
}

// This command reads a GPIO block that (in principle) is hooked up to a
// binary counter that counts clock ticks while SYNC is low. 
// Count is reset by JESD SYS Reset
// TODO, copy/pasted the code for A & B...dry it out!
static uint32_t jesd_a_sync_rate_command(uint32_t *args) {
    UNUSED(args);
    uint32_t GPIO_DATA1_ADDR = 0x0;
    uint32_t first = read_gpio_value(get_hermes_handle()->gpio1, GPIO_DATA1_ADDR);
    usleep(500e3);
    uint32_t second = read_gpio_value(get_hermes_handle()->gpio1, GPIO_DATA1_ADDR);
    // Don't need to worry about rollover, subtraction handles it okay even
    // accross the 32-bit rollover.
    return second - first;
}

static uint32_t read_all_error_rates_command(uint32_t* resp) {
    uint32_t first[10];
    uint32_t second[10];
    int i;
    int channel;
    int a_or_b;
    uint32_t GPIO_DATA1_ADDR = 0x0;
    uint32_t GPIO_DATA2_ADDR = 0x4;

    uint32_t* data_arr = first;

    for(i=0; i < 2; i++) {
        for(a_or_b =0; a_or_b < 2; a_or_b++) {
            AXI_JESD* this_jesd = a_or_b ? get_hermes_handle()->jesd_b : get_hermes_handle()->jesd_a;
            uint32_t gpio_data_offset = a_or_b ? GPIO_DATA2_ADDR : GPIO_DATA1_ADDR;
            for(channel=0; channel<4; channel++) {
                data_arr[channel+a_or_b*4] = read_error_register(this_jesd, channel);
            }
            data_arr[8+a_or_b] = read_gpio_value(get_hermes_handle()->gpio1, gpio_data_offset);
        }
        usleep(500e3);
        data_arr = second;
    }
    for(i=0; i<10; i++) {
        resp[i] = second[i] - first[i];
    }
    return 0;
}

// This command reads a GPIO block that (in principle) is hooked up to a
// binary counter that counts clock ticks while SYNC is low. 
// Count is reset by JESD SYS Reset
static uint32_t jesd_b_sync_rate_command(uint32_t *args) {
    UNUSED(args);
    uint32_t GPIO_DATA2_ADDR = 0x4;
    uint32_t first = read_gpio_value(get_hermes_handle()->gpio1, GPIO_DATA2_ADDR);
    usleep(500e3);
    uint32_t second = read_gpio_value(get_hermes_handle()->gpio1, GPIO_DATA2_ADDR);
    // Don't need to worry about rollover, subtraction handles it okay even
    // accross the 32-bit rollover.
    return second - first;
}

static uint32_t jesd_a_is_synced_command(uint32_t *args) {
    UNUSED(args);
    return jesd_is_synced(get_hermes_handle()->jesd_a);
}

static uint32_t jesd_b_is_synced_command(uint32_t *args) {
    UNUSED(args);
    return jesd_is_synced(get_hermes_handle()->jesd_b);
}

static uint32_t jesd_a_set_sync_error_reporting_command(uint32_t* args) {
    uint32_t state = args[0];
    return set_sync_error_reporting(get_hermes_handle()->jesd_a, state);
}

static uint32_t jesd_b_set_sync_error_reporting_command(uint32_t* args) {
    uint32_t state = args[0];
    return set_sync_error_reporting(get_hermes_handle()->jesd_b, state);
}

static uint32_t set_trigger_mode_command(uint32_t* args) {
    uint32_t value = args[0];
    return write_gpio_value(get_hermes_handle()->gpio0, 1, value);
}

static uint32_t read_trigger_mode_command(uint32_t* args) {
    UNUSED(args);
    return read_gpio_value(get_hermes_handle()->gpio0, 1);
}

static uint32_t set_activate_trigger_command(uint32_t* args) {
    uint32_t on_off = args[0];
    return write_gpio_value(get_hermes_handle()->gpio2, 1, on_off);
}

static uint32_t read_pipe_valid_status_command(uint32_t* args) {
    UNUSED(args);
    return read_gpio_value(get_hermes_handle()->gpio3, 0);
}

static uint32_t read_data_pipeline_0_command(uint32_t* args) {
    uint32_t offset =  args[0];
    return read_data_pipeline_value(get_hermes_handle()->pipeline, offset);

}

static uint32_t write_data_pipeline_0_command(uint32_t* args) {
    uint32_t offset = args[0];
    uint32_t value = args[1];
    return write_data_pipeline_value(get_hermes_handle()->pipeline, offset, value);
}

static uint32_t read_data_pipeline_0_threshold_command(uint32_t* args) {
    UNUSED(args);
    return read_threshold(get_hermes_handle()->pipeline);
}

static uint32_t write_data_pipeline_0_threshold_command(uint32_t* args) {
    uint32_t val = args[0];
    return write_threshold(get_hermes_handle()->pipeline, val);
}

static uint32_t read_data_pipeline_0_channel_mask_command(uint32_t* args) {
    UNUSED(args);
    return read_channel_mask(get_hermes_handle()->pipeline);
}

static uint32_t write_data_pipeline_0_channeL_mask_command(uint32_t* args) {
    uint32_t val = args[0];
    return write_channel_mask(get_hermes_handle()->pipeline, val);
}

static uint32_t read_data_pipeline_0_depth_command(uint32_t* args) {
    int channel = args[0];
    return read_channel_depth(get_hermes_handle()->pipeline, channel);

}

static uint32_t write_data_pipeline_0_depth_command(uint32_t* args) {
    int channel = args[0];
    int value = args[1];
    return write_channel_depth(get_hermes_handle()->pipeline, channel, value);

}

static uint32_t read_data_pipeline_0_invalid_count_command(uint32_t* args) {
    UNUSED(args);
    return read_invalid_count(get_hermes_handle()->pipeline);
}

static uint32_t read_data_pipeline_0_status_command(uint32_t* args) {
    UNUSED(args);
    return read_fifo_status_reg(get_hermes_handle()->pipeline);
}

static uint32_t write_data_pipeline_0_reset_command(uint32_t* args) {
    uint32_t mask = args[0];
    return write_reset_reg(get_hermes_handle()->pipeline, mask);
}

ServerCommand hermes_commands[] = {
    {"read_iic_reg", NULL, read_iic_block_command, 2, 1, 0, 0},
    {"write_iic_reg", NULL, write_iic_block_command, 3, 0, 0, 0},
    {"read_iic_bus", NULL, read_iic_bus_command, 2, 1, 0, 0},
    {"write_iic_bus", NULL, write_iic_bus_command, 3, 1, 0, 0},
    {"read_iic_bus_with_reg", NULL, read_iic_bus_with_reg_command, 3, 1, 0, 0},
    {"write_iic_bus_with_reg", NULL, write_iic_bus_with_reg_command, 4, 1, 0, 0},
    {"read_gpio0", NULL, read_gpio_0_command, 2, 1, 0, 0},
    {"write_gpio0", NULL, write_gpio_0_command, 3, 1, 0, 0},
    {"read_gpio1", NULL, read_gpio_1_command, 2, 1, 0, 0},
    {"write_gpio1", NULL, write_gpio_1_command, 3, 1, 0, 0},
    {"read_gpio2", NULL, read_gpio_2_command, 2, 1, 0, 0},
    {"write_gpio2", NULL, write_gpio_2_command, 3, 1, 0, 0},
    {"read_gpio4", NULL, read_gpio_4_command, 2, 1, 0, 0},
    {"write_gpio4", NULL, write_gpio_4_command, 3, 1, 0, 0},
    {"set_preamp_power", NULL, set_preamp_power_command, 2, 1, 0, 0},
    {"set_adc_power", NULL, set_adc_power_command, 2, 1, 0, 0},
    {"read_ads_a", NULL, read_ads_a_if_command, 2, 1, 0, 0},
    {"write_ads_a", NULL, write_ads_a_if_command, 3, 1, 0, 0},
    {"read_ads_b", NULL, read_ads_b_if_command, 2, 1, 0, 0},
    {"write_ads_b", NULL, write_ads_b_if_command, 3, 1, 0, 0},
    {"write_a_adc_spi", NULL, write_a_adc_spi_command, 4, 1, 0, 0},
    {"write_b_adc_spi", NULL, write_b_adc_spi_command, 4, 1, 0, 0},
    {"write_both_adcs_spi", NULL, write_both_adcs_spi_command, 4, 1, 0, 0},
    {"ads_a_spi_data_available", NULL, ads_a_spi_data_available_command, 1, 1, 0, 0},
    {"ads_b_spi_data_available", NULL, ads_b_spi_data_available_command, 1, 1, 0, 0},
    {"ads_a_spi_pop", NULL, ads_a_spi_data_pop_command, 1, 1, 0, 0},
    {"ads_b_spi_pop", NULL, ads_b_spi_data_pop_command, 1, 1, 0, 0},
    {"write_lmk_if", NULL, write_lmk_if_command, 3, 1, 0, 0},
    {"read_lmk_if", NULL, read_lmk_if_command, 2, 1, 0, 0},
    {"write_lmk_spi", NULL, write_lmk_spi_command, 4, 1, 0, 0},
    {"lmk_spi_data_available", NULL, lmk_spi_data_available_command, 1, 1, 0, 0},
    {"lmk_spi_data_pop", NULL, lmk_spi_data_pop_command, 1, 1, 0, 0},
    {"read_dac_if", NULL, read_dac_if_command, 2, 1, 0, 0},
    {"write_dac_if", NULL, write_dac_if_command, 3, 1, 0, 0},
    {"write_dac_spi", NULL, write_dac_spi_command, 4, 1, 0, 0},
    {"set_bias_for_channel", NULL, set_bias_for_channel_command, 3, 1, 0, 0},
    {"set_ocm_for_channel", NULL, set_ocm_for_channel_command, 3, 1, 0, 0},
    {"set_bias_for_all_channels", NULL, set_bias_for_all_channels_commands, 2, 1, 0, 0},
    {"set_ocm_for_all_channels", NULL, set_ocm_for_all_channels_commands, 2, 1, 0, 0},
    {"toggle_dac_ldac", NULL, toggle_dac_ldac_command, 1, 1, 0, 0},
    {"toggle_dac_reset", NULL, toggle_dac_reset_command, 1, 1, 0, 0},
    {"jesd_a_read", NULL, jesd_a_read_command, 2, 1, 0, 0},
    {"jesd_a_write", NULL, jesd_a_write_command, 3, 1, 0, 0},
    {"jesd_b_read", NULL, jesd_b_read_command, 2, 1, 0, 0},
    {"jesd_b_write", NULL, jesd_b_write_command, 3, 1, 0, 0},
    {"jesd_a_error_rate", NULL, jesd_a_error_rate_command, 1, 4, 0, 0},
    {"jesd_b_error_rate", NULL, jesd_b_error_rate_command, 1, 4, 0, 0},
    {"jesd_a_reset", NULL, jesd_a_reset_command, 1, 1, 0, 0},
    {"jesd_b_reset", NULL, jesd_b_reset_command, 1, 1, 0, 0},
    {"jesd_sys_reset", NULL, jesd_sys_reset_command, 1, 1, 0, 0},
    {"jesd_a_sync_rate", NULL, jesd_a_sync_rate_command, 1, 1, 0, 0},
    {"jesd_b_sync_rate", NULL, jesd_b_sync_rate_command, 1, 1, 0, 0},
    {"jesd_a_is_synced", NULL, jesd_a_is_synced_command, 1, 1, 0, 0},
    {"jesd_b_is_synced", NULL, jesd_b_is_synced_command, 1, 1, 0, 0},
    {"jesd_a_set_sync_error_reporting", NULL, jesd_a_set_sync_error_reporting_command, 2, 1, 0, 0},
    {"jesd_b_set_sync_error_reporting", NULL, jesd_b_set_sync_error_reporting_command, 2, 1, 0, 0},
    {"read_all_error_rates", NULL, read_all_error_rates_command, 1, 10, 0, 0},
    {"set_trigger_mode", NULL, set_trigger_mode_command, 2, 1, 0, 0},
    {"read_trigger_mode", NULL, read_trigger_mode_command, 1, 1, 0, 0},
    {"set_activate_trigger", NULL, set_activate_trigger_command, 2, 1, 0, 0},
    {"read_pipe_valid_status", NULL, read_pipe_valid_status_command, 1, 1, 0, 0},
    {"read_data_pipeline_threshold", NULL, read_data_pipeline_0_threshold_command, 1, 1, 0, 0},
    {"write_data_pipeline_threshold", NULL, write_data_pipeline_0_threshold_command, 2, 1, 0, 0},
    {"read_data_pipeline_channel_mask", NULL, read_data_pipeline_0_channel_mask_command, 1, 1, 0, 0},
    {"write_data_pipeline_channeL_mask", NULL, write_data_pipeline_0_channeL_mask_command, 2, 1, 0, 0},
    {"read_data_pipeline_depth", NULL, read_data_pipeline_0_depth_command, 2, 1, 0, 0},
    {"write_data_pipeline_depth", NULL, write_data_pipeline_0_depth_command, 3, 1, 0, 0},
    {"write_data_pipeline_depth", NULL, write_data_pipeline_0_depth_command, 3, 1, 0, 0},
    {"write_data_pipeline", NULL, write_data_pipeline_0_command, 3, 1, 0, 0},
    {"read_data_pipeline", NULL, read_data_pipeline_0_command, 2, 1, 0, 0},
    {"read_data_pipeline_invalid_count", NULL, read_data_pipeline_0_invalid_count_command, 1, 1, 0, 0},
    {"read_data_pipeline_status", NULL, read_data_pipeline_0_status_command, 1, 1, 0, 0},
    {"write_data_pipeline_reset", NULL, write_data_pipeline_0_reset_command, 2, 1, 0, 0},
    {"", NULL, NULL, 0, 0, 0, 0} // Must be last
};

#include <stdlib.h>
#include <assert.h>
#include "server_common.h"
#include "ti_board_if.h"
#include "axi_qspi.h"
#include "ads_if.h"
#include "lmk_if.h"
#include "gpio.h"
#include "iic.h"
#include "jesd.h"
#include "adc_sample_fanout.h"
#include "channel_trigger.h"
#include "dac_if.h"
#include "data_pipeline.h"

#define    LMK_AXI_ADDR            0x3000
#define    ADC_AXI_ADDR            0x3000
#define    DAC_AXI_ADDR            0x2080
#define    IIC_AXI_ADDR            0x2000
#define    GPIO0_AXI_ADDR          0x0000
#define    GPIO1_AXI_ADDR          0x10000
#define    GPIO2_AXI_ADDR          0x1000
#define    JESD_A_AXI_ADDR         0x6000
#define    ADC_A_FANOUT_ADDR       0x30000
#define    CHANNEL_TRIGGER_0_ADDR  0x40000
#define    CHANNEL_TRIGGER_1_ADDR  0x50000
#define    CHANNEL_TRIGGER_2_ADDR  0x60000
#define    CHANNEL_TRIGGER_3_ADDR  0x70000
#define    DATA_PIPELINE_0_ADDR    0x4000
#define    DATA_PIPELINE_1_ADDR    0x5000


struct TI_IF {
    AXI_QSPI* lmk;
    AXI_QSPI* adc;
    AXI_GPIO* gpio0;
    AXI_GPIO* gpio1;
    AXI_GPIO* gpio2;
    AXI_IIC* iic_main;
    AXI_JESD* jesd;
    AXI_ADC_SAMPLE_FANOUT* fanout;
    AXI_DATA_PIPELINE* dp_0;
    AXI_DATA_PIPELINE* dp_1;
    AXI_CHANNEL_TRIGGER* channel_triggers[4];
    AXI_QSPI* dac;
};

struct TI_IF* ti_board = NULL;

// HERMES DAC channel to PMT channel mapping
static uint32_t OCM_CHANNELS[8] = {0, 1, 2, 3, 8, 9, 10, 11};
static uint32_t BIAS_CHANNELS[8] = {7, 6, 5, 4, 12, 13, 14, 15};

const uint32_t TI_SAFE_READ_ADDRESS = IIC_AXI_ADDR + 0x104; // Should be the status register for the iic core

struct TI_IF* get_ti_handle() {
    if(ti_board == NULL) {
        ti_board = malloc(sizeof(struct TI_IF));
        ti_board->lmk = new_lmk_spi("lmk", LMK_AXI_ADDR);
        ti_board->adc = new_ads_spi("adc", ADC_AXI_ADDR);
        ti_board->gpio0 = new_gpio("gpio_jesd_reset", GPIO0_AXI_ADDR);
        ti_board->gpio1 = new_gpio("gpio_sync_counter", GPIO1_AXI_ADDR);
        ti_board->gpio2 = new_gpio("gpio_reset", GPIO2_AXI_ADDR);
        ti_board->iic_main = new_iic("iic_main", IIC_AXI_ADDR, 0);
        ti_board->jesd = new_jesd("jesd", JESD_A_AXI_ADDR);
        ti_board->fanout = new_adc_sample_fanout("fanout", ADC_A_FANOUT_ADDR);
        ti_board->dp_0 = new_data_pipeline_if("data_pipeline_0", DATA_PIPELINE_0_ADDR);
        ti_board->dp_1 = new_data_pipeline_if("data_pipeline_1", DATA_PIPELINE_1_ADDR);
        ti_board->dac = new_dac_spi("dac", DAC_AXI_ADDR);
        ti_board->channel_triggers[0] = new_channel_trigger("channel_trigger_0", CHANNEL_TRIGGER_0_ADDR);
        ti_board->channel_triggers[1] = new_channel_trigger("channel_trigger_1", CHANNEL_TRIGGER_1_ADDR);
        ti_board->channel_triggers[2] = new_channel_trigger("channel_trigger_2", CHANNEL_TRIGGER_2_ADDR);
        ti_board->channel_triggers[3] = new_channel_trigger("channel_trigger_3", CHANNEL_TRIGGER_3_ADDR);
    }

    return ti_board;
}

static uint32_t write_ads_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_qspi_addr(get_ti_handle()->adc, offset, data);
}

static uint32_t read_ads_if_command(uint32_t *args) {
    uint32_t offset = args[0];
    return read_qspi_addr(get_ti_handle()->adc, offset);
}

static uint32_t write_adc_spi_command(uint32_t* args) {
    uint32_t wmpch = args[0];
    uint32_t adc_addr = args[1];
    uint32_t adc_data = args[2];

    wmpch &= 0xF;
    adc_addr &= 0xFFF;
    adc_data &= 0xFF;

    uint8_t word_buf[3];

    word_buf[0] = (wmpch<<4) | (adc_addr >> 8);
    word_buf[1] = adc_addr & 0xFF;
    word_buf[2] = adc_data;

    uint8_t ssr = 0x6;
    return write_spi(get_ti_handle()->adc, ssr, word_buf, 3);
    //return write_adc_spi(get_ti_handle()->adc, args);
}

static uint32_t ads_spi_data_available_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_data_available(get_ti_handle()->adc);
}

static uint32_t ads_spi_data_pop_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_pop(get_ti_handle()->adc);
}

static uint32_t write_lmk_if_command(uint32_t* args) {
    return write_lmk_if(get_ti_handle()->lmk, args[0], args[1]);
}

static uint32_t read_lmk_if_command(uint32_t* args) {
    return read_lmk_if(get_ti_handle()->lmk, args[0]);
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
    uint32_t ssr = 0x5; // for TI board
    //uint32_t ssr = 0x0; //for masada
    return write_spi(get_ti_handle()->lmk, ssr, word_buf, 3);
}

 static uint32_t lmk_spi_data_available_command(uint32_t* args) {
     UNUSED(args);
    return spi_drr_data_available(get_ti_handle()->lmk);
}

static uint32_t lmk_spi_data_pop_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_pop(get_ti_handle()->lmk);
}

static uint32_t turn_on_data_pipe_command(uint32_t* args) {
    UNUSED(args);
    write_fanout_value( get_ti_handle()->fanout, 0xF);
    return 0;
}

static uint32_t set_threshold_for_channel_command(uint32_t *args) {
    uint32_t channel = args[0];
    uint32_t threshold = args[1];
    assert(channel < 4);
    return write_channel_trigger_value(get_ti_handle()->channel_triggers[channel], threshold);
}

static uint32_t read_threshold_for_channel_command(uint32_t *args) {
    uint32_t channel = args[0];
    return read_channel_trigger_value(get_ti_handle()->channel_triggers[channel]);
}

static uint32_t set_trigger_mode_command(uint32_t* args) {
    uint32_t value = args[0];
    return write_gpio_value(get_ti_handle()->gpio0, 1, value);
}

static uint32_t read_trigger_mode_command(uint32_t* args) {
    UNUSED(args);
    return read_gpio_value(get_ti_handle()->gpio0, 1);
}

static uint32_t set_activate_trigger_command(uint32_t* args) {
    uint32_t on_off = args[0];
    return write_gpio_value(get_ti_handle()->gpio2, 1, on_off);
}

static uint32_t set_bias_for_all_channels_commands(uint32_t* args) {
    uint16_t value = args[0];
    uint32_t ret = 0;
    int i;
    for(i=0; i < 8; i++) {
        uint8_t dac_channel = BIAS_CHANNELS[i];
        ret |= write_dac_spi(get_ti_handle()->dac, UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
    }
    return ret;

}

static uint32_t set_ocm_for_all_channels_commands(uint32_t* args) {
    uint16_t value = args[0];
    uint32_t ret = 0;
    int i;
    for(i=0; i < 8; i++) {
        uint8_t dac_channel = OCM_CHANNELS[i];
        ret |= write_dac_spi(get_ti_handle()->dac, UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
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
    return write_dac_spi(get_ti_handle()->dac, UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
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
    return write_dac_spi(get_ti_handle()->dac, UPDATE_INPUT_AND_DAC_REG, dac_channel, value);
}

static uint32_t jesd_read_command(uint32_t* args) {
    uint32_t offset = args[0];
    return read_jesd(get_ti_handle()->jesd, offset);
}

static uint32_t jesd_write_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_jesd(get_ti_handle()->jesd, offset, data);
}

static uint32_t jesd_is_synced_command(uint32_t *args) {
    UNUSED(args);
    return jesd_is_synced(get_ti_handle()->jesd);
}

static uint32_t jesd_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    uint32_t GPIO_DATA_OFFSET = 0x0;
    // Just toggle the signal up then down...should do a reset
    write_gpio_value(get_ti_handle()->gpio2, GPIO_DATA_OFFSET, 0x1);
    usleep(200);
    write_gpio_value(get_ti_handle()->gpio2, GPIO_DATA_OFFSET, 0x0);
    return 0;
}

static uint32_t read_all_error_rates_command(uint32_t* resp) {
    uint32_t first[5];
    uint32_t second[5];
    int i;
    int channel;
    uint32_t GPIO_DATA1_ADDR = 0x0;

    uint32_t* data_arr = first;

    for(i=0; i < 2; i++) {
        AXI_JESD* this_jesd =  get_ti_handle()->jesd;
        for(channel=0; channel<4; channel++) {
            data_arr[channel] = read_error_register(this_jesd, channel);
        }
        data_arr[4] = read_gpio_value(get_ti_handle()->gpio1, GPIO_DATA1_ADDR);
        data_arr = second;
        usleep(500e3);
    }
    for(i=0; i<5; i++) {
        resp[i] = second[i] - first[i];
    }
    return 0;
}

static uint32_t read_data_pipeline_0_command(uint32_t* args) {
    uint32_t offset =  args[0];
    return read_data_pipeline_value(get_ti_handle()->dp_0, offset);

}

static uint32_t write_data_pipeline_0_command(uint32_t* args) {
    uint32_t offset = args[0];
    uint32_t value = args[1];
    return write_data_pipeline_value(get_ti_handle()->dp_0, offset, value);
}

static uint32_t read_data_pipeline_0_threshold_command(uint32_t* args) {
    UNUSED(args);
    return read_threshold(get_ti_handle()->dp_0);
}

static uint32_t write_data_pipeline_0_threshold_command(uint32_t* args) {
    uint32_t val = args[0];
    return write_threshold(get_ti_handle()->dp_0, val);
}

static uint32_t read_data_pipeline_0_channel_mask_command(uint32_t* args) {
    UNUSED(args);
    return read_channel_mask(get_ti_handle()->dp_0);
}

static uint32_t write_data_pipeline_0_channeL_mask_command(uint32_t* args) {
    uint32_t val = args[0];
    return write_channel_mask(get_ti_handle()->dp_0, val);
}

static uint32_t read_data_pipeline_0_depth_command(uint32_t* args) {
    int channel = args[0];
    return read_channel_depth(get_ti_handle()->dp_0, channel);

}

static uint32_t write_data_pipeline_0_depth_command(uint32_t* args) {
    int channel = args[0];
    int value = args[1];
    return write_channel_depth(get_ti_handle()->dp_0, channel, value);

}

static uint32_t read_data_pipeline_0_invalid_count_command(uint32_t* args) {
    UNUSED(args);
    return read_invalid_count(get_ti_handle()->dp_0);
}

static uint32_t read_data_pipeline_0_status_command(uint32_t* args) {
    UNUSED(args);
    return read_fifo_status_reg(get_ti_handle()->dp_0);
}

static uint32_t write_data_pipeline_0_reset_command(uint32_t* args) {
    uint32_t mask = args[0];
    return write_reset_reg(get_ti_handle()->dp_0, mask);
}

ServerCommand ti_commands[] = {
    {"read_ads", read_ads_if_command, 1, 1},
    {"write_ads", write_ads_if_command, 2, 1},
    {"write_adc_spi", write_adc_spi_command, 3, 1},
    {"ads_spi_data_available", ads_spi_data_available_command, 0, 1},
    {"ads_spi_pop", ads_spi_data_pop_command, 0, 1},
    {"write_lmk_if", write_lmk_if_command, 2, 1},
    {"read_lmk_if", read_lmk_if_command, 1, 1},
    {"write_lmk_spi", write_lmk_spi_command, 3, 1},
    {"lmk_spi_data_available", lmk_spi_data_available_command, 0, 1},
    {"lmk_spi_data_pop", lmk_spi_data_pop_command, 0, 1},
    {"turn_on_data_pipe", turn_on_data_pipe_command, 0, 1},
    {"set_threshold_for_channel", set_threshold_for_channel_command, 2, 1},
    {"read_threshold_for_channel", read_threshold_for_channel_command, 1, 1},
    {"set_trigger_mode", set_trigger_mode_command, 1, 1},
    {"read_trigger_mode", read_trigger_mode_command, 0, 1},
    {"set_activate_trigger", set_activate_trigger_command, 1, 1},
    {"set_bias_for_channel", set_bias_for_channel_command, 2, 1},
    {"set_ocm_for_channel", set_ocm_for_channel_command, 2, 1},
    {"set_bias_for_all_channels", set_bias_for_all_channels_commands, 1, 1},
    {"set_ocm_for_all_channels", set_ocm_for_all_channels_commands, 1, 1},
    {"jesd_read", jesd_read_command, 1, 1},
    {"jesd_write", jesd_write_command, 2, 1},
    {"jesd_sys_reset", jesd_sys_reset_command, 0, 1},
    {"jesd_is_synced", jesd_is_synced_command, 0, 1},
    {"read_all_error_rates", read_all_error_rates_command, 0, 5},
    {"read_data_pipeline_0_threshold", read_data_pipeline_0_threshold_command, 0, 1},
    {"write_data_pipeline_0_threshold", write_data_pipeline_0_threshold_command, 1, 1},
    {"read_data_pipeline_0_channel_mask", read_data_pipeline_0_channel_mask_command, 0, 1},
    {"write_data_pipeline_0_channeL_mask", write_data_pipeline_0_channeL_mask_command, 1, 1},
    {"read_data_pipeline_0_depth", read_data_pipeline_0_depth_command, 1, 1},
    {"write_data_pipeline_0_depth", write_data_pipeline_0_depth_command, 2, 1},
    {"write_data_pipeline_0_depth", write_data_pipeline_0_depth_command, 2, 1},
    {"write_data_pipeline_0", write_data_pipeline_0_command, 2, 1},
    {"read_data_pipeline_0", read_data_pipeline_0_command, 1, 1},
    {"read_data_pipeline_0_invalid_count", read_data_pipeline_0_invalid_count_command, 0, 1},
    {"read_data_pipeline_0_status", read_data_pipeline_0_status_command, 0, 1},
    {"write_data_pipeline_0_reset", write_data_pipeline_0_reset_command, 1, 1},
    {"", NULL, 0, 0} // Must be last
};

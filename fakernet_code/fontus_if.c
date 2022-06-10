#include <assert.h>
#include "fontus_if.h"
#include "gpio.h"
#include "iic.h"
#include "lmk_if.h"
#include "dac_if.h"
#include "ads_if.h"
#include "jesd.h"
#include "jesd_phy.h"
#include "reset_gen_if.h"
#include "trigger_pipeline.h"

#define  IIC_AXI_ADDR           0x100000
#define  GPIO0_AXI_ADDR         0x200000
#define  RESET_GEN_AXI_ADDR     0x300000
#define  FONTUS_LMK_AXI_ADDR    0x400000
#define  DAC_AXI_ADDR           0x500000
#define  TRIGGER_PIPELINE_ADDR  0x0

#define RESET_GEN_PIPELINE_BIT 0
#define RESET_GEN_AXI_BIT 1
#define RESET_GEN_AURORA_BIT 2

#define COAX_GPIO_OFFSET 0

const uint32_t FONTUS_SAFE_READ_ADDRESS = TRIGGER_PIPELINE_ADDR;

struct FONTUS_IF {
    AXI_QSPI* lmk;
    AXI_QSPI* dac;
    AXI_RESET_GEN* reset_gen;
    AXI_GPIO* gpio;
    AXI_TRIGGER_PIPELINE* pipeline;
};

static struct FONTUS_IF* fontus = NULL;

/*
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
*/

static struct FONTUS_IF* get_fontus_handle() {
    if(fontus == NULL) {
        fontus = malloc(sizeof(struct FONTUS_IF));
        fontus->lmk = new_lmk_spi("fontus_lmk", FONTUS_LMK_AXI_ADDR);
        fontus->pipeline = new_trig_pipeline_if("trigger_pipeline", TRIGGER_PIPELINE_ADDR);
        fontus->gpio = new_gpio("gpio", GPIO0_AXI_ADDR);
        fontus->dac = new_dac_spi("DAC SPI", DAC_AXI_ADDR);
        fontus->reset_gen = new_reset_gen("Resetter", RESET_GEN_AXI_ADDR);
    }
    return fontus;
}

static uint32_t write_lmk_if_command(uint32_t* args) {
    return write_lmk_if(get_fontus_handle()->lmk, args[0], args[1]);
}

static uint32_t read_lmk_if_command(uint32_t* args) {
    return read_lmk_if(get_fontus_handle()->lmk, args[0]);
}

static uint32_t write_lmk_spi_command(uint32_t* args) {
    uint32_t rw = args[0];
    uint32_t addr = args[1];
    uint32_t data = args[2];

    return write_lmk_spi(get_fontus_handle()->lmk, rw, addr, data);
}

static uint32_t lmk_spi_data_available_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_data_available(get_fontus_handle()->lmk);
}

static uint32_t lmk_spi_data_pop_command(uint32_t* args) {
    UNUSED(args);
    return spi_drr_pop(get_fontus_handle()->lmk);
}
static uint32_t read_lmk_pll_status_command(uint32_t* args) {
    uint32_t which_pll = args[0];
    uint32_t status;
    if(which_pll == 0) {
        status = read_pll1_dld_status_reg(get_fontus_handle()->lmk);
    }
    else {
        status = read_pll2_dld_status_reg(get_fontus_handle()->lmk);
    }
    return status;
}

static uint32_t clear_lmk_pll_status_command(uint32_t* args) {
    uint32_t which_pll = args[0];
    uint32_t ret;
    if(which_pll == 0) {
        ret = clear_pll1_dld_status_reg(get_fontus_handle()->lmk);
    }
    else {
        ret = clear_pll2_dld_status_reg(get_fontus_handle()->lmk);
    }
    return ret;
}
static uint32_t read_lmk_dac_value_command(uint32_t* args) {
    UNUSED(args);
    return read_lmk_dac(get_fontus_handle()->lmk);
}

static uint32_t write_lmk_dac_value_command(uint32_t* args) {
    uint32_t value = args[0];
    return write_lmk_dac(get_fontus_handle()->lmk, value);
}

static uint32_t set_enable_trigger_system_command(uint32_t* args) {
    uint32_t enable = args[0] ? 1 : 0;
    return write_self_trigger_enable(get_fontus_handle()->pipeline, enable);
}

static uint32_t write_delay_value_command(uint32_t* args) {
    // 0 = INNER MULTIPLICITY
    // 1 = INNER ESUM
    // 2 = VETO MULTIPLICITY
    // 3 = VETO ESUM
    // 4 = EXTERNAL-1
    // 5 = EXTERNAL-2
    // 6 = EXTERNAL-3
    uint32_t which_type = args[0];
    uint32_t channel = args[1];
    uint32_t value = args[2];
    switch(which_type) {
        case(0):
            return write_inner_multiplicity_delay(get_fontus_handle()->pipeline, channel, value);
        case(1):
            return write_inner_esum_delay(get_fontus_handle()->pipeline, channel, value);
        case(2):
            return write_veto_multiplicity_delay(get_fontus_handle()->pipeline, channel, value);
            break;
        case(3):
            return write_veto_esum_delay(get_fontus_handle()->pipeline, channel, value);
        case(4):
            //write_external_delay(get_fontus_handle()->pipeline, channel);
            break;
        case(5):
            //write_external_delay(get_fontus_handle()->pipeline, channel + 3);
            break;
        case(6):
            //write_external_delay(get_fontus_handle()->pipeline, channel + 6);
            break;
    }

}

static uint32_t read_delay_value(uint32_t* args) {

    return 0;
}

static uint32_t read_dac_if_command(uint32_t* args) {
    uint32_t offset = args[0];
    return read_dac_if(get_fontus_handle()->dac, offset);
}

static uint32_t write_dac_if_command(uint32_t* args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    return write_dac_if(get_fontus_handle()->dac, offset, data);
}

static uint32_t write_dac_spi_command(uint32_t* args) {
    uint32_t rw = args[0];
    uint32_t addr = args[1];
    uint32_t data = args[2];

    return write_dac_spi(get_fontus_handle()->dac, rw, addr, data);
}

static uint32_t set_threshold_for_channel_command(uint32_t* args) {
    uint8_t channel = args[0];
    uint16_t value = args[1];
    return write_dac_spi(get_fontus_handle()->dac, UPDATE_INPUT_AND_DAC_REG, channel, value);
}

static void generic_sys_reset(uint32_t bit_mask) {
    write_reset_gen_mask(get_fontus_handle()->reset_gen, bit_mask);
    write_reset_gen_length(get_fontus_handle()->reset_gen, 5000);
    reset_gen_do_reset(get_fontus_handle()->reset_gen);
}

static uint32_t pipeline_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_PIPELINE_BIT);
    return 0;
}

static uint32_t aurora_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_AURORA_BIT);
    return 0;
}

static uint32_t axi_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_AXI_BIT);
    return 0;
}

static uint32_t read_sync_length_command(uint32_t* args) {
    UNUSED(args);
    return read_sync_length(get_fontus_handle()->pipeline);
}

static uint32_t do_sync_command(uint32_t* args) {
    uint16_t length = args[0];
    return do_sync(get_fontus_handle()->pipeline, length);
}

static uint32_t write_coax_dir_select_command(uint32_t* args) {
    uint32_t value = args[0];
    return write_gpio_value(get_fontus_handle()->gpio, COAX_GPIO_OFFSET, value);
}

static uint32_t read_coax_dir_select_command(uint32_t* args) {
    UNUSED(args);
    return read_gpio_value(get_fontus_handle()->gpio, COAX_GPIO_OFFSET);
}

ServerCommand fontus_commands[] = {
{"write_lmk_if",NULL,                              write_lmk_if_command,                                   3,  1, 0, 0},
{"read_lmk_if",NULL,                               read_lmk_if_command,                                    2,  1, 0, 0},
{"write_lmk_spi",NULL,                             write_lmk_spi_command,                                  4,  1, 0, 0},
{"lmk_spi_data_available",NULL,                    lmk_spi_data_available_command,                         1,  1, 0, 0},
{"lmk_spi_data_pop",NULL,                          lmk_spi_data_pop_command,                               1,  1, 0, 0},
{"read_lmk_pll_status",NULL,                       read_lmk_pll_status_command,                            2,  1, 0, 0},
{"clear_lmk_pll_status",NULL,                      clear_lmk_pll_status_command,                           2,  1, 0, 0},
{"read_lmk_dac_value",NULL,                        read_lmk_dac_value_command,                             1,  1, 0, 0},
{"write_lmk_dac_value",NULL,                       write_lmk_dac_value_command,                            2,  0, 0, 0},
{"set_enable_trigger_system",NULL,                 set_enable_trigger_system_command,                      2,  0, 0, 0},
{"write_delay_value",NULL,                         write_delay_value_command,                              2,  0, 0, 0},
{"write_dac_if",NULL,                              write_dac_if_command,                                   3,  0, 0, 0}, 
{"read_dac_if",NULL,                               read_dac_if_command,                                    2,  1, 0, 0},
{"write_dac_spi",NULL,                             write_dac_spi_command,                                  2,  0, 0, 0},
{"set_threshold_for_channel",NULL,                 set_threshold_for_channel_command,                      3,  1, 0, 0},
{"pipeline_sys_reset",NULL,                        pipeline_sys_reset_command,                             1,  1, 0, 0},
{"aurora_sys_reset",NULL,                          aurora_sys_reset_command,                               1,  1, 0, 0},
{"axi_sys_reset",NULL,                             axi_sys_reset_command,                                  1,  1, 0, 0},
{"read_sync_length",NULL,                          read_sync_length_command,                               1,  1, 0, 0},
{"do_sync",NULL,                                   do_sync_command,                                        2,  0, 0, 0},
{"write_coax_dir_select",NULL,                     write_coax_dir_select_command,                          2,  0, 0, 0},
{"read_coax_dir_select",NULL,                      read_coax_dir_select_command,                           1,  1, 0, 0},
//{"read_delay_value",NULL,                          read_delay_value_command,                               2,  0, 0, 0},
{"",NULL,                                          NULL,                                                   0,  0, 0, 0}    //  Must  be  last
};

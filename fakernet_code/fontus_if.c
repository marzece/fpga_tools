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

#include "server.h" // TODO it'd be nice not to include this and have this be stand alone

#define  IIC_AXI_ADDR           0x100000
#define  GPIO0_AXI_ADDR         0x200000
#define  RESET_GEN_AXI_ADDR     0x300000
#define  FONTUS_LMK_AXI_ADDR    0x400000
#define  DAC_AXI_ADDR           0x500000
#define  TRIGGER_PIPELINE_ADDR  0x0

#define RESET_GEN_PIPELINE_BIT 0
#define RESET_GEN_AXI_BIT 1
#define RESET_GEN_AURORA_BIT 2
#define RESET_GEN_FIFO_BIT 3

#define COAX_GPIO_OFFSET 0
#define SYNC_MOD_GPIO_OFFSET 1

#define N_EXT 3
#define TRIGGERS_PER_EXTERNAL 3
#define N_THRESHOLD 5

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

static uint32_t set_enable_self_trigger_system_command(uint32_t* args) {
    uint32_t enable = args[0] ? 1 : 0;
    return write_self_trigger_enable(get_fontus_handle()->pipeline, enable);
}

static uint32_t read_enable_self_trigger_system_command(uint32_t* args) {
    UNUSED(args);
    return read_self_trigger_enable(get_fontus_handle()->pipeline);
}

static uint32_t set_enable_trigger_command(uint32_t* args) {
    uint32_t enable = args[0] ? 1 : 0;
    return write_pulse_generator_enable(get_fontus_handle()->pipeline, enable);
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

static uint32_t fifo_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_FIFO_BIT);
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

static uint32_t write_inner_multiplicity_threshold_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t threshold = args[1];
    return write_inner_multiplicity_threshold(get_fontus_handle()->pipeline, channel, threshold);
}

static uint32_t read_inner_multiplicity_threshold_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_inner_multiplicity_threshold(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_veto_multiplicity_threshold_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t threshold = args[1];
    return write_veto_multiplicity_threshold(get_fontus_handle()->pipeline, channel, threshold);
}

static uint32_t read_veto_multiplicity_threshold_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_veto_multiplicity_threshold(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_inner_esum_threshold_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t threshold = args[1];
    return write_inner_esum_threshold(get_fontus_handle()->pipeline, channel, threshold);
}

static uint32_t read_inner_esum_threshold_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_inner_esum_threshold(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_veto_esum_threshold_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t threshold = args[1];
    return write_veto_esum_threshold(get_fontus_handle()->pipeline, channel, threshold);
}

static uint32_t read_veto_esum_threshold_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_veto_esum_threshold(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_inner_multiplicity_delay_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t value = args[1];
    return write_inner_multiplicity_delay(get_fontus_handle()->pipeline, channel, value);
}

static uint32_t read_inner_multiplicity_delay_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_inner_multiplicity_delay(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_veto_multiplicity_delay_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t value = args[1];
    return write_veto_multiplicity_delay(get_fontus_handle()->pipeline, channel, value);
}

static uint32_t read_veto_multiplicity_delay_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_veto_multiplicity_delay(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_inner_esum_delay_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t value = args[1];
    return write_inner_esum_delay(get_fontus_handle()->pipeline, channel, value);
}

static uint32_t read_inner_esum_delay_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_inner_esum_delay(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_veto_esum_delay_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t value = args[1];
    return write_veto_esum_delay(get_fontus_handle()->pipeline, channel, value);
}

static uint32_t read_veto_esum_delay_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_veto_esum_delay(get_fontus_handle()->pipeline, channel);

}

static uint32_t write_frontpanel_trigger_delay_command(uint32_t* args) {
    uint32_t which_input = args[0];
    uint32_t channel = args[1];
    uint32_t value = args[2];
    return write_external_delay(get_fontus_handle()->pipeline, which_input, channel, value);
}

static uint32_t read_frontpanel_trigger_delay_command(uint32_t* args) {
    uint32_t which_input = args[0];
    uint32_t channel = args[1];
    return read_external_delay(get_fontus_handle()->pipeline, which_input, channel);
}

static uint32_t write_inner_multiplicity_gate_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t value = args[1];
    return write_inner_multiplicity_gate(get_fontus_handle()->pipeline, channel, value);
}

static uint32_t read_inner_multiplicity_gate_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_inner_multiplicity_gate(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_veto_multiplicity_gate_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t value = args[1];
    return write_veto_multiplicity_gate(get_fontus_handle()->pipeline, channel, value);
}

static uint32_t read_veto_multiplicity_gate_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_veto_multiplicity_gate(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_inner_esum_gate_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t value = args[1];
    return write_inner_esum_gate(get_fontus_handle()->pipeline, channel, value);
}

static uint32_t read_inner_esum_gate_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_inner_esum_gate(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_veto_esum_gate_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t value = args[1];
    return write_veto_esum_gate(get_fontus_handle()->pipeline, channel, value);
}

static uint32_t read_veto_esum_gate_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_veto_esum_gate(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_frontpanel_trigger_gate_command(uint32_t* args) {
    uint32_t which_input = args[0];
    uint32_t channel = args[1];
    uint32_t value = args[2];
    return write_external_gate(get_fontus_handle()->pipeline, which_input, channel, value);
}

static uint32_t read_frontpanel_trigger_gate_command(uint32_t* args) {
    uint32_t which_input = args[0];
    uint32_t channel = args[1];
    return read_external_gate(get_fontus_handle()->pipeline, which_input, channel);
}

static uint32_t read_led_trigger_length_command(uint32_t *args) {
    UNUSED(args);
    return read_led_trigger_length(get_fontus_handle()->pipeline);
}

static uint32_t write_led_trigger_length_command(uint32_t *args) {
    uint32_t length = args[0];
    return write_led_trigger_length(get_fontus_handle()->pipeline, length);
}

static uint32_t read_kicker_trigger_length_command(uint32_t *args) {
    UNUSED(args);
    return read_kicker_trigger_length(get_fontus_handle()->pipeline);
}

static uint32_t write_kicker_trigger_length_command(uint32_t *args) {
    uint32_t length = args[0];
    return write_kicker_trigger_length(get_fontus_handle()->pipeline, length);
}

static uint32_t write_self_trigger_mask_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t mask = args[1];
    return write_trigger_mask(get_fontus_handle()->pipeline, channel, mask);
}

static uint32_t read_self_trigger_mask_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_trigger_mask(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_self_veto_mask_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t mask = args[1];
    return write_trigger_veto_mask(get_fontus_handle()->pipeline, channel, mask);
}

static uint32_t read_self_veto_mask_command(uint32_t* args) {
    uint32_t channel = args[0];
    return read_trigger_veto_mask(get_fontus_handle()->pipeline, channel);
}

static uint32_t read_self_trigger_length_command(uint32_t *args) {
    uint32_t channel = args[0];
    return read_trigger_length(get_fontus_handle()->pipeline, channel);
}

static uint32_t write_self_trigger_length_command(uint32_t* args) {
    uint32_t channel = args[0];
    uint32_t len = args[1];
    return write_trigger_length(get_fontus_handle()->pipeline, channel, len);
}

static uint32_t read_threshold_enable_mask_command(uint32_t* args) {
    return read_threshold_enable_mask(get_fontus_handle()->pipeline);
}

static uint32_t write_threshold_enable_mask_command(uint32_t*args) {
    uint32_t mask = args[0];
    return write_threshold_enable_mask(get_fontus_handle()->pipeline, mask);
}

static uint32_t read_multiplicity_width_command(uint32_t* args) {
    UNUSED(args);
    return read_multiplicity_width(get_fontus_handle()->pipeline);
}

static uint32_t write_multiplicity_width_command(uint32_t* args) {
    uint32_t val = args[0];
    return write_multiplicity_width(get_fontus_handle()->pipeline, val);
}

static uint32_t read_trigger_enable_mask_command(uint32_t* args) {
    UNUSED(args);
    return read_trigger_mask_enable(get_fontus_handle()->pipeline);
}

static uint32_t write_trigger_enable_mask_command(uint32_t* args) {
    uint32_t mask = args[0];
    return write_trigger_mask_enable(get_fontus_handle()->pipeline, mask);
}

static uint32_t write_coax_dir_select_command(uint32_t* args) {
    uint32_t value = args[0];
    return write_gpio_value(get_fontus_handle()->gpio, COAX_GPIO_OFFSET, value);
}

static uint32_t read_coax_dir_select_command(uint32_t* args) {
    UNUSED(args);
    return read_gpio_value(get_fontus_handle()->gpio, 0, COAX_GPIO_OFFSET);
}

static int get_bit_for_trigger_specifier(client* c, sds* argv) {
    int bit;
    sds location = argv[0];
    // Inner Multiplicity = T/V Mask [4:0]
    // Inner ESUM = T/V Mask [9:5]
    // External Multiplicity = T/V Mask [14:10]
    // External ESUM = T/V Mask [19:15]
    // Front-panel = T/V Mask [28:20]

    if(strcmp(location, "EXTERNAL")==0) {
        uint32_t channel_1 = strtoul(argv[1], NULL, 0);
        uint32_t channel_2 = strtoul(argv[2], NULL, 0);

        if(channel_1 >= N_EXT || channel_2 >= TRIGGERS_PER_EXTERNAL) {
            addReplyErrorFormat(c, "Invalid external channel specified\n");
            return -1;
        }
        bit = 20 + TRIGGERS_PER_EXTERNAL*channel_1 + channel_2;
    }
    else if (strcmp(location, "INNER")==0 || strcmp(location, "VETO")==0) {
        sds trig_type = argv[1];
        uint32_t channel = strtoul(argv[2], NULL, 0);
        if(channel >= N_THRESHOLD) {
            addReplyErrorFormat(c, "Invalid '%s' channel specified.", location);
            return -1;
        }
        if(strcmp(trig_type, "MULT") !=0 && strcmp(trig_type, "ESUM") !=0) {
            addReplyErrorFormat(c, "Invalid trigger type specified. Must be either 'MULT' or 'ESUM'");
            return -1;
        }
        bit = strcmp(location, "INNER")==0 ? 0 : 2*N_THRESHOLD;
        bit += strcmp(trig_type, "MULT")==0 ? 0 : N_THRESHOLD;
        bit += channel;
    }
    else {
            addReplyErrorFormat(c, "Invalid trigger specifier given. Must be either 'EXTERNAL', 'INNER' or 'VETO'.");
            return -1;
    }
    return bit;
}

static void add_to_trigger_mask_hr_command(client* c, int argc, sds* argv) {

    int i;
    int bit;
    uint32_t which_mask = strtoul(argv[1], NULL, 0);
    if(which_mask >= 10) {
        addReplyErrorFormat(c, "Invalid trigger mask specified.");
        return;
    }

    for(i=2; i<argc; i++) {
        sdstoupper(argv[i]);
    }
    bit = get_bit_for_trigger_specifier(c, argv+2);
    if(bit < 0) {
        // Client error string should have been set above
        return;
    }

    uint32_t current_mask = read_trigger_mask(get_fontus_handle()->pipeline, which_mask);
    uint32_t new_mask = current_mask | (1<<bit);
    if(write_trigger_mask(get_fontus_handle()->pipeline, which_mask, new_mask)) {
        addReplyError(c, "Error while writing mask to FONTUS");
    } else {
        addReplyStatus(c, "OK");
    }
}

static void add_to_veto_mask_hr_command(client* c, int argc, sds* argv) {
    int i;
    int bit;
    uint32_t which_mask = strtoul(argv[1], NULL, 0);
    if(which_mask >= 10) {
        addReplyErrorFormat(c, "Invalid trigger mask specified.");
        return;
    }

    for(i=2; i<argc; i++) {
        sdstoupper(argv[i]);
    }
    bit = get_bit_for_trigger_specifier(c, argv+2);
    if(bit < 0) {
        // Client error string should have been set above
        return;
    }

    uint32_t current_mask = read_trigger_veto_mask(get_fontus_handle()->pipeline, which_mask);
    uint32_t new_mask = current_mask | (1<<bit);
    if(write_trigger_veto_mask(get_fontus_handle()->pipeline, which_mask, new_mask)) {
        addReplyError(c, "Error while writing mask to FONTUS");
    } else {
        addReplyStatus(c, "OK");
    }
}

static void remove_from_trigger_mask_hr_command(client* c, int argc, sds* argv) {
    int i;
    int bit;
    uint32_t which_mask = strtoul(argv[1], NULL, 0);
    if(which_mask >= 10) {
        addReplyErrorFormat(c, "Invalid trigger mask specified.");
        return;
    }

    for(i=2; i<argc; i++) {
        sdstoupper(argv[i]);
    }
    bit = get_bit_for_trigger_specifier(c, argv+2);
    if(bit < 0) {
        // Client error string should have been set above
        return;
    }

    uint32_t current_mask = read_trigger_mask(get_fontus_handle()->pipeline, which_mask);
    uint32_t new_mask = current_mask & (~(1<<bit));

    if(write_trigger_mask(get_fontus_handle()->pipeline, which_mask, new_mask)) {
        addReplyError(c, "Error while writing mask to FONTUS");
    } else {
        addReplyStatus(c, "OK");
    }
}

static void remove_from_veto_mask_hr_command(client* c, int argc, sds* argv) {
    int i;
    int bit;
    uint32_t which_mask = strtoul(argv[1], NULL, 0);
    if(which_mask >= 10) {
        addReplyErrorFormat(c, "Invalid trigger mask specified.");
        return;
    }

    for(i=2; i<argc; i++) {
        sdstoupper(argv[i]);
    }
    bit = get_bit_for_trigger_specifier(c, argv+2);
    if(bit < 0) {
        // Client error string should have been set above
        return;
    }

    uint32_t current_mask = read_trigger_veto_mask(get_fontus_handle()->pipeline, which_mask);
    uint32_t new_mask = current_mask & (~(1<<bit));

    if(write_trigger_veto_mask(get_fontus_handle()->pipeline, which_mask, new_mask)) {
        addReplyError(c, "Error while writing mask to FONTUS");
    } else {
        addReplyStatus(c, "OK");
    }
}

static uint32_t write_trigger_pipeline_reg_command(uint32_t* args) {
    uint32_t addr = args[0];
    uint32_t data = args[1];
    return write_trig_pipeline_value(get_fontus_handle()->pipeline, addr, data);
}
static uint32_t read_trigger_pipeline_reg_command(uint32_t* args) {
    uint32_t addr = args[0];
    return read_trig_pipeline_value(get_fontus_handle()->pipeline, addr);
}

static uint32_t write_sync_mod_command(uint32_t* args) {
    uint32_t value = args[0];
    return write_gpio_value(get_fontus_handle()->gpio, SYNC_MOD_GPIO_OFFSET, value);
}

static uint32_t read_sync_mod_command(uint32_t* args) {
    UNUSED(args);
    return read_gpio_value(get_fontus_handle()->gpio, 0, SYNC_MOD_GPIO_OFFSET);
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
{"set_enable_self_trigger_system",NULL,            set_enable_self_trigger_system_command,                 2,  0, 0, 0},
{"read_enable_self_trigger_system",NULL,           read_enable_self_trigger_system_command,                1,  0, 0, 0},
{"set_enable_trigger_system",NULL,                 set_enable_trigger_command,                             2,  0, 0, 0},
{"write_dac_if",NULL,                              write_dac_if_command,                                   3,  0, 0, 0}, 
{"read_dac_if",NULL,                               read_dac_if_command,                                    2,  1, 0, 0},
{"write_dac_spi",NULL,                             write_dac_spi_command,                                  2,  0, 0, 0},
{"set_frontpanel_threshold_for_channel",NULL,      set_threshold_for_channel_command,                      3,  1, 0, 0},
{"pipeline_sys_reset",NULL,                        pipeline_sys_reset_command,                             1,  1, 0, 0},
{"aurora_sys_reset",NULL,                          aurora_sys_reset_command,                               1,  1, 0, 0},
{"axi_sys_reset",NULL,                             axi_sys_reset_command,                                  1,  1, 0, 0},
{"fifo_sys_reset",NULL,                            fifo_sys_reset_command,                                 1,  1, 0, 0},
{"read_sync_length",NULL,                          read_sync_length_command,                               1,  1, 0, 0},
{"do_sync",NULL,                                   do_sync_command,                                        2,  0, 0, 0},
{"write_inner_multiplicity_threshold",NULL,        write_inner_multiplicity_threshold_command,             3,  0, 0, 0},
{"read_inner_multiplicity_threshold",NULL,         read_inner_multiplicity_threshold_command,              2,  1, 0, 0},
{"write_veto_multiplicity_threshold",NULL,         write_veto_multiplicity_threshold_command,              3,  0, 0, 0},
{"read_veto_multiplicity_threshold",NULL,          read_veto_multiplicity_threshold_command,               2,  1, 0, 0},
{"write_inner_esum_threshold",NULL,                write_inner_esum_threshold_command,                     3,  0, 0, 0},
{"read_inner_esum_threshold",NULL,                 read_inner_esum_threshold_command,                      2,  1, 0, 0},
{"write_veto_esum_threshold",NULL,                 write_veto_esum_threshold_command,                      3,  0, 0, 0},
{"read_veto_esum_threshold",NULL,                  read_veto_esum_threshold_command,                       2,  1, 0, 0},

{"write_inner_multiplicity_delay",NULL,            write_inner_multiplicity_delay_command,                 3,  0, 0, 0},
{"read_inner_multiplicity_delay",NULL,             read_inner_multiplicity_delay_command,                  2,  1, 0, 0},
{"write_veto_multiplicity_delay",NULL,             write_veto_multiplicity_delay_command,                  3,  0, 0, 0},
{"read_veto_multiplicity_delay",NULL,              read_veto_multiplicity_delay_command,                   2,  1, 0, 0},
{"write_inner_esum_delay",NULL,                    write_inner_esum_delay_command,                         3,  0, 0, 0},
{"read_inner_esum_delay",NULL,                     read_inner_esum_delay_command,                          2,  1, 0, 0},
{"write_veto_esum_delay",NULL,                     write_veto_esum_delay_command,                          3,  0, 0, 0},
{"read_veto_esum_delay",NULL,                      read_veto_esum_delay_command,                           2,  1, 0, 0},
{"write_frontpanel_trigger_delay",NULL,            write_frontpanel_trigger_delay_command,                 4,  0, 0, 0},
{"read_frontpanel_trigger_delay",NULL,             read_frontpanel_trigger_delay_command,                  3,  1, 0, 0},

{"write_inner_multiplicity_gate",NULL,             write_inner_multiplicity_gate_command,                  3,  0, 0, 0},
{"read_inner_multiplicity_gate",NULL,              read_inner_multiplicity_gate_command,                   2,  1, 0, 0},
{"write_veto_multiplicity_gate",NULL,              write_veto_multiplicity_gate_command,                   3,  0, 0, 0},
{"read_veto_multiplicity_gate",NULL,               read_veto_multiplicity_gate_command,                    2,  1, 0, 0},
{"write_inner_esum_gate",NULL,                     write_inner_esum_gate_command,                          3,  0, 0, 0},
{"read_inner_esum_gate",NULL,                      read_inner_esum_gate_command,                           2,  1, 0, 0},
{"write_veto_esum_gate",NULL,                      write_veto_esum_gate_command,                           3,  0, 0, 0},
{"read_veto_esum_gate",NULL,                       read_veto_esum_gate_command,                            2,  1, 0, 0},
{"write_frontpanel_trigger_gate",NULL,             write_frontpanel_trigger_gate_command,                  4,  0, 0, 0},
{"read_frontpanel_trigger_gate",NULL,              read_frontpanel_trigger_gate_command,                   3,  1, 0, 0},

{"read_led_trigger_length",NULL,                   read_led_trigger_length_command,                        1,  1, 0, 0},
{"write_led_trigger_length",NULL,                  write_led_trigger_length_command,                       2,  0, 0, 0},
{"read_kicker_trigger_length",NULL,                read_kicker_trigger_length_command,                     1,  1, 0, 0},
{"write_kicker_trigger_length",NULL,               write_kicker_trigger_length_command,                    2,  0, 0, 0},

{"write_self_trigger_mask",NULL,                   write_self_trigger_mask_command,                        3,  0, 0, 0},
{"read_self_trigger_mask",NULL,                    read_self_trigger_mask_command,                         2,  1, 0, 0},
{"write_self_veto_mask",NULL,                      write_self_veto_mask_command,                           3,  0, 0, 0},
{"read_self_veto_mask",NULL,                       read_self_veto_mask_command,                            2,  1, 0, 0},
{"write_self_trigger_length",NULL,                 write_self_trigger_length_command,                      3,  0, 0, 0},
{"read_self_trigger_length",NULL,                  read_self_trigger_length_command,                       2,  1, 0, 0},

{"read_threshold_enable_mask",NULL,                read_threshold_enable_mask_command,                     1,  1, 0, 0},
{"write_threshold_enable_mask",NULL,               write_threshold_enable_mask_command,                    2,  0, 0, 0},

{"write_multiplicity_width",NULL,                  write_multiplicity_width_command,                       2,  0, 0, 0},
{"read_multiplicity_width",NULL,                   read_multiplicity_width_command,                        1,  1, 0, 0},
{"write_trigger_enable_mask",NULL,                 write_trigger_enable_mask_command,                      2,  0, 0, 0},
{"read_trigger_enable_mask",NULL,                   read_trigger_enable_mask_command,                       1,  1, 0, 0},

{"add_to_trigger_mask_hr",                         add_to_trigger_mask_hr_command, NULL,                   5,  0, 0, 0},
{"add_to_veto_mask_hr",                            add_to_veto_mask_hr_command, NULL,                      5,  0, 0, 0},
{"remove_from_trigger_mask_hr",                    remove_from_trigger_mask_hr_command, NULL,              5,  0, 0, 0},
{"remove_from_veto_mask_hr",                       remove_from_veto_mask_hr_command, NULL,                 5,  0, 0, 0},

{"write_coax_dir_select",NULL,                     write_coax_dir_select_command,                          2,  0, 0, 0},
{"read_coax_dir_select",NULL,                      read_coax_dir_select_command,                           1,  1, 0, 0},

{"write_trigger_pipeline_reg",NULL,                write_trigger_pipeline_reg_command,                     3,  0, 0, 0},
{"read_trigger_pipline_reg",NULL,                  read_trigger_pipeline_reg_command,                      2,  1, 0, 0},
{"write_sync_mod",NULL,                            write_sync_mod_command,                                 2,  0, 0, 0},
{"read_sync_mod",NULL,                             read_sync_mod_command,                                  1,  1, 0, 0},
{"",NULL,                                          NULL,                                                   0,  0, 0, 0}    //  Must  be  last
};

#include <assert.h>
#include "ceres_if.h"
#include "gpio.h"
#include "iic.h"
#include "lmk_if.h"
#include "dac_if.h"
#include "ads_if.h"
#include "jesd.h"
#include "data_pipeline.h"

#define  ADC_A_AXI_ADDR          0x0000
#define  ADC_B_AXI_ADDR          0x10000
#define  ADC_C_AXI_ADDR          0x80
#define  ADC_D_AXI_ADDR          0x100

#define  GPIO0_AXI_ADDR          0x1000
#define  GPIO1_AXI_ADDR          0x2000
#define  GPIO2_AXI_ADDR          0x3000
#define  GPIO3_AXI_ADDR          0xC000

#define  LMK_A_AXI_ADDR           0x5000
#define  LMK_B_AXI_ADDR           0xB000

#define  IIC_AXI_ADDR            0x4000
#define  CLKGEN_IIC_AXI_ADDR     0xD000

#define  JESD_A_AXI_ADDR         0x7000
#define  JESD_B_AXI_ADDR         0x8000
#define  JESD_C_AXI_ADDR         0x9000
#define  JESD_D_AXI_ADDR         0xA000

#define  DATA_PIPELINE_0_ADDR    0x6000

#define CLKGEN_IIC_ADDR          0xCE

const uint32_t CERES_SAFE_READ_ADDRESS = GPIO0_AXI_ADDR;

struct CERES_IF {
    AXI_QSPI* lmk_a;
    AXI_QSPI* lmk_b;
    AXI_QSPI* adc_a;
    AXI_QSPI* adc_b;
    AXI_QSPI* adc_c;
    AXI_QSPI* adc_d;
    AXI_GPIO* gpio0;
    AXI_GPIO* gpio1;
    AXI_GPIO* gpio2;
    AXI_GPIO* gpio3;
    AXI_IIC* iic_main;
    AXI_IIC* clk_gen_iic;
    AXI_JESD* jesd_a;
    AXI_JESD* jesd_b;
    AXI_JESD* jesd_c;
    AXI_JESD* jesd_d;
    AXI_DATA_PIPELINE* pipeline;
};

static struct CERES_IF* ceres = NULL;

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

static struct CERES_IF* get_ceres_handle() {
    if(ceres == NULL) {
        ceres = malloc(sizeof(struct CERES_IF));
        ceres->lmk_a = new_lmk_spi("lmk_a", LMK_A_AXI_ADDR);
        ceres->lmk_b = new_lmk_spi("lmk_b", LMK_B_AXI_ADDR);
        ceres->adc_a = new_ads_spi("adc_a", ADC_A_AXI_ADDR);
        ceres->adc_b = new_ads_spi("adc_b", ADC_B_AXI_ADDR);
        ceres->adc_c = new_ads_spi("adc_c", ADC_C_AXI_ADDR);
        ceres->adc_d = new_ads_spi("adc_d", ADC_D_AXI_ADDR);
        ceres->gpio0 = new_gpio("power_up_cntrl", GPIO0_AXI_ADDR);
        ceres->gpio1 = new_gpio("gpio_sync_counter", GPIO1_AXI_ADDR);
        ceres->gpio2 = new_gpio("gpio_reset", GPIO2_AXI_ADDR);
        ceres->gpio3 = new_gpio("gpio_pdn", GPIO3_AXI_ADDR);
        ceres->iic_main = new_iic("iic_main", IIC_AXI_ADDR,1, 1);
        ceres->clk_gen_iic = new_iic("clkgen_iic", CLKGEN_IIC_AXI_ADDR, 2, 2);
        ceres->jesd_a = new_jesd("jesd_a", JESD_A_AXI_ADDR);
        ceres->jesd_b = new_jesd("jesd_b", JESD_B_AXI_ADDR);
        ceres->jesd_c = new_jesd("jesd_c", JESD_C_AXI_ADDR);
        ceres->jesd_d = new_jesd("jesd_d", JESD_D_AXI_ADDR);
        ceres->pipeline = new_data_pipeline_if("dp0", DATA_PIPELINE_0_ADDR);
    }
    return ceres;
}

static uint32_t write_gpio_0_command(uint32_t *args) {
    return write_gpio_value(get_ceres_handle()->gpio0, args[0], args[1]);
}

static uint32_t read_gpio_0_command(uint32_t *args) {
    return read_gpio_value(get_ceres_handle()->gpio0, args[0]);
}

static uint32_t write_gpio_1_command(uint32_t *args) {
    return write_gpio_value(get_ceres_handle()->gpio1, args[0], args[1]);
}

static uint32_t read_gpio_1_command(uint32_t *args) {
    return read_gpio_value(get_ceres_handle()->gpio1, args[0]);
}

static uint32_t write_gpio_2_command(uint32_t *args) {
    return write_gpio_value(get_ceres_handle()->gpio2, args[0], args[1]);
}
//
static uint32_t read_gpio_2_command(uint32_t *args) {
    return read_gpio_value(get_ceres_handle()->gpio2, args[0]);
}

static uint32_t set_adc_power_command(uint32_t *args) {
    uint32_t val = args[0];

    write_gpio_value(get_ceres_handle()->gpio0, 0x0, val);
    return 0;
}

// IIC commands
static uint32_t read_iic_block_command(uint32_t* args) {
    uint32_t offset = args[0];
    return iic_read(get_ceres_handle()->iic_main, offset);
}

static uint32_t write_iic_block_command(uint32_t* args) {
    uint32_t offset = args[0];
    uint32_t data = args[1];
    int err = iic_write(get_ceres_handle()->iic_main, offset, data);
    return err;
}

static uint32_t read_iic_bus_command(uint32_t* args) {
    return read_iic_bus(get_ceres_handle()->iic_main, args[0]);
}

static uint32_t write_iic_bus_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t iic_value = args[1];
    return write_iic_bus(get_ceres_handle()->iic_main, iic_addr, iic_value);
}

static uint32_t read_iic_bus_with_reg_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t reg_addr = args[1];
    return read_iic_bus_with_reg(get_ceres_handle()->iic_main, iic_addr, reg_addr);
}

static uint32_t write_iic_bus_with_reg_command(uint32_t* args) {
    uint32_t iic_addr = args[0];
    uint32_t reg_addr = args[1];
    uint32_t reg_value = args[2];
    return write_iic_bus_with_reg(get_ceres_handle()->iic_main, iic_addr, reg_addr, reg_value); 
}

static uint32_t read_clkgen_iic_command(uint32_t *args) {
    uint32_t offset = args[0];
    return iic_read(get_ceres_handle()->clk_gen_iic, offset);
}

static uint32_t write_clkgen_iic_command(uint32_t *args) {
    uint32_t offset = args[0];
    uint32_t value = args[1];
    return iic_write(get_ceres_handle()->clk_gen_iic, offset, value);
}

static uint32_t write_clkgen_register_command(uint32_t *args) {
    uint32_t iic_addr = CLKGEN_IIC_ADDR;
    uint16_t reg_addr = args[0];
    uint16_t reg_value = args[1];
    // IIC Stuff
    return write_iic_bus_with_reg(get_ceres_handle()->clk_gen_iic, iic_addr, reg_addr, reg_value);
}

static uint32_t read_clkgen_register_command(uint32_t *args) {
    uint32_t iic_addr = CLKGEN_IIC_ADDR;
    uint16_t reg_addr = args[0];
    // IIC Stuff
    return read_iic_bus_with_reg(get_ceres_handle()->clk_gen_iic, iic_addr, reg_addr);
}

static uint32_t write_clkgen_gpio_command(uint32_t *args) {
    uint32_t val = args[0];
    return write_iic_gpio(get_ceres_handle()->clk_gen_iic, val);
}

static uint32_t read_clkgen_gpio_command(uint32_t *args) {
    UNUSED(args);
    return read_iic_gpio(get_ceres_handle()->clk_gen_iic);
}

//static uint32_t write_clkgen_eeprom(uint32*t args) { }

AXI_QSPI* adc_switch(int which_adc) {
    AXI_QSPI* this_adc = NULL;
    switch(which_adc) {
        case(0):
            this_adc = get_ceres_handle()->adc_a;
            break;
        case(1):
            this_adc = get_ceres_handle()->adc_b;
            break;
        case(2):
            this_adc = get_ceres_handle()->adc_c;
            break;
        case(3):
            this_adc = get_ceres_handle()->adc_d;
            break;
        default:
            // TODO really need to add an error string!
            return NULL;
    }
    return this_adc;
}

AXI_QSPI* lmk_switch(uint32_t which_lmk) {
    AXI_QSPI* this_lmk = NULL;
    switch(which_lmk) {
        case(0):
            this_lmk = get_ceres_handle()->lmk_a;
            break;
        case(1):
            this_lmk = get_ceres_handle()->lmk_b;
            break;
        default:
            // TODO really need to add an error string!
            return NULL;
    }
    return this_lmk;
}

AXI_JESD* jesd_switch(uint32_t which_jesd) {
    AXI_JESD* this_jesd = NULL;
    switch(which_jesd) {
        case 0:
            this_jesd = get_ceres_handle()->jesd_a;
            break;
        case 1:
            this_jesd = get_ceres_handle()->jesd_b;
            break;
        case 2:
            this_jesd = get_ceres_handle()->jesd_c;
            break;
        case 3:
            this_jesd = get_ceres_handle()->jesd_d;
            break;
        default:
            return NULL;
    }
    return this_jesd;
}

static uint32_t write_ads_if_command(uint32_t *args) {
    uint32_t which_adc = args[0];
    uint32_t offset = args[1];
    uint32_t data = args[2];

    AXI_QSPI* adc = adc_switch(which_adc);
    if(!adc) { return -1; }
    return write_qspi_addr(adc, offset, data);
}

static uint32_t read_ads_if_command(uint32_t *args) {
    uint32_t which_adc = args[0];
    uint32_t offset = args[1];
    AXI_QSPI* adc = adc_switch(which_adc);
    if(!adc) { return -1; }
    return read_qspi_addr(adc, offset);
}

static uint32_t write_adc_spi_command(uint32_t* args) {
    uint32_t which_adc = args[0];
    AXI_QSPI* adc = adc_switch(which_adc);
    if(!adc) { return -1; }
    return write_adc_spi(adc, args+1);
}

static uint32_t ads_spi_data_available_command(uint32_t* args) {
    uint32_t which_adc = args[0];
    AXI_QSPI* adc = adc_switch(which_adc);
    if(!adc) { return -1; }
    return spi_drr_data_available(adc);
}

static uint32_t ads_spi_data_pop_command(uint32_t* args) {
    uint32_t which_adc = args[0];
    AXI_QSPI* adc = adc_switch(which_adc);
    if(!adc) { return -1; }
    return spi_drr_pop(adc);
}

static uint32_t write_lmk_if_command(uint32_t* args) {
    int which_lmk = args[0];
    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }
    return write_lmk_if(lmk, args[1], args[2]);
}

static uint32_t read_lmk_if_command(uint32_t* args) {
    int which_lmk = args[0];
    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }
    return read_lmk_if(lmk, args[1]);
}

uint32_t write_lmk_spi(AXI_QSPI* lmk, uint32_t rw, uint32_t addr, uint32_t data) {
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
    return write_spi(lmk, ssr, word_buf, 3);

}
static uint32_t write_lmk_spi_command(uint32_t* args) {
    uint32_t which_lmk = args[0];
    uint32_t rw = args[1];
    uint32_t addr = args[2];
    uint32_t data = args[3];

    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }

    return write_lmk_spi(lmk, rw, addr, data);
}

static uint32_t lmk_spi_data_available_command(uint32_t* args) {
    uint32_t which_lmk = args[0];
    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }
    return spi_drr_data_available(lmk);
}

static uint32_t lmk_spi_data_pop_command(uint32_t* args) {
    uint32_t which_lmk = args[0];
    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }
    return spi_drr_pop(lmk);
}

static uint32_t jesd_read_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t offset = args[1];

    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }

    return read_jesd(jesd, offset);
}

static uint32_t jesd_write_command(uint32_t *args) {
    uint32_t which_jesd = args[0];
    uint32_t offset = args[1];
    uint32_t data = args[2];

    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }
    return write_jesd(jesd, offset, data);
}

static uint32_t jesd_error_rate_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }
    return jesd_read_error_rate(jesd, args);
}

static uint32_t jesd_reset_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }
    return jesd_reset(jesd);
}

static uint32_t jesd_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    uint32_t GPIO_DATA_OFFSET = 0x0;
    // Just toggle the signal up then down...should do a reset
    write_gpio_value(get_ceres_handle()->gpio2, GPIO_DATA_OFFSET, 0x1);
    usleep(200);
    write_gpio_value(get_ceres_handle()->gpio2, GPIO_DATA_OFFSET, 0x0);
    return 0;
}

// This command reads a GPIO block that (in principle) is hooked up to a
// binary counter that counts clock ticks while SYNC is low. 
// Count is reset by JESD SYS Reset
// TODO, copy/pasted the code for A & B...dry it out!
static uint32_t jesd_a_sync_rate_command(uint32_t *args) {
    UNUSED(args);
    uint32_t GPIO_DATA1_ADDR = 0x0;
    uint32_t first = read_gpio_value(get_ceres_handle()->gpio1, GPIO_DATA1_ADDR);
    usleep(500e3);
    uint32_t second = read_gpio_value(get_ceres_handle()->gpio1, GPIO_DATA1_ADDR);
    // Don't need to worry about rollover, subtraction handles it okay even
    // accross the 32-bit rollover.
    return second - first;
}

/*
static uint32_t read_all_error_rates_command(uint32_t* resp) {
    uint32_t first[20];
    uint32_t second[20];
    int i;
    int channel;
    int which_jesd;
    uint32_t GPIO_DATA1_ADDR = 0x0;
    uint32_t GPIO_DATA2_ADDR = 0x4;

    uint32_t* data_arr = first;

    for(i=0; i < 2; i++) {
        for(which_jesd=0; which_jesd < 4; which_jesd++) {
            AXI_JESD* this_jesd = jesd_switch(which_jesd);
            uint32_t gpio_data_offset = a_or_b ? GPIO_DATA2_ADDR : GPIO_DATA1_ADDR;
            for(channel=0; channel<4; channel++) {
                data_arr[channel+a_or_b*4] = read_error_register(this_jesd, channel);
            }
            data_arr[8+a_or_b] = read_gpio_value(get_ceres_handle()->gpio1, gpio_data_offset);
        }
        usleep(500e3);
        data_arr = second;
    }
    for(i=0; i<10; i++) {
        resp[i] = second[i] - first[i];
    }
    return 0;
}*/

// This command reads a GPIO block that (in principle) is hooked up to a
// binary counter that counts clock ticks while SYNC is low. 
// Count is reset by JESD SYS Reset
/*
static uint32_t jesd_b_sync_rate_command(uint32_t *args) {
    UNUSED(args);
    uint32_t GPIO_DATA2_ADDR = 0x4;
    uint32_t first = read_gpio_value(get_ceres_handle()->gpio1, GPIO_DATA2_ADDR);
    usleep(500e3);
    uint32_t second = read_gpio_value(get_ceres_handle()->gpio1, GPIO_DATA2_ADDR);
    // Don't need to worry about rollover, subtraction handles it okay even
    // accross the 32-bit rollover.
    return second - first;
}
*/

static uint32_t jesd_is_synced_command(uint32_t *args) {
    uint32_t which_jesd = args[0];
    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }
    return jesd_is_synced(jesd);
}

static uint32_t jesd_set_sync_error_reporting_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t state = args[1];
    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }
    return set_sync_error_reporting(jesd, state);
}

static uint32_t set_adc_pdn_command(uint32_t* args) {
    return write_gpio_value(get_ceres_handle()->gpio3, 0, args[0]);
}

static uint32_t get_adc_pdn_command(uint32_t* args) {
    UNUSED(args);
    return read_gpio_value(get_ceres_handle()->gpio3, 0);
}

static uint32_t adc_reset_command(uint32_t* args) {
    uint32_t mask = args[0];
    return write_gpio_value(get_ceres_handle()->gpio2, 1, mask);
}

static uint32_t read_data_pipeline_command(uint32_t* args) {
    uint32_t offset =  args[0];
    return read_data_pipeline_value(get_ceres_handle()->pipeline, offset);
}

static uint32_t write_data_pipeline_command(uint32_t* args) {
    uint32_t offset = args[0];
    uint32_t value = args[1];
    return write_data_pipeline_value(get_ceres_handle()->pipeline, offset, value);
}

static uint32_t read_data_pipeline_threshold_command(uint32_t* args) {
    UNUSED(args);
    return read_threshold(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_threshold_command(uint32_t* args) {
    uint32_t val = args[0];
    return write_threshold(get_ceres_handle()->pipeline, val);
}

static uint32_t read_data_pipeline_channel_mask_command(uint32_t* args) {
    UNUSED(args);
    return read_channel_mask(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_channeL_mask_command(uint32_t* args) {
    uint32_t val = args[0];
    return write_channel_mask(get_ceres_handle()->pipeline, val);
}

static uint32_t read_data_pipeline_depth_command(uint32_t* args) {
    int channel = args[0];
    return read_channel_depth(get_ceres_handle()->pipeline, channel);
}

static uint32_t write_data_pipeline_depth_command(uint32_t* args) {
    int channel = args[0];
    int value = args[1];
    return write_channel_depth(get_ceres_handle()->pipeline, channel, value);
}

static uint32_t read_data_pipeline_invalid_count_command(uint32_t* args) {
    UNUSED(args);
    return read_invalid_count(get_ceres_handle()->pipeline);
}

static uint32_t read_data_pipeline_status_command(uint32_t* args) {
    UNUSED(args);
    return read_fifo_status_reg(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_reset_command(uint32_t* args) {
    uint32_t mask = args[0];
    return write_reset_reg(get_ceres_handle()->pipeline, mask);
}

static uint32_t read_data_pipeline_local_trigger_enable_command(uint32_t *args) {
    UNUSED(args);
    return read_local_trigger_enable(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_local_trigger_enable_command(uint32_t *args) {
    uint32_t trigger_enable = args[0];
    return write_local_trigger_enable(get_ceres_handle()->pipeline, trigger_enable);
}

static uint32_t read_data_pipeline_local_trigger_mode_command(uint32_t* args) {
    UNUSED(args);
    return read_local_trigger_mode(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_local_trigger_mode_command(uint32_t* args) {
    uint32_t trigger_mode = args[0];
    return write_local_trigger_mode(get_ceres_handle()->pipeline, trigger_mode);
}

static uint32_t read_data_pipeline_local_trigger_length_command(uint32_t* args) {
    UNUSED(args);
    return read_local_trigger_length(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_local_trigger_length_command(uint32_t* args) {
    uint32_t length = args[0];
    return write_local_trigger_length(get_ceres_handle()->pipeline, length);
}

static uint32_t read_data_pipeline_trigger_count_reset_value_command(uint32_t* args) {
    UNUSED(args);
    return read_local_trigger_count_reset_value(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_trigger_count_reset_value_command(uint32_t* args) {
    uint32_t val = args[0];
    return write_local_trigger_count_reset_value(get_ceres_handle()->pipeline, val);
}

static uint32_t read_data_pipeline_trigger_enable_command(uint32_t* args) {
    UNUSED(args);
    return read_trigger_enable(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_trigger_enable_command(uint32_t* args) {
    uint32_t enable = args[0];
    return write_trigger_enable(get_ceres_handle()->pipeline, enable);
}

static uint32_t set_trigger_params_command(uint32_t* args) {
    uint32_t rate = args[0];   // Number of counts between each readout
    uint32_t length = args[1]; // Amount of data in each readout

    if(rate == 0 || length == 0) {
        printf("Rate and length parameter canot be zero\n");
        return -1;
    }

    rate = 100e6 / rate; // Idk if 100e6 is the right value here, but its close enough probably
    uint32_t ret = 0;
    ret = write_local_trigger_length(get_ceres_handle()->pipeline, length);
    ret |= write_local_trigger_count_reset_value(get_ceres_handle()->pipeline, rate);

    return ret;
}

static uint32_t set_sysref_command(uint32_t* args) {
    uint32_t which_lmk = args[0];   // Number of counts between each readout
    uint32_t onoff = args[1];   // Number of counts between each readout

    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }

    uint32_t addr = 0x127;
    uint32_t rw = 1;
    uint32_t data = 0x11 ? onoff : 0x01;
    uint32_t ret = write_lmk_spi(lmk, rw, addr, data);

    // TODO look at these values? Return them?
    spi_drr_pop(lmk);
    spi_drr_pop(lmk);
    spi_drr_pop(lmk);

    return ret;
}

ServerCommand ceres_commands[] = {
{"read_iic_reg",                                   read_iic_block_command,                                 1,  1},
{"write_iic_reg",                                  write_iic_block_command,                                2,  0},
{"read_iic_bus",                                   read_iic_bus_command,                                   1,  1},
{"write_iic_bus",                                  write_iic_bus_command,                                  2,  1},
{"read_iic_bus_with_reg",                          read_iic_bus_with_reg_command,                          2,  1},
{"write_iic_bus_with_reg",                         write_iic_bus_with_reg_command,                         3,  1},
{"write_clkgen_iic",                               write_clkgen_iic_command,                               2,  1},
{"read_clkgen_iic",                                read_clkgen_iic_command,                                1,  1},
{"read_clkgen_gpio",                               read_clkgen_gpio_command,                               0,  1},
{"write_clkgen_gpio",                              write_clkgen_gpio_command,                              1,  1},
{"write_clkgen_register",                          write_clkgen_register_command,                          2,  1},
{"read_clkgen_register",                           read_clkgen_register_command,                           1,  1},
{"read_gpio0",                                     read_gpio_0_command,                                    1,  1},
{"write_gpio0",                                    write_gpio_0_command,                                   2,  1},
{"read_gpio1",                                     read_gpio_1_command,                                    1,  1},
{"write_gpio1",                                    write_gpio_1_command,                                   2,  1},
{"read_gpio2",                                     read_gpio_2_command,                                    1,  1},
{"write_gpio2",                                    write_gpio_2_command,                                   2,  1},
{"set_adc_power",                                  set_adc_power_command,                                  1,  1},
{"read_ads",                                       read_ads_if_command,                                    2,  1},
{"write_ads",                                      write_ads_if_command,                                   3,  1},
{"write_adc_spi",                                  write_adc_spi_command,                                  4,  1},
{"ads_spi_data_available",                         ads_spi_data_available_command,                         1,  1},
{"ads_spi_pop",                                    ads_spi_data_pop_command,                               1,  1},
{"write_lmk_if",                                   write_lmk_if_command,                                   3,  1},
{"read_lmk_if",                                    read_lmk_if_command,                                    2,  1},
{"write_lmk_spi",                                  write_lmk_spi_command,                                  4,  1},
{"lmk_spi_data_available",                         lmk_spi_data_available_command,                         1,  1},
{"lmk_spi_data_pop",                               lmk_spi_data_pop_command,                               1,  1},
{"jesd_read",                                      jesd_read_command,                                      2,  1},
{"jesd_write",                                     jesd_write_command,                                     3,  1},
{"jesd_error_rate",                                jesd_error_rate_command,                                1,  4},
{"jesd_reset",                                     jesd_reset_command,                                     1,  1},
{"jesd_sys_reset",                                 jesd_sys_reset_command,                                 0,  1},
{"jesd_is_synced",                                 jesd_is_synced_command,                                 1,  1},
{"jesd_set_sync_error_reporting",                  jesd_set_sync_error_reporting_command,                  2,  1},
//{"read_all_error_rates",                         read_all_error_rates_command,                           0,  10},
{"set_adc_pdn",                                    set_adc_pdn_command,                                    1,  1},
{"get_adc_pdn",                                    get_adc_pdn_command,                                    0,  1},
{"adc_reset",                                      adc_reset_command,                                      1,  1},
{"read_data_pipeline_threshold",                   read_data_pipeline_threshold_command,                   0,  1},
{"write_data_pipeline_threshold",                  write_data_pipeline_threshold_command,                  1,  1},
{"read_data_pipeline_channel_mask",                read_data_pipeline_channel_mask_command,                0,  1},
{"write_data_pipeline_channeL_mask",               write_data_pipeline_channeL_mask_command,               1,  1},
{"read_data_pipeline_depth",                       read_data_pipeline_depth_command,                       1,  1},
{"write_data_pipeline_depth",                      write_data_pipeline_depth_command,                      2,  1},
{"write_data_pipeline",                            write_data_pipeline_command,                            2,  1},
{"read_data_pipeline",                             read_data_pipeline_command,                             1,  1},
{"read_data_pipeline_invalid_count",               read_data_pipeline_invalid_count_command,               0,  1},
{"read_data_pipeline_status",                      read_data_pipeline_status_command,                      0,  1},
{"write_data_pipeline_reset",                      write_data_pipeline_reset_command,                      1,  1},
{"read_data_pipeline_local_trigger_enable",        read_data_pipeline_local_trigger_enable_command,        0,  1},
{"write_data_pipeline_local_trigger_enable",       write_data_pipeline_local_trigger_enable_command,       1,  1},
{"read_data_pipeline_local_trigger_mode",          read_data_pipeline_local_trigger_mode_command,          0,  1},
{"write_data_pipeline_local_trigger_mode",         write_data_pipeline_local_trigger_mode_command,         1,  1},
{"read_data_pipeline_local_trigger_length",        read_data_pipeline_local_trigger_length_command,        0,  1},
{"write_data_pipeline_local_trigger_length",       write_data_pipeline_local_trigger_length_command,       1,  1},
{"read_data_pipeline_trigger_count_reset_value",   read_data_pipeline_trigger_count_reset_value_command,   0,  1},
{"write_data_pipeline_trigger_count_reset_value",  write_data_pipeline_trigger_count_reset_value_command,  1,  1},
{"read_data_pipeline_trigger_enable",              read_data_pipeline_trigger_enable_command,              0,  1},
{"write_data_pipeline_trigger_enable",             write_data_pipeline_trigger_enable_command,             1,  1},
{"set_trigger_params",                             set_trigger_params_command,                             2,  1},
{"set_sysref",                                     set_sysref_command,                                     2,  1},
{"",                                               NULL,                                                   0,  0}    //  Must  be  last
};

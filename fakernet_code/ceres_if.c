#include <assert.h>
#include "ceres_if.h"
#include "gpio.h"
#include "iic.h"
#include "lmk_if.h"
#include "ads_if.h"
#include "jesd.h"
#include "jesd_phy.h"
#include "reset_gen_if.h"
#include "data_pipeline.h"

#define  ADC_A_AXI_ADDR             0x100100
#define  ADC_B_AXI_ADDR             0x100200
#define  ADC_C_AXI_ADDR             0x100400
#define  ADC_D_AXI_ADDR             0x100500

#define  GPIO_AXI_ADDR              0x400000
#define  GPIO_HERMES_POWER_OFFSET   0
#define  GPIO_ADC_RESET_OFFSET      1
#define  GPIO_ADC_PDN_OFFSET        2
#define  GPIO_LMK_RESET_OFFSET      3

#define  LMK_A_AXI_ADDR             0x100000
#define  LMK_B_AXI_ADDR             0x100300
#define  CERES_LMK_AXI_ADDR         0x100600
#define  RESET_GEN_AXI_ADDR         0x500000
#define  IIC_AXI_ADDR               0x300000
#define  CLKGEN_IIC_AXI_ADDR        0xD000
#define  JESD_A_AXI_ADDR            0x204000
#define  JESD_B_AXI_ADDR            0x205000
#define  JESD_C_AXI_ADDR            0x206000
#define  JESD_D_AXI_ADDR            0x207000
#define  JESD_PHY_A_AXI_ADDR        0x200000
#define  JESD_PHY_B_AXI_ADDR        0x201000
#define  JESD_PHY_C_AXI_ADDR        0x202000
#define  JESD_PHY_D_AXI_ADDR        0x203000
#define  DATA_PIPELINE_0_ADDR       0x0
#define  CLKGEN_IIC_ADDR            0xD0

#define RESET_GEN_JESD_BIT 0
#define RESET_GEN_PIPELINE_BIT 1
#define RESET_GEN_AXI_BIT 2
#define RESET_GEN_AURORA_BIT 3
#define RESET_GEN_FNET_BIT 4
#define RESET_GEN_FIFO_BIT 5

const uint32_t CERES_SAFE_READ_ADDRESS = GPIO_AXI_ADDR;

struct CERES_IF {
    AXI_QSPI* lmk_a;
    AXI_QSPI* lmk_b;
    AXI_QSPI* ceres_lmk;
    AXI_QSPI* adc_a;
    AXI_QSPI* adc_b;
    AXI_QSPI* adc_c;
    AXI_QSPI* adc_d;
    AXI_GPIO* axi_gpio;
    AXI_RESET_GEN* reset_gen;
    AXI_IIC* iic_main;
    AXI_IIC* clk_gen_iic;
    AXI_JESD* jesd_a;
    AXI_JESD* jesd_b;
    AXI_JESD* jesd_c;
    AXI_JESD* jesd_d;
    AXI_JESD_PHY* jesd_phy_a;
    AXI_JESD_PHY* jesd_phy_b;
    AXI_JESD_PHY* jesd_phy_c;
    AXI_JESD_PHY* jesd_phy_d;
    AXI_DATA_PIPELINE* pipeline;
};

static struct CERES_IF* ceres = NULL;

static struct CERES_IF* get_ceres_handle() {
    if(ceres == NULL) {
        ceres = malloc(sizeof(struct CERES_IF));
        ceres->lmk_a = new_lmk_spi("lmk_a", LMK_A_AXI_ADDR);
        ceres->lmk_b = new_lmk_spi("lmk_b", LMK_B_AXI_ADDR);
        ceres->ceres_lmk = new_lmk_spi("ceres_lmk", CERES_LMK_AXI_ADDR);
        ceres->adc_a = new_ads_spi("adc_a", ADC_A_AXI_ADDR);
        ceres->adc_b = new_ads_spi("adc_b", ADC_B_AXI_ADDR);
        ceres->adc_c = new_ads_spi("adc_c", ADC_C_AXI_ADDR);
        ceres->adc_d = new_ads_spi("adc_d", ADC_D_AXI_ADDR);
        ceres->axi_gpio = new_gpio("gpio", GPIO_AXI_ADDR);
        ceres->reset_gen = new_reset_gen("reset_gen", RESET_GEN_AXI_ADDR);
        ceres->iic_main = new_iic("iic_main", IIC_AXI_ADDR,1, 1);
        ceres->clk_gen_iic = new_iic("clkgen_iic", CLKGEN_IIC_AXI_ADDR, 2, 2);
        ceres->jesd_a = new_jesd("jesd_a", JESD_A_AXI_ADDR);
        ceres->jesd_b = new_jesd("jesd_b", JESD_B_AXI_ADDR);
        ceres->jesd_c = new_jesd("jesd_c", JESD_C_AXI_ADDR);
        ceres->jesd_d = new_jesd("jesd_d", JESD_D_AXI_ADDR);
        ceres->jesd_phy_a = new_jesd_phy("jesd_phy_a", JESD_PHY_A_AXI_ADDR);
        ceres->jesd_phy_b = new_jesd_phy("jesd_phy_b", JESD_PHY_B_AXI_ADDR);
        ceres->jesd_phy_c = new_jesd_phy("jesd_phy_c", JESD_PHY_C_AXI_ADDR);
        ceres->jesd_phy_d = new_jesd_phy("jesd_phy_d", JESD_PHY_D_AXI_ADDR);
        ceres->pipeline = new_data_pipeline_if("dp0", DATA_PIPELINE_0_ADDR);
    }
    return ceres;
}

static uint32_t set_adc_power_command(uint32_t *args) {
    uint32_t val = args[0];
    return write_gpio_value(get_ceres_handle()->axi_gpio, GPIO_HERMES_POWER_OFFSET, val);
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
        case(2):
            this_lmk = get_ceres_handle()->ceres_lmk;
            break;
        default:
            // TODO really need to add an error string!
            printf("Invalid LMK given\n");
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

static uint32_t read_lmk_pll_status_command(uint32_t* args) { 
    uint32_t which_lmk = args[0];
    uint32_t which_pll = args[1];
    uint32_t status;
    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }
    if(which_pll == 0) {
        status = read_pll1_dld_status_reg(lmk);
    }
    else {
        status = read_pll2_dld_status_reg(lmk);
    }
    return status;
}

static uint32_t clear_lmk_pll_status_command(uint32_t* args) { 
    uint32_t which_lmk = args[0];
    uint32_t which_pll = args[1];
    uint32_t ret;
    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }
    if(which_pll == 0) {
        ret = clear_pll1_dld_status_reg(lmk);
    }
    else {
        ret = clear_pll2_dld_status_reg(lmk);
    }
    return ret;
}

static uint32_t read_lmk_dac_value_command(uint32_t* args) {
    uint32_t which_lmk = args[0];
    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }
    return read_lmk_dac(lmk);
}

static uint32_t write_lmk_dac_value_command(uint32_t* args) {
    uint32_t which_lmk = args[0];
    uint32_t value = args[1];
    AXI_QSPI* lmk = lmk_switch(which_lmk);
    if(!lmk) {
        return -1;
    }
    return write_lmk_dac(lmk, value);
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

static uint32_t jesd_error_count_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }
    return jesd_error_count(jesd, args, 1);
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

static void generic_sys_reset(uint32_t bit_mask) {
    write_reset_gen_mask(get_ceres_handle()->reset_gen, bit_mask);
    write_reset_gen_length(get_ceres_handle()->reset_gen, 5000);
    reset_gen_do_reset(get_ceres_handle()->reset_gen);
}

static uint32_t fnet_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_FNET_BIT);
    return 0;
}
static uint32_t pipeline_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_PIPELINE_BIT);
    return 0;
}

static uint32_t fifo_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_FIFO_BIT);
    return 0;
}

static uint32_t aurora_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_AURORA_BIT);
    return 0;
}

static uint32_t jesd_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_JESD_BIT);
    return 0;
}

static uint32_t axi_sys_reset_command(uint32_t* args) {
    UNUSED(args);
    generic_sys_reset(1<<RESET_GEN_AXI_BIT);
    return 0;
}

// This command reads a GPIO block that (in principle) is hooked up to a
// binary counter that counts clock ticks while SYNC is low. 
// Count is reset by JESD SYS Reset
// TODO, copy/pasted the code for A & B...dry it out!
/*
static uint32_t jesd_a_sync_rate_command(uint32_t *args) {
    UNUSED(args);
    uint32_t GPIO_DATA1_ADDR = 0x0;
    uint32_t first = read_gpio_value(get_ceres_handle()->gpio1, GPIO_DATA1_ADDR);
    write_gpio_value(get_ceres_handle()->reset_gen,
    usleep(500e3);
    uint32_t second = read_gpio_value(get_ceres_handle()->gpio1, GPIO_DATA1_ADDR);
    // Don't need to worry about rollover, subtraction handles it okay even
    // accross the 32-bit rollover.
    return second - first;
}
*/

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
    uint32_t mask = args[0];
    return write_gpio_value(get_ceres_handle()->axi_gpio, GPIO_ADC_PDN_OFFSET, mask);
}

static uint32_t get_adc_pdn_command(uint32_t* args) {
    UNUSED(args);
    return read_gpio_value(get_ceres_handle()->axi_gpio, GPIO_ADC_PDN_OFFSET);
}

static uint32_t adc_reset_command(uint32_t* args) {
    uint32_t mask = args[0];
    write_gpio_value(get_ceres_handle()->axi_gpio, GPIO_ADC_RESET_OFFSET, mask);
    return 0;
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

static uint32_t read_data_pipeline_global_depth_command(uint32_t* args) {
    UNUSED(args);
    return read_global_depth(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_global_depth_command(uint32_t* args) {
    int value = args[0];
    return write_global_depth(get_ceres_handle()->pipeline, value);
}

static uint32_t read_data_pipeline_invalid_count_command(uint32_t* args) {
    UNUSED(args);
    return read_invalid_count(get_ceres_handle()->pipeline);
}

static uint32_t read_data_pipeline_status_command(uint32_t* args) {
    UNUSED(args);
    return read_fifo_status_reg(get_ceres_handle()->pipeline);
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

static uint32_t read_data_pipeline_trigger_sum_width_command(uint32_t* args) {
    UNUSED(args);
    return read_trig_sum_width(get_ceres_handle()->pipeline);
}

static uint32_t write_data_pipeline_trigger_sum_width_command(uint32_t* args) {
    uint32_t length = args[0];
    return write_trig_sum_width(get_ceres_handle()->pipeline, length);
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

static uint32_t set_local_trigger_params_command(uint32_t* args) {
    uint32_t rate = args[0];   // Number of counts between each readout
    uint32_t length = args[1]; // Amount of data in each readout

    if(rate == 0 || length == 0) {
        printf("Rate and length parameter canot be zero\n");
        return -1;
    }

    rate = 250e6 / rate; // Idk if 100e6 is the right value here, but its close enough probably
    uint32_t ret = 0;
    ret = write_local_trigger_length(get_ceres_handle()->pipeline, length);
    ret |= write_local_trigger_count_reset_value(get_ceres_handle()->pipeline, rate);

    return ret;
}

static uint32_t set_trigger_params_command(uint32_t* args) {
    uint32_t mask = args[0];
    uint32_t width = args[1];
    uint32_t threshold = args[2];
    uint32_t ret = 0;
    ret = write_channel_mask(get_ceres_handle()->pipeline, mask);
    ret |= write_trig_sum_width(get_ceres_handle()->pipeline, width);
    ret |= write_threshold(get_ceres_handle()->pipeline, threshold);
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

AXI_JESD_PHY* jesd_phy_switch(uint32_t which_jesd) {
    AXI_JESD_PHY* this_jesd_phy = NULL;
    switch(which_jesd) {
        case(0):
            this_jesd_phy = get_ceres_handle()->jesd_phy_a;
            break;
        case(1):
            this_jesd_phy = get_ceres_handle()->jesd_phy_b;
            break;
        case(2):
            this_jesd_phy = get_ceres_handle()->jesd_phy_c;
            break;
        case(3):
            this_jesd_phy = get_ceres_handle()->jesd_phy_d;
            break;
        default:
            // TODO really need to add an error string!
            return NULL;
    }
    return this_jesd_phy;
}

static uint32_t write_jesd_phy_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t addr = args[1];
    uint32_t val = args[2];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return write_jesd_phy(this_jesd_phy, addr, val);

}

static uint32_t read_jesd_phy_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t addr = args[1];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return read_jesd_phy(this_jesd_phy, addr);
}

static uint32_t set_jesd_lpmen_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t val = args[1];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return write_lpmen(this_jesd_phy, val);
}
static uint32_t read_jesd_lpmen_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return read_lpmen(this_jesd_phy);
}

static uint32_t reset_lpmen_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return reset_lpmen(this_jesd_phy);

}

static uint32_t read_insertion_loss_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return read_insertion_loss(this_jesd_phy);
}


static uint32_t read_jesd_drp_common_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t drp_addr = args[1];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return read_drp_common(this_jesd_phy, drp_addr);
}

static uint32_t write_jesd_drp_common_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t drp_addr = args[1];
    uint16_t drp_data = args[2] & 0xFFFF;

    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return write_drp_common(this_jesd_phy, drp_addr, drp_data);
}

static uint32_t read_drp_interface_selector_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return read_jesd_phy_drp_interface_selector(this_jesd_phy);
}

static uint32_t write_drp_interface_selector_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t interface = args[1];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return write_jesd_phy_drp_interface_selector(this_jesd_phy, interface);
}

static uint32_t read_jesd_drp_channel_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t drp_addr = args[1];
    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return read_drp_transceiver(this_jesd_phy, drp_addr);
}

static uint32_t write_jesd_drp_channel_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t drp_addr = args[1];
    uint16_t drp_data = args[2] & 0xFFFF;

    AXI_JESD_PHY* this_jesd_phy = jesd_phy_switch(which_jesd);
    if(!this_jesd_phy) {
        return -1;
    }
    return write_drp_transceiver(this_jesd_phy, drp_addr, drp_data);
}

static uint32_t read_build_tag_command(uint32_t* args) {
    return read_build_info(get_ceres_handle()->pipeline);
}

static uint32_t read_jesd_rx_lane_buffer_adj_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    int i;
    uint32_t rx_lane_offset[] = {0x830, 0x870, 0x8B0, 0x8F0};

    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }

    for(i=0; i<4;i++) {
        args[i] = read_jesd(jesd, rx_lane_offset[i]);
    }
    return 0;
}

static uint32_t read_jesd_buffer_delay_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }
    return read_jesd_buffer_delay(jesd);
}

static uint32_t write_jesd_buffer_delay_command(uint32_t* args) {
    uint32_t which_jesd = args[0];
    uint32_t data = args[1];
    AXI_JESD* jesd = jesd_switch(which_jesd);
    if(!jesd) { return -1; }
    return write_jesd_buffer_delay(jesd, data);
}

ServerCommand ceres_commands[] = {
{"read_iic_reg",NULL,                              read_iic_block_command,                                 2,  1, 0, 0},
{"write_iic_reg",NULL,                             write_iic_block_command,                                3,  0, 0, 0},
{"read_iic_bus",NULL,                              read_iic_bus_command,                                   2,  1, 0, 0},
{"write_iic_bus",NULL,                             write_iic_bus_command,                                  3,  1, 0, 0},
{"read_iic_bus_with_reg",NULL,                     read_iic_bus_with_reg_command,                          3,  1, 0, 0},
{"write_iic_bus_with_reg",NULL,                    write_iic_bus_with_reg_command,                         4,  1, 0, 0},
{"write_clkgen_iic",NULL,                          write_clkgen_iic_command,                               3,  1, 0, 0},
{"read_clkgen_iic",NULL,                           read_clkgen_iic_command,                                2,  1, 0, 0},
{"read_clkgen_gpio",NULL,                          read_clkgen_gpio_command,                               1,  1, 0, 0},
{"write_clkgen_gpio",NULL,                         write_clkgen_gpio_command,                              2,  1, 0, 0},
{"write_clkgen_register",NULL,                     write_clkgen_register_command,                          3,  1, 0, 0},
{"read_clkgen_register",NULL,                      read_clkgen_register_command,                           2,  1, 0, 0},
{"set_adc_power",NULL,                             set_adc_power_command,                                  2,  1, 0, 0},
{"read_ads",NULL,                                  read_ads_if_command,                                    3,  1, 0, 0},
{"write_ads",NULL,                                 write_ads_if_command,                                   4,  1, 0, 0},
{"write_adc_spi",NULL,                             write_adc_spi_command,                                  5,  1, 0, 0},
{"ads_spi_data_available",NULL,                    ads_spi_data_available_command,                         2,  1, 0, 0},
{"ads_spi_pop",NULL,                               ads_spi_data_pop_command,                               2,  1, 0, 0},
{"write_lmk_if",NULL,                              write_lmk_if_command,                                   4,  1, 0, 0},
{"read_lmk_if",NULL,                               read_lmk_if_command,                                    3,  1, 0, 0},
{"write_lmk_spi",NULL,                             write_lmk_spi_command,                                  5,  1, 0, 0},
{"lmk_spi_data_available",NULL,                    lmk_spi_data_available_command,                         2,  1, 0, 0},
{"lmk_spi_data_pop",NULL,                          lmk_spi_data_pop_command,                               2,  1, 0, 0},
{"read_lmk_pll_status",NULL,                       read_lmk_pll_status_command,                            3,  1, 0, 0},
{"clear_lmk_pll_status",NULL,                      clear_lmk_pll_status_command,                           3,  1, 0, 0},
{"read_lmk_dac_value",NULL,                        read_lmk_dac_value_command,                             2,  1, 0, 0},
{"write_lmk_dac_value",NULL,                       write_lmk_dac_value_command,                            3,  1, 0, 0},
{"jesd_read",NULL,                                 jesd_read_command,                                      3,  1, 0, 0},
{"jesd_write",NULL,                                jesd_write_command,                                     4,  1, 0, 0},
{"jesd_error_count",NULL,                          jesd_error_count_command,                               2,  4, 0, 0},
{"jesd_error_rate",NULL,                           jesd_error_rate_command,                                2,  4, 0, 0},
{"jesd_reset",NULL,                                jesd_reset_command,                                     2,  1, 0, 0},
{"fnet_sys_reset",NULL,                            fnet_sys_reset_command,                                 1,  1, 0, 0},
{"pipeline_sys_reset",NULL,                        pipeline_sys_reset_command,                             1,  1, 0, 0},
{"fifo_sys_reset",NULL,                            fifo_reset_command,                                     1,  1, 0, 0},
{"aurora_sys_reset",NULL,                          aurora_sys_reset_command,                               1,  1, 0, 0},
{"jesd_sys_reset",NULL,                            jesd_sys_reset_command,                                 1,  1, 0, 0},
{"axi_sys_reset",NULL,                             axi_sys_reset_command,                                  1,  1, 0, 0},
{"jesd_is_synced",NULL,                            jesd_is_synced_command,                                 2,  1, 0, 0},
{"jesd_set_sync_error_reporting",NULL,             jesd_set_sync_error_reporting_command,                  3,  1, 0, 0},
//{"read_all_error_rates",NULL,                    read_all_error_rates_command,                           1,  1, 0, 00},
{"set_adc_pdn",NULL,                               set_adc_pdn_command,                                    2,  1, 0, 0},
{"get_adc_pdn",NULL,                               get_adc_pdn_command,                                    1,  1, 0, 0},
{"adc_reset",NULL,                                 adc_reset_command,                                      2,  1, 0, 0},
{"read_data_pipeline_threshold",NULL,              read_data_pipeline_threshold_command,                   1,  1, 0, 0},
{"write_data_pipeline_threshold",NULL,             write_data_pipeline_threshold_command,                  2,  1, 0, 0},
{"read_data_pipeline_channel_mask",NULL,           read_data_pipeline_channel_mask_command,                1,  1, 0, 0},
{"write_data_pipeline_channeL_mask",NULL,          write_data_pipeline_channeL_mask_command,               2,  1, 0, 0},
{"read_data_pipeline_depth",NULL,                  read_data_pipeline_depth_command,                       2,  1, 0, 0},
{"write_data_pipeline_depth",NULL,                 write_data_pipeline_depth_command,                      3,  1, 0, 0},
{"read_data_pipeline_global_depth",NULL,           read_data_pipeline_global_depth_command,                1,  1, 0, 0},
{"write_data_pipeline_global_depth",NULL,          write_data_pipeline_global_depth_command,               2,  1, 0, 0},
{"write_data_pipeline",NULL,                       write_data_pipeline_command,                            3,  1, 0, 0},
{"read_data_pipeline",NULL,                        read_data_pipeline_command,                             2,  1, 0, 0},
{"read_data_pipeline_invalid_count",NULL,          read_data_pipeline_invalid_count_command,               1,  1, 0, 0},
{"read_data_pipeline_status",NULL,                 read_data_pipeline_status_command,                      1,  1, 0, 0},
{"read_data_pipeline_local_trigger_enable",NULL,   read_data_pipeline_local_trigger_enable_command,        1,  1, 0, 0},
{"write_data_pipeline_local_trigger_enable",NULL,  write_data_pipeline_local_trigger_enable_command,       2,  1, 0, 0},
{"read_data_pipeline_local_trigger_mode",NULL,     read_data_pipeline_local_trigger_mode_command,          1,  1, 0, 0},
{"write_data_pipeline_local_trigger_mode",NULL,    write_data_pipeline_local_trigger_mode_command,         2,  1, 0, 0},
{"read_data_pipeline_local_trigger_length",NULL,   read_data_pipeline_local_trigger_length_command,        1,  1, 0, 0},
{"write_data_pipeline_local_trigger_length",NULL,  write_data_pipeline_local_trigger_length_command,       2,  1, 0, 0},
{"read_data_pipeline_trigger_sum_width",NULL,      read_data_pipeline_trigger_sum_width_command,           1,  1, 0, 0},
{"write_data_pipeline_trigger_sum_width",NULL,     write_data_pipeline_trigger_sum_width_command,          2,  1, 0, 0},
{"read_data_pipeline_trigger_count_reset_value",NULL,   read_data_pipeline_trigger_count_reset_value_command,   1,  1, 0, 0},
{"write_data_pipeline_trigger_count_reset_value",NULL,  write_data_pipeline_trigger_count_reset_value_command,  2,  1, 0, 0},
{"read_data_pipeline_trigger_enable",NULL,         read_data_pipeline_trigger_enable_command,              1,  1, 0, 0},
{"write_data_pipeline_trigger_enable",NULL,        write_data_pipeline_trigger_enable_command,             2,  1, 0, 0},
{"set_local_trigger_params",NULL,                  set_local_trigger_params_command,                       3,  1, 0, 0},
{"set_trigger_params",NULL,                        set_trigger_params_command,                             4,  1, 0, 0},
{"set_sysref",NULL,                                set_sysref_command,                                     3,  1, 0, 0},
{"write_jesd_phy",NULL,                            write_jesd_phy_command,                                         4,  1, 0, 0},
{"read_jesd_phy",NULL,                             read_jesd_phy_command,                                          3,  1, 0, 0},
{"write_lpmen",NULL,                               set_jesd_lpmen_command,                                 3,  1, 0, 0},
{"read_lpmen",NULL,                                read_jesd_lpmen_command,                                2,  1, 0, 0},
{"reset_lpmen",NULL,                               reset_lpmen_command,                                    2,  1, 0, 0},
{"read_insertion_loss",NULL,                       read_insertion_loss_command,                            2,  1, 0, 0},
{"read_jesd_drp_common",NULL,                      read_jesd_drp_common_command,                           3,  1, 0, 0},
{"write_jesd_drp_common",NULL,                     write_jesd_drp_common_command,                          4,  1, 0, 0},
{"read_drp_interface_selector",NULL,               read_drp_interface_selector_command,                    2,  1, 0, 0},
{"write_drp_interface_selector",NULL,              write_drp_interface_selector_command,                   3,  1, 0, 0},
{"read_jesd_drp_channel",NULL,                     read_jesd_drp_channel_command,                          3,  1, 0, 0},
{"write_jesd_drp_channel",NULL,                    write_jesd_drp_channel_command,                         4,  1, 0, 0},
{"read_build_tag",NULL,                            read_build_tag_command,                                 1,  1, 0, 0},
{"read_jesd_rx_lane_buffer_adj",NULL,              read_jesd_rx_lane_buffer_adj_command,                   2,  4, 0, 0},
{"write_jesd_buffer_delay",NULL,                   write_jesd_buffer_delay_command,                        3,  1, 0, 0},
{"read_jesd_buffer_delay",NULL,                    read_jesd_buffer_delay_command,                         2,  1, 0, 0},
{"",NULL,                                          NULL,                                                   0,  0, 0, 0}    //  Must  be  last
};

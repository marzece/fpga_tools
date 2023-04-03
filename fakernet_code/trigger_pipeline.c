#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "trigger_pipeline.h"


int read_addr(uint32_t, uint32_t, uint32_t*);
int double_read_addr(uint32_t, uint32_t, uint32_t*);
int write_addr(uint32_t, uint32_t, uint32_t);

#define  NUM_THRESHOLDS                            5
#define  NUM_EXTERNALS                             3
#define  CHANNELS_PER_EXTERNAL                     3
#define  NUM_TRIGGERS                              10
#define  REGISTER_WIDTH                            0x4
#define  RESET_REG_OFFSET                          0x0
#define  PULSE_GENERATOR_ENABLE_MASK               0x8
#define  KICKER_TRIGGER_LENGTH_OFFSET              0xC
#define  LED_TRIGGER_LENGTH_OFFSET                 0x10
#define  SYNC_REG_OFFSET                           0x14
#define  TRIG_VAR_COMBINER_MULTIPLICITY_WIDTH      0x20
#define  INNER_MULTIPLICITY_THRESHOLD_BASE_OFFSET  0x30
#define  INNER_ESUM_THRESHOLD_BASE_OFFSET          0x50
#define  VETO_MULTIPLICITY_THRESHOLD_BASE_OFFSET   0x70
#define  VETO_ESUM_THRESHOLD_BASE_OFFSET           0x90

#define  INNER_MULTIPLICITY_DELAY_BASE_OFFSET      0xD0
#define  INNER_ENERGY_SUM_DELAY_BASE_OFFSET        (INNER_MULTIPLICITY_DELAY_BASE_OFFSET  +  REGISTER_WIDTH*NUM_THRESHOLDS)
#define  VETO_MULTIPLICITY_DELAY_BASE_OFFSET       (INNER_ENERGY_SUM_DELAY_BASE_OFFSET    +  REGISTER_WIDTH*NUM_THRESHOLDS)
#define  VETO_ENERGY_SUM_DELAY_BASE_OFFSET         (VETO_MULTIPLICITY_DELAY_BASE_OFFSET   +  REGISTER_WIDTH*NUM_THRESHOLDS)
#define  EXTERNAL_DELAY_OFFSET                     (VETO_ENERGY_SUM_DELAY_BASE_OFFSET     +  REGISTER_WIDTH*NUM_THRESHOLDS)

#define  INNER_MULTIPLICITY_GATE_BASE_OFFSET       0x150
#define  INNER_ENERGY_SUM_GATE_BASE_OFFSET         (INNER_MULTIPLICITY_GATE_BASE_OFFSET   +  REGISTER_WIDTH*NUM_THRESHOLDS)
#define  VETO_MULTIPLICITY_GATE_BASE_OFFSET        (INNER_ENERGY_SUM_GATE_BASE_OFFSET     +  REGISTER_WIDTH*NUM_THRESHOLDS)
#define  VETO_ENERGY_SUM_GATE_BASE_OFFSET          (VETO_MULTIPLICITY_GATE_BASE_OFFSET    +  REGISTER_WIDTH*NUM_THRESHOLDS)
#define  EXTERNAL_GATE_OFFSET                      (VETO_ENERGY_SUM_GATE_BASE_OFFSET      +  REGISTER_WIDTH*NUM_THRESHOLDS)
#define  TRIGGER_MASK_BASE_OFFSET                  0x1D0
#define  TRIGGER_VETO_MASK_BASE_OFFSET             0x200
#define  TRIGGER_LENGTH_BASE_OFFSET                0x230

#define  THRESHOLD_ENABLE_MASK_OFFSET              0x300
#define  TRIGGER_ENABLE_MASK_OFFSET                0x304
#define  SELF_TRIGGER_ENABLE_OFFSET                0x308

AXI_TRIGGER_PIPELINE* new_trig_pipeline_if(const char* name, uint32_t axi_addr) {
    AXI_TRIGGER_PIPELINE* ret = malloc(sizeof(AXI_TRIGGER_PIPELINE));
    ret->name = name;
    ret->axi_addr = axi_addr;
    return ret;
}

uint32_t read_trig_pipeline_value(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t offset) {
    uint32_t ret;
    if(double_read_addr(tp_axi->axi_addr, offset, &ret)) {
        // TODO!!!
        return -1;
    }
    return ret;
}

uint32_t write_trig_pipeline_value(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t offset, uint32_t data) {
    return write_addr(tp_axi->axi_addr, offset, data);
}

uint32_t read_multiplicity_width(AXI_TRIGGER_PIPELINE* tp_axi) {
    return read_trig_pipeline_value(tp_axi, TRIG_VAR_COMBINER_MULTIPLICITY_WIDTH);
}

uint32_t write_multiplicity_width(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t width) {
    return write_trig_pipeline_value(tp_axi, TRIG_VAR_COMBINER_MULTIPLICITY_WIDTH, width);
}

uint32_t read_inner_multiplicity_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset =  INNER_MULTIPLICITY_THRESHOLD_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_inner_multiplicity_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t threshold) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_MULTIPLICITY_THRESHOLD_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, threshold);
}

uint32_t read_veto_multiplicity_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset =  VETO_MULTIPLICITY_THRESHOLD_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_veto_multiplicity_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t data) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }

    uint32_t offset = VETO_MULTIPLICITY_THRESHOLD_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, data);
}

uint32_t read_inner_esum_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_ESUM_THRESHOLD_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_inner_esum_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t data) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_ESUM_THRESHOLD_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, data);
}

uint32_t read_veto_esum_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_ESUM_THRESHOLD_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_veto_esum_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t data) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_ESUM_THRESHOLD_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, data);
}

uint32_t read_inner_multiplicity_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_MULTIPLICITY_DELAY_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_inner_multiplicity_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_MULTIPLICITY_DELAY_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_veto_multiplicity_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_MULTIPLICITY_DELAY_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_veto_multiplicity_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_MULTIPLICITY_DELAY_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_inner_esum_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_ENERGY_SUM_DELAY_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_inner_esum_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_ENERGY_SUM_DELAY_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_veto_esum_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_ENERGY_SUM_DELAY_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_veto_esum_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_ENERGY_SUM_DELAY_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_external_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t which_external, uint32_t channel) {
    if(which_external >= NUM_EXTERNALS || channel >= CHANNELS_PER_EXTERNAL) {
        return -1;
    }
    uint32_t offset = EXTERNAL_DELAY_OFFSET + (which_external*CHANNELS_PER_EXTERNAL + channel)*REGISTER_WIDTH;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_external_delay(AXI_TRIGGER_PIPELINE* tp_axi,  uint32_t which_external, uint32_t channel, uint32_t delay) {
    if(which_external >= NUM_EXTERNALS || channel >= CHANNELS_PER_EXTERNAL) {
        return -1;
    }
    uint32_t offset = EXTERNAL_DELAY_OFFSET + (which_external*CHANNELS_PER_EXTERNAL + channel)*REGISTER_WIDTH;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_inner_multiplicity_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_MULTIPLICITY_GATE_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_inner_multiplicity_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_MULTIPLICITY_GATE_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_veto_multiplicity_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_MULTIPLICITY_GATE_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_veto_multiplicity_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_MULTIPLICITY_GATE_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_inner_esum_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_ENERGY_SUM_GATE_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_inner_esum_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = INNER_ENERGY_SUM_GATE_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_veto_esum_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_ENERGY_SUM_GATE_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_veto_esum_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay) {
    if(channel >= NUM_THRESHOLDS) {
        // TODO set error string
        return -1;
    }
    uint32_t offset = VETO_ENERGY_SUM_GATE_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_external_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t which_external, uint32_t channel) {
    if(which_external >= NUM_EXTERNALS || channel >= CHANNELS_PER_EXTERNAL) {
        return -1;
    }
    uint32_t offset = EXTERNAL_GATE_OFFSET + (which_external*CHANNELS_PER_EXTERNAL + channel)*REGISTER_WIDTH;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_external_gate(AXI_TRIGGER_PIPELINE* tp_axi,  uint32_t which_external, uint32_t channel, uint32_t delay) {
    if(which_external >= NUM_EXTERNALS || channel >= CHANNELS_PER_EXTERNAL) {
        return -1;
    }
    uint32_t offset = EXTERNAL_GATE_OFFSET + (which_external*CHANNELS_PER_EXTERNAL + channel)*REGISTER_WIDTH;
    return write_trig_pipeline_value(tp_axi, offset, delay);
}

uint32_t read_trigger_mask(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_TRIGGERS) {
        return -1;
    }
    uint32_t offset = TRIGGER_MASK_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_trigger_mask(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t mask) {
    if(channel >= NUM_TRIGGERS) {
        return -1;
    }
    uint32_t offset = TRIGGER_MASK_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, mask);
}

uint32_t read_trigger_veto_mask(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_TRIGGERS) {
        return -1;
    }
    uint32_t offset = TRIGGER_VETO_MASK_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t write_trigger_veto_mask(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t mask) {
    if(channel >= NUM_TRIGGERS) {
        return -1;
    }
    uint32_t offset = TRIGGER_VETO_MASK_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, mask);
}

uint32_t read_trigger_length(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel) {
    if(channel >= NUM_TRIGGERS) {
        return -1;
    }
    uint32_t offset = TRIGGER_LENGTH_BASE_OFFSET + REGISTER_WIDTH*channel;
    return read_trig_pipeline_value(tp_axi, offset);
}

uint32_t read_led_trigger_length(AXI_TRIGGER_PIPELINE *tp_axi) {
    return read_trig_pipeline_value(tp_axi, LED_TRIGGER_LENGTH_OFFSET);
}
uint32_t write_led_trigger_length(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t val) {
    return write_trig_pipeline_value(tp_axi, LED_TRIGGER_LENGTH_OFFSET, val);
}
uint32_t read_kicker_trigger_length(AXI_TRIGGER_PIPELINE *tp_axi) {
    return read_trig_pipeline_value(tp_axi, KICKER_TRIGGER_LENGTH_OFFSET);
}
uint32_t write_kicker_trigger_length(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t val) {
    return write_trig_pipeline_value(tp_axi, KICKER_TRIGGER_LENGTH_OFFSET, val);
}

uint32_t write_trigger_length(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t mask) {
    if(channel >= NUM_TRIGGERS) {
        return -1;
    }
    uint32_t offset = TRIGGER_LENGTH_BASE_OFFSET + REGISTER_WIDTH*channel;
    return write_trig_pipeline_value(tp_axi, offset, mask);
}

uint32_t read_pulse_generator_enable(AXI_TRIGGER_PIPELINE* tp_axi) {
    return read_trig_pipeline_value(tp_axi, PULSE_GENERATOR_ENABLE_MASK);
}

uint32_t write_pulse_generator_enable(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t value) {
    value = value ? 1 : 0; // 1 or 0 are the only valid values
    return write_trig_pipeline_value(tp_axi, PULSE_GENERATOR_ENABLE_MASK, value);
}

uint32_t read_threshold_enable_mask(AXI_TRIGGER_PIPELINE* tp_axi) {
    return read_trig_pipeline_value(tp_axi, THRESHOLD_ENABLE_MASK_OFFSET);
}

uint32_t write_threshold_enable_mask(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t val) {
    return write_trig_pipeline_value(tp_axi, THRESHOLD_ENABLE_MASK_OFFSET, val);
}

uint32_t read_trigger_mask_enable(AXI_TRIGGER_PIPELINE* tp_axi) {
    return read_trig_pipeline_value(tp_axi, TRIGGER_ENABLE_MASK_OFFSET);
}

uint32_t write_trigger_mask_enable(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t val) {
    return write_trig_pipeline_value(tp_axi, TRIGGER_ENABLE_MASK_OFFSET, val);
}

uint32_t read_self_trigger_enable(AXI_TRIGGER_PIPELINE *tp_axi) {
    return read_trig_pipeline_value(tp_axi, SELF_TRIGGER_ENABLE_OFFSET);
}

uint32_t write_self_trigger_enable(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t val) {
    return write_trig_pipeline_value(tp_axi, SELF_TRIGGER_ENABLE_OFFSET, val);
}

uint32_t read_sync_length(AXI_TRIGGER_PIPELINE* tp_axi) {
    uint32_t word = read_trig_pipeline_value(tp_axi, SYNC_REG_OFFSET);
    word = (word >> 4) & 0xFFFF;
    return word;
}

uint32_t do_sync(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t length) {
    uint32_t word = ((length & 0xFFFF) << 4) | 0x1;
    uint32_t ret = 0;
    ret = write_trig_pipeline_value(tp_axi, SYNC_REG_OFFSET, word);
    if(ret) { return ret;}

    // TODO from looking at the sync_gen RTL code it looks like the SYNC pulse
    // will only be emitted once bit-0 of the register is '1' and will stop
    // only once that bit is un-set.
    // This behavior should be tested & possibly re-thought cause it's at least
    // unintuitive.
    // Unset bit-0
    word &= 0xFFFFE;
    return write_trig_pipeline_value(tp_axi, SYNC_REG_OFFSET, word);
}

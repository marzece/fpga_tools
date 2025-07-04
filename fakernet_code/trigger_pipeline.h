#ifndef __DATA_PIPELINE__
#define __DATA_PIPELINE__
#include <inttypes.h>
#include <stdlib.h>

typedef struct AXI_TRIGGER_PIPELINE {
    const char* name;
    uint32_t axi_addr;
} AXI_TRIGGER_PIPELINE;

AXI_TRIGGER_PIPELINE* new_trig_pipeline_if(const char* name, uint32_t axi_addr);

uint32_t write_trig_pipeline_value(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t offset, uint32_t data);
uint32_t read_trig_pipeline_value(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t offset);
uint32_t read_multiplicity_width(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t write_multiplicity_width(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t width);

uint32_t read_inner_multiplicity_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_inner_multiplicity_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t threshold);
uint32_t read_veto_multiplicity_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_veto_multiplicity_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t threshold);
uint32_t read_inner_esum_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_inner_esum_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t threshold);
uint32_t read_veto_esum_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_veto_esum_threshold(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t threshold);

uint32_t read_inner_multiplicity_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_inner_multiplicity_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay);
uint32_t read_inner_esum_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_inner_esum_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay);
uint32_t read_veto_multiplicity_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_veto_multiplicity_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay);
uint32_t read_veto_esum_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_veto_esum_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t delay);
uint32_t read_external_delay(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t which_external, uint32_t channel);
uint32_t write_external_delay(AXI_TRIGGER_PIPELINE* tp_axi,  uint32_t which_external, uint32_t channel, uint32_t delay);

uint32_t read_inner_multiplicity_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_inner_multiplicity_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t value);
uint32_t read_inner_esum_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_inner_esum_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t value);
uint32_t read_veto_multiplicity_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);

uint32_t write_veto_multiplicity_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t value);
uint32_t read_veto_esum_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_veto_esum_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t value);
uint32_t read_external_gate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t which_external, uint32_t channel);
uint32_t write_external_gate(AXI_TRIGGER_PIPELINE* tp_axi,  uint32_t which_external, uint32_t channel, uint32_t value);

uint32_t write_trigger_mask(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t mask);
uint32_t read_trigger_mask(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_trigger_veto_mask(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t mask);
uint32_t read_trigger_veto_mask(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);
uint32_t write_trigger_length(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel, uint32_t mask);
uint32_t read_trigger_length(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t channel);

uint32_t read_led_trigger_length(AXI_TRIGGER_PIPELINE *tp_axi);
uint32_t write_led_trigger_length(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t val);
uint32_t read_kicker_trigger_length(AXI_TRIGGER_PIPELINE *tp_axi);
uint32_t write_kicker_trigger_length(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t val);

uint32_t write_pulse_generator_enable(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t value);
uint32_t read_pulse_generator_enable(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t read_threshold_enable_mask(AXI_TRIGGER_PIPELINE *tp_axi) ;
uint32_t write_threshold_enable_mask(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t val) ;
uint32_t read_trigger_mask_enable(AXI_TRIGGER_PIPELINE *tp_axi);
uint32_t write_trigger_mask_enable(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t val);
uint32_t read_self_trigger_enable(AXI_TRIGGER_PIPELINE *tp_axi);
uint32_t write_self_trigger_enable(AXI_TRIGGER_PIPELINE *tp_axi, uint32_t val);

uint32_t read_sync_length(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t do_sync(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t length);
uint32_t write_auto_trig_length(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t length);
uint32_t read_auto_trig_length(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t write_auto_trig_rate(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t period);
uint32_t read_auto_trig_rate(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t write_auto_trig_enable(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t en);
uint32_t read_auto_trig_enable(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t fontus_read_build_info(AXI_TRIGGER_PIPELINE *tp_axi);

/*
 * FONTUS should have a local trigger, but it doesn't yet.
 * So, once the day arrives that FONTUS does have a local trigger, the
 * below can be uncommented
uint32_t read_local_trigger_enable(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t write_local_trigger_enable(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t val);
uint32_t read_local_trigger_mode(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t write_local_trigger_mode(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t val);
uint32_t read_local_trigger_length(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t write_local_trigger_length(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t val);
uint32_t read_local_trigger_count_reset_value(AXI_TRIGGER_PIPELINE* tp_axi);
uint32_t write_local_trigger_count_reset_value(AXI_TRIGGER_PIPELINE* tp_axi, uint32_t val);
*/
#endif

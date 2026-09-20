#pragma once
#include <Arduino.h>
#include "config.h"

void ranging_init();          // load settings from NVS, bring up the DW3000
void ranging_start();         // spawn the radio task on core 1

Role     ranging_role();
uint16_t ranging_antdly();
uint16_t ranging_interval();
bool     ranging_radio_ok();

// Setters persist to NVS. Changes are applied by the radio task itself so the
// web task never touches the SPI bus.
void ranging_set_role(Role r);
void ranging_set_antdly(uint16_t d);
void ranging_set_interval(uint16_t ms);
uint8_t ranging_xtrim();
void    ranging_set_xtrim(uint8_t v);
Phy  ranging_phy();
void ranging_set_phy(Phy p);
uint16_t ranging_tag_addr();
void     ranging_set_tag_addr(uint16_t a);
uint8_t  ranging_tag_id_count();
uint16_t ranging_tag_id_at(uint8_t i);
void     ranging_set_tag_ids(const char *csv);
bool     ranging_tag_moving(uint8_t i);
uint32_t ranging_tag_wakes(uint8_t i);
uint32_t ranging_motion_tick_ms();
uint32_t ranging_idle_tick_ms();
uint32_t ranging_motion_hold_ms();
void     ranging_set_motion_tick_ms(uint32_t v);
void     ranging_set_idle_tick_ms(uint32_t v);
void     ranging_set_motion_hold_ms(uint32_t v);
bool     ranging_trigger_motion(uint16_t addr);
bool     ranging_settle(uint16_t addr);
uint16_t ranging_tag_batt_mv(uint8_t i);
uint16_t ranging_tag_misses(uint8_t i);
float    ranging_tag_uah(uint8_t i);
uint32_t ranging_drain_mult();
void     ranging_set_drain_mult(uint32_t v);
void     ranging_reset_battery();
uint16_t ranging_burst_size();
void     ranging_set_burst_size(uint16_t n);
uint32_t ranging_event_ms();
void     ranging_set_event_ms(uint32_t ms);

// Halt ranging for OTA: flash writes and a live radio loop do not mix.
void ranging_suspend();
void ranging_resume();

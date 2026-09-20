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

// Halt ranging for OTA: flash writes and a live radio loop do not mix.
void ranging_suspend();
void ranging_resume();

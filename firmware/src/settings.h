// Panel settings and off timers, persisted in NVS so they survive reboots.
#pragma once
#include <Arduino.h>

struct Settings {
  uint8_t brightness = 100;  // percent, awake
  uint16_t idleSec = 90;     // until the ambient screen
  bool nightDim = true;      // darker ambient screen between nightStart and nightEnd
  uint8_t nightStart = 22, nightEnd = 6;
  uint32_t offAt[2] = {0, 0};  // unix time per unit, 0 = no timer
};
extern Settings g_set;
void settings_load();
void settings_save();
bool settings_is_night();  // true when the clock is valid and inside the night window

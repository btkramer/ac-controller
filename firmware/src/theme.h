// Palette, mode/fan tables and fonts shared by all UI files.
#pragma once
#include <lvgl.h>
#include "fonts/fonts.h"
#include "assets/assets.h"

#define C_BG     0x080B10
#define C_CARD   0x121821
#define C_CHIP   0x1C232E
#define C_CHIP_P 0x2A3442
#define C_TRACK  0x1F2833
#define C_TEXT   0xF4F7FB
#define C_MUTED  0x8A94A3
#define C_DARK   0x06080B
#define C_OK     0x3DD68C
#define C_WARN   0xF5C451
#define C_BAD    0xFF5A5A

struct ModeDef {
  const char *ha, *label, *verb, *icon;
  uint32_t c1, c2, glow;
  const lv_image_dsc_t *ring;  // dial gradient, nullptr for off
};
constexpr int N_MODES = 6;
extern const ModeDef MODES[N_MODES];

constexpr int N_FANS = 8;
extern const char *const FAN_HA[N_FANS];
extern const char *const FAN_LABEL[N_FANS];
extern const uint16_t FAN_SPIN_MS[N_FANS];

int modeIndex(const char *ha);
int fanIndex(const char *ha);
static inline lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

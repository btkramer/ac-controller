#include "theme.h"
#include <string.h>

const ModeDef MODES[N_MODES] = {
    {"off", "Off", "Off", IC_POWER, 0x7A8492, 0x4A525E, 0, nullptr},
    {"cool", "Cool", "Cooling", IC_COOL, 0x5FE1FF, 0x2F6BFF, 0x2F7BFF, &img_ring_cool},
    {"heat", "Heat", "Heating", IC_HEAT, 0xFFC24A, 0xFF4D2E, 0xFF6A2E, &img_ring_heat},
    {"heat_cool", "Auto", "Auto", IC_AUTO, 0x7AF0B0, 0x14B8A6, 0x22C58A, &img_ring_auto},
    {"fan_only", "Fan", "Fan only", IC_FAN, 0xC9BCFF, 0x7B61FF, 0x7B61FF, &img_ring_fan},
    {"dry", "Dry", "Drying", IC_DRY, 0x8FF3FF, 0x1497C7, 0x14A3C7, &img_ring_dry},
};
const char *const FAN_HA[N_FANS] = {"auto", "quiet", "low", "medlow", "medium", "medhigh", "high", "strong"};
const char *const FAN_LABEL[N_FANS] = {"Auto", "Quiet", "Low", "Med-Low", "Medium", "Med-High", "High", "Strong"};
const uint16_t FAN_SPIN_MS[N_FANS] = {1600, 3000, 2200, 1600, 1200, 900, 650, 450};

int modeIndex(const char *ha) {
  for (int i = 0; i < N_MODES; i++) if (!strcmp(ha, MODES[i].ha)) return i;
  return -1;
}
int fanIndex(const char *ha) {
  for (int i = 0; i < N_FANS; i++) if (!strcmp(ha, FAN_HA[i])) return i;
  return -1;
}

// Home Assistant REST client: background task polls state and sends commands.
#pragma once
#include <Arduino.h>

constexpr int AC_COUNT = 2;
constexpr int HIST_N = 72;  // 6 hours at one sample per 5 minutes
extern const char *const AC_ENTITY[AC_COUNT];
extern const char *const AC_NAME[AC_COUNT];

struct AcState {
  bool valid = false;  // false until first good poll / when HA says unavailable
  char mode[16] = "";
  char fan[16] = "";
  char swing[16] = "";
  char hswing[16] = "";
  float setTemp = NAN;
  float curTemp = NAN;
};

struct HaSnapshot {
  AcState ac[AC_COUNT];
  bool sync = false, syncValid = false;
  bool wifi = false, haOk = false, everOk = false, noCreds = false;
  int rssi = 0;
  bool weatherValid = false;
  float outTemp = NAN;
  char weather[24] = "";
  bool nextValid = false;
  char nextAction[48] = "";
  int histCount = 0;                // valid samples, oldest first
  int16_t hist[AC_COUNT][HIST_N];   // tenths of a degree F, INT16_MIN = no reading
};

enum CmdKind { CMD_TEMP, CMD_MODE, CMD_FAN, CMD_SYNC, CMD_SWING, CMD_HSWING, CMD_TEMP_MODE };

void ha_begin();
void ha_snapshot(HaSnapshot &out);
// unit: 0..AC_COUNT-1 (ignored for CMD_SYNC). sval: mode/fan/swing string, or "on"/"off" for sync.
// CMD_TEMP_MODE sends temperature and hvac_mode in one call.
void ha_send(CmdKind kind, int unit, const char *sval, float fval = 0);

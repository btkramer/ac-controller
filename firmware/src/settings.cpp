#include "settings.h"
#include <Preferences.h>
#include <time.h>

Settings g_set;
static Preferences prefs;

void settings_load() {
  prefs.begin("acp", true);
  g_set.brightness = prefs.getUChar("bright", g_set.brightness);
  g_set.idleSec = prefs.getUShort("idle", g_set.idleSec);
  g_set.nightDim = prefs.getBool("night", g_set.nightDim);
  g_set.nightStart = prefs.getUChar("nStart", g_set.nightStart);
  g_set.nightEnd = prefs.getUChar("nEnd", g_set.nightEnd);
  g_set.offAt[0] = prefs.getULong("off0", 0);
  g_set.offAt[1] = prefs.getULong("off1", 0);
  prefs.end();
}

void settings_save() {
  prefs.begin("acp", false);
  prefs.putUChar("bright", g_set.brightness);
  prefs.putUShort("idle", g_set.idleSec);
  prefs.putBool("night", g_set.nightDim);
  prefs.putUChar("nStart", g_set.nightStart);
  prefs.putUChar("nEnd", g_set.nightEnd);
  prefs.putULong("off0", g_set.offAt[0]);
  prefs.putULong("off1", g_set.offAt[1]);
  prefs.end();
}

bool settings_is_night() {
  time_t now = time(nullptr);
  if (!g_set.nightDim || now < 1700000000) return false;
  struct tm t;
  localtime_r(&now, &t);
  int h = t.tm_hour;
  return g_set.nightStart > g_set.nightEnd ? (h >= g_set.nightStart || h < g_set.nightEnd)
                                           : (h >= g_set.nightStart && h < g_set.nightEnd);
}

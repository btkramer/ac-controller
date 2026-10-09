#include "ha.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"

const char *const AC_ENTITY[AC_COUNT] = {"climate.front_room_ac_2", "climate.family_ac_2"};
const char *const AC_NAME[AC_COUNT] = {"Front Room", "Family Room"};
static const char *SYNC_ENTITY = "input_boolean.ac_sync";
static const char *WEATHER_ENTITY = "weather.forecast_home";
static const char *NEXT_ENTITY = "sensor.ac_next_action";  // optional; published by the HA side
static const char *TZ_POSIX = "MST7";                      // America/Phoenix, no DST

static constexpr uint32_t POLL_MS = 4000;
static constexpr uint32_t WEATHER_MS = 5 * 60000;
static constexpr uint32_t NEXT_MS = 60000;
static constexpr uint32_t HIST_MS = 5 * 60000;
static constexpr uint32_t FAIL_LIMIT = 3;  // consecutive failed polls before marking HA offline

struct Cmd {
  CmdKind kind;
  int unit;
  char sval[16];
  float fval;
};

static HaSnapshot g_snap;
static int16_t g_ring[AC_COUNT][HIST_N];
static int g_ringHead = 0, g_ringCount = 0;
static SemaphoreHandle_t g_mu;
static QueueHandle_t g_q;
static uint32_t g_fails = 0;

static bool credsMissing() { return strcmp(WIFI_SSID, "CHANGE_ME") == 0; }

static int haGet(const String &path, JsonDocument &doc, JsonDocument &filter) {
  HTTPClient http;
  http.setTimeout(4000);
  http.begin(String(HA_BASE_URL) + path);
  http.addHeader("Authorization", "Bearer " HA_TOKEN);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) code = -100;
  } else if (code != 404) {
    Serial.printf("GET %s -> %d\n", path.c_str(), code);
  }
  http.end();
  return code;
}

static bool haPost(const String &path, const String &json) {
  HTTPClient http;
  http.setTimeout(6000);
  http.begin(String(HA_BASE_URL) + path);
  http.addHeader("Authorization", "Bearer " HA_TOKEN);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  http.end();
  Serial.printf("POST %s %s -> %d\n", path.c_str(), json.c_str(), code);
  return code == 200;
}

static void doCmd(const Cmd &c) {
  JsonDocument d;
  String body, path = "/api/services/climate/";
  if (c.kind == CMD_SYNC) {
    d["entity_id"] = SYNC_ENTITY;
    path = String("/api/services/input_boolean/turn_") + c.sval;
  } else {
    d["entity_id"] = AC_ENTITY[c.unit];
    switch (c.kind) {
      case CMD_TEMP: d["temperature"] = (int)lroundf(c.fval); path += "set_temperature"; break;
      case CMD_TEMP_MODE: d["temperature"] = (int)lroundf(c.fval); d["hvac_mode"] = c.sval; path += "set_temperature"; break;
      case CMD_MODE: d["hvac_mode"] = c.sval; path += "set_hvac_mode"; break;
      case CMD_FAN: d["fan_mode"] = c.sval; path += "set_fan_mode"; break;
      case CMD_SWING: d["swing_mode"] = c.sval; path += "set_swing_mode"; break;
      case CMD_HSWING: d["swing_horizontal_mode"] = c.sval; path += "set_swing_horizontal_mode"; break;
      default: return;
    }
  }
  serializeJson(d, body);
  haPost(path, body);
}

static bool pollAc(int i) {
  JsonDocument filter, doc;
  filter["state"] = true;
  JsonObject fa = filter["attributes"].to<JsonObject>();
  fa["temperature"] = true;
  fa["current_temperature"] = true;
  fa["fan_mode"] = true;
  fa["swing_mode"] = true;
  fa["swing_horizontal_mode"] = true;
  if (haGet(String("/api/states/") + AC_ENTITY[i], doc, filter) != 200) return false;
  AcState s;
  const char *st = doc["state"] | "unavailable";
  JsonObject a = doc["attributes"];
  s.valid = strcmp(st, "unavailable") != 0 && strcmp(st, "unknown") != 0;
  strlcpy(s.mode, st, sizeof(s.mode));
  strlcpy(s.fan, a["fan_mode"] | "", sizeof(s.fan));
  strlcpy(s.swing, a["swing_mode"] | "", sizeof(s.swing));
  strlcpy(s.hswing, a["swing_horizontal_mode"] | "", sizeof(s.hswing));
  if (!a["temperature"].isNull()) s.setTemp = a["temperature"].as<float>();
  if (!a["current_temperature"].isNull()) s.curTemp = a["current_temperature"].as<float>();
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_snap.ac[i] = s;
  xSemaphoreGive(g_mu);
  return true;
}

static bool pollSync() {
  JsonDocument filter, doc;
  filter["state"] = true;
  if (haGet(String("/api/states/") + SYNC_ENTITY, doc, filter) != 200) return false;
  const char *st = doc["state"] | "";
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_snap.syncValid = strcmp(st, "on") == 0 || strcmp(st, "off") == 0;
  g_snap.sync = strcmp(st, "on") == 0;
  xSemaphoreGive(g_mu);
  return true;
}

static void pollWeather() {
  JsonDocument filter, doc;
  filter["state"] = true;
  filter["attributes"]["temperature"] = true;
  bool ok = haGet(String("/api/states/") + WEATHER_ENTITY, doc, filter) == 200;
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_snap.weatherValid = ok && !doc["attributes"]["temperature"].isNull();
  if (g_snap.weatherValid) {
    g_snap.outTemp = doc["attributes"]["temperature"].as<float>();
    strlcpy(g_snap.weather, doc["state"] | "", sizeof(g_snap.weather));
  }
  xSemaphoreGive(g_mu);
}

static void pollNext() {
  JsonDocument filter, doc;
  filter["state"] = true;
  bool ok = haGet(String("/api/states/") + NEXT_ENTITY, doc, filter) == 200;
  const char *st = ok ? (doc["state"] | "") : "";
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_snap.nextValid = ok && *st && strcmp(st, "none") && strcmp(st, "unknown") && strcmp(st, "unavailable");
  strlcpy(g_snap.nextAction, g_snap.nextValid ? st : "", sizeof(g_snap.nextAction));
  xSemaphoreGive(g_mu);
}

static void sampleHistory() {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  for (int i = 0; i < AC_COUNT; i++) {
    float t = g_snap.ac[i].curTemp;
    g_ring[i][g_ringHead] = isnan(t) ? INT16_MIN : (int16_t)lroundf(t * 10);
  }
  g_ringHead = (g_ringHead + 1) % HIST_N;
  if (g_ringCount < HIST_N) g_ringCount++;
  xSemaphoreGive(g_mu);
}

static void haTask(void *) {
  uint32_t lastPoll = 0, lastWeather = 0, lastNext = 0, lastHist = 0, lastWifiTry = 0;
  bool firstOk = true, timeSet = false;
  for (;;) {
    bool up = WiFi.status() == WL_CONNECTED;
    xSemaphoreTake(g_mu, portMAX_DELAY);
    g_snap.wifi = up;
    g_snap.noCreds = credsMissing();
    g_snap.rssi = up ? WiFi.RSSI() : 0;
    xSemaphoreGive(g_mu);

    if (!up) {
      if (!credsMissing() && (lastWifiTry == 0 || millis() - lastWifiTry > 10000)) {
        lastWifiTry = millis();
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!timeSet) { configTzTime(TZ_POSIX, "pool.ntp.org", "time.nist.gov"); timeSet = true; }

    Cmd c;
    bool sent = false;
    while (xQueueReceive(g_q, &c, 0) == pdTRUE) { doCmd(c); sent = true; }
    if (sent) lastPoll = millis() - POLL_MS + 1200;  // re-poll shortly after a command

    if (millis() - lastPoll >= POLL_MS) {
      lastPoll = millis();
      bool ok = true;
      for (int i = 0; i < AC_COUNT; i++) ok &= pollAc(i);
      ok &= pollSync();
      g_fails = ok ? 0 : g_fails + 1;
      xSemaphoreTake(g_mu, portMAX_DELAY);
      g_snap.haOk = g_fails < FAIL_LIMIT && (ok || g_snap.everOk);
      if (ok) g_snap.everOk = true;
      xSemaphoreGive(g_mu);
      if (ok && firstOk) { firstOk = false; sampleHistory(); lastHist = millis(); }
    }
    if (lastWeather == 0 || millis() - lastWeather >= WEATHER_MS) { lastWeather = millis(); pollWeather(); }
    if (lastNext == 0 || millis() - lastNext >= NEXT_MS) { lastNext = millis(); pollNext(); }
    if (!firstOk && millis() - lastHist >= HIST_MS) { lastHist = millis(); sampleHistory(); }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void ha_begin() {
  g_mu = xSemaphoreCreateMutex();
  g_q = xQueueCreate(12, sizeof(Cmd));
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("ac-panel");
  xTaskCreatePinnedToCore(haTask, "ha", 12288, nullptr, 1, nullptr, 0);
}

void ha_snapshot(HaSnapshot &out) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  out = g_snap;
  out.histCount = g_ringCount;
  for (int k = 0; k < g_ringCount; k++) {
    int idx = (g_ringHead - g_ringCount + k + HIST_N) % HIST_N;
    for (int i = 0; i < AC_COUNT; i++) out.hist[i][k] = g_ring[i][idx];
  }
  xSemaphoreGive(g_mu);
}

void ha_send(CmdKind kind, int unit, const char *sval, float fval) {
  Cmd c{kind, unit, "", fval};
  if (sval) strlcpy(c.sval, sval, sizeof(c.sval));
  xQueueSend(g_q, &c, 0);
}

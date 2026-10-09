// OTA: HTTP POST /update (fast, see ../../OTA_HTTP.md) + ArduinoOTA espota as fallback.
// Both need the OTA_USER/OTA_PASS from secrets.h. Runs on its own task, starts once WiFi is up.
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "ota.h"
#include "secrets.h"

static WebServer server(80);
static volatile bool g_rebootAt = false;
static uint32_t g_rebootMs = 0;
static bool g_uploadOk = false;
static bool g_uploadAuthed = false;

static void onFinish() {
  if (!server.authenticate(OTA_USER, OTA_PASS)) return server.requestAuthentication();
  server.send(g_uploadOk ? 200 : 500, "text/plain", g_uploadOk ? "OK, rebooting\n" : "update failed\n");
  if (g_uploadOk) { g_rebootAt = true; g_rebootMs = millis() + 500; }
}

static void onUpload() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    g_uploadAuthed = server.authenticate(OTA_USER, OTA_PASS);
    g_uploadOk = false;
    if (g_uploadAuthed) {
      Serial.printf("OTA start: %s\n", u.filename.c_str());
      Update.begin(UPDATE_SIZE_UNKNOWN);
    }
  } else if (!g_uploadAuthed) {
    return;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    Update.write(u.buf, u.currentSize);
    delay(1);  // yield: sustained flash writes can starve the idle task / watchdog
  } else if (u.status == UPLOAD_FILE_END) {
    g_uploadOk = Update.end(true);
    Serial.printf("OTA end: %s (%u bytes)\n", g_uploadOk ? "ok" : Update.errorString(), u.totalSize);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

static void otaTask(void *) {
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));
  ArduinoOTA.setHostname("ac-panel");
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();
  server.on("/update", HTTP_POST, onFinish, onUpload);
  server.on("/", HTTP_GET, []() { server.send(200, "text/plain", "ac-panel\n"); });
  server.begin();
  Serial.printf("OTA ready: http://%s/update\n", WiFi.localIP().toString().c_str());
  for (;;) {
    ArduinoOTA.handle();
    server.handleClient();
    if (g_rebootAt && (int32_t)(millis() - g_rebootMs) >= 0) ESP.restart();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void ota_begin() { xTaskCreatePinnedToCore(otaTask, "ota", 8192, nullptr, 1, nullptr, 0); }

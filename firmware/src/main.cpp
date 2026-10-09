// ac-controller: Sunton ESP32-4848S040 -> LVGL UI -> Home Assistant climate entities.
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "ui_internal.h"
#include "ota.h"

#define FW_VERSION "0.3.0"
#define GFX_BL 38
#define BL_CH 0
#define TOUCH_ADDR 0x5D

static Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18, 17, 16, 21,
    11, 12, 13, 14, 0,
    8, 20, 3, 46, 9, 10,
    4, 5, 6, 7, 15,
    1, 10, 8, 50, 1, 10, 8, 20,
    0, 12000000, false, 0, 0, 0);
static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0, true, bus, GFX_NOT_DEFINED,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

static volatile bool g_wakeReq = false;
static bool g_swallow = false;  // touch that woke the screen; ignored until the finger lifts
static uint8_t g_level = 0;

const char *fw_version() { return FW_VERSION; }

static bool gt911Read(uint16_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)px, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1);
  lv_display_flush_ready(d);
}

static void touchCb(lv_indev_t *, lv_indev_data_t *data) {
  static int16_t lx = 0, ly = 0;
  uint8_t st = 0, p[4];
  bool pressed = false;
  if (gt911Read(0x814E, &st, 1)) {
    if ((st & 0x80) && (st & 0x0F) && gt911Read(0x8150, p, 4)) {
      lx = p[0] | (p[1] << 8);
      ly = p[2] | (p[3] << 8);
    }
    if (st & 0x80) {  // buffer status must be cleared after every read
      Wire.beginTransmission(TOUCH_ADDR); Wire.write(0x81); Wire.write(0x4E); Wire.write((uint8_t)0); Wire.endTransmission();
    }
    pressed = (st & 0x80) && (st & 0x0F);
  }
  // A touch on the dim/clock screen only wakes the panel; it never reaches a control.
  if (ui_ambient_active() || g_swallow) {
    if (pressed) { g_wakeReq = true; g_swallow = true; }
    else if (!ui_ambient_active()) g_swallow = false;
    data->state = LV_INDEV_STATE_RELEASED;
  } else {
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  }
  data->point.x = lx;
  data->point.y = ly;
}

static void setLevel(uint8_t l) {
  if (l == g_level) return;
  g_level = l;
  ledcWrite(BL_CH, l);
}

void backlight_apply() {
  if (ui_ambient_active()) setLevel(settings_is_night() ? 4 : 30);
  else setLevel(max(10, g_set.brightness * 255 / 100));
}

void setup() {
  Serial.begin(115200);
  settings_load();
  if (!gfx->begin()) Serial.println("gfx->begin() failed");
  gfx->fillScreen(BLACK);
  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(GFX_BL, BL_CH);
  Wire.begin(19, 45, 400000);

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  const size_t bufBytes = 480 * 48 * 2;
  void *b1 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void *b2 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_display_t *disp = lv_display_create(480, 480);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, b1, b2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_indev_t *ind = lv_indev_create();
  lv_indev_set_type(ind, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(ind, touchCb);

  ha_begin();
  ui_build();
  ota_begin();
  backlight_apply();
}

void loop() {
  lv_timer_handler();
  if (g_wakeReq) {
    g_wakeReq = false;
    ui_exit_ambient();
    lv_display_trigger_activity(nullptr);
  } else if (!ui_ambient_active() && lv_display_get_inactive_time(nullptr) > g_set.idleSec * 1000UL) {
    ui_enter_ambient();
  }
  backlight_apply();
  delay(5);
}

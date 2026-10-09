// Settings and info screen, opened by long-pressing the overview title or the offline banner's Info button.
#include "ui_internal.h"
#include <WiFi.h>

static lv_obj_t *scr, *bright, *idleChips[4], *nightSw, *info;
static const uint16_t IDLE_SEC[] = {30, 60, 120, 300};
static const char *const IDLE_LABEL[] = {"30 sec", "1 min", "2 min", "5 min"};

lv_obj_t *set_screen() { return scr; }

static void paint_idle() {
  for (int k = 0; k < 4; k++) style_chip(idleChips[k], g_set.idleSec == IDLE_SEC[k], C_TEXT, 0xC7D0DB);
}
static void on_back(lv_event_t *) { lv_screen_load(ui_main_screen()); }
static void on_bright(lv_event_t *) { g_set.brightness = lv_slider_get_value(bright); backlight_apply(); }
static void on_bright_done(lv_event_t *) { settings_save(); }
static void on_idle(lv_event_t *e) {
  g_set.idleSec = IDLE_SEC[(int)(intptr_t)lv_event_get_user_data(e)];
  settings_save();
  paint_idle();
}
static void on_night(lv_event_t *) {
  g_set.nightDim = lv_obj_has_state(nightSw, LV_STATE_CHECKED);
  settings_save();
}
static void on_restart(lv_event_t *) { ESP.restart(); }

static lv_obj_t *row_label(const char *txt, int y) {
  lv_obj_t *l = mk_label(scr, &font_ui16, C_TEXT, txt);
  lv_obj_set_pos(l, 22, y);
  return l;
}

void set_build() {
  scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, hex(C_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *b = mk_btn(scr, 44, 44, 22, IC_BACK, &font_ui18, on_back, nullptr);
  lv_obj_set_pos(b, 12, 8);
  lv_obj_t *t = mk_label(scr, &font_o34, C_TEXT, "Settings");
  lv_obj_set_pos(t, 70, 6);

  row_label(IC_SUN "  Brightness", 70);
  bright = lv_slider_create(scr);
  lv_slider_set_range(bright, 10, 100);
  lv_slider_set_value(bright, g_set.brightness, LV_ANIM_OFF);
  lv_obj_set_size(bright, 420, 14);
  lv_obj_set_pos(bright, 30, 104);
  lv_obj_set_style_bg_color(bright, hex(C_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bright, hex(C_TEXT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bright, hex(C_TEXT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(bright, 7, LV_PART_KNOB);
  lv_obj_add_event_cb(bright, on_bright, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(bright, on_bright_done, LV_EVENT_RELEASED, nullptr);

  row_label(IC_MOON "  Show the clock screen after", 138);
  for (int k = 0; k < 4; k++) {
    idleChips[k] = mk_btn(scr, 100, 40, 14, IDLE_LABEL[k], &font_ui14, on_idle, (void *)(intptr_t)k);
    lv_obj_set_pos(idleChips[k], 22 + k * 110, 164);
  }
  paint_idle();

  char buf[48];
  snprintf(buf, sizeof(buf), "Darker at night, %d PM to %d AM", g_set.nightStart - 12, g_set.nightEnd);
  row_label(IC_MOON "  Night dimming", 222);
  lv_obj_t *nl = mk_label(scr, &font_ui14, C_MUTED, buf);
  lv_obj_set_pos(nl, 50, 246);
  nightSw = lv_switch_create(scr);
  lv_obj_set_size(nightSw, 62, 34);
  lv_obj_set_pos(nightSw, 396, 224);
  lv_obj_set_style_bg_color(nightSw, hex(C_CHIP_P), LV_PART_MAIN);
  lv_obj_set_style_bg_color(nightSw, hex(C_OK), LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (g_set.nightDim) lv_obj_add_state(nightSw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(nightSw, on_night, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *box = mk_panel(scr, 436, 132, C_CARD, 18);
  lv_obj_set_pos(box, 22, 280);
  info = mk_label(box, &font_ui14, 0xC6CED9, "");
  lv_obj_set_style_text_line_space(info, 6, 0);
  lv_obj_set_pos(info, 16, 12);

  lv_obj_t *r = mk_btn(scr, 180, 44, 14, IC_POWER "  Restart panel", &font_ui16, on_restart, nullptr);
  lv_obj_set_pos(r, 22, 424);
}

void set_update(const HaSnapshot &s) {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  uint32_t up = millis() / 60000;
  char buf[320];
  snprintf(buf, sizeof(buf),
           "Firmware %s\n"
           "Address %s  \xC2\xB7  WiFi %s\n"
           "Home Assistant: %s\n"
           "Running for %luh %lum  \xC2\xB7  Memory free %u KB, %u KB extra",
           fw_version(), s.wifi ? WiFi.localIP().toString().c_str() : "-",
           s.wifi ? (String(s.rssi) + " dBm").c_str() : "disconnected",
           s.haOk ? "connected" : (s.everOk ? "not responding" : "not connected yet"),
           up / 60, up % 60, ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
  set_text(info, buf);
}

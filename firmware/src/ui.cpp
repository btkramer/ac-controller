// Main screen: tileview of [Overview | Front Room | Family Room], or [Overview | Both Rooms] while
// the units are synced. Plus the airflow / timer sheets and the offline overlay.
#include "ui_internal.h"
#include <time.h>

static constexpr int T_MIN = 61, T_MAX = 88;
static constexpr uint32_t HOLD_MS = 8000;      // ignore polled values for a field after a local change
static constexpr uint32_t SEND_SINGLE = 700;   // dial / +- debounce, one room
static constexpr uint32_t SEND_BOTH = 1500;    // longer while synced so the HA flow sees one change
static constexpr uint32_t CONFIRM_MS = 12000;  // give up waiting for HA to reflect a change
static constexpr int BOTH = 2;
static constexpr int CX = 240, CY = 194;       // dial center on a room page

struct Preset { const char *icon, *name; int temp; };  // temp 0 = all off
static const Preset PRESETS[] = {{IC_BED, "Sleep", 76}, {IC_COUCH, "Comfort", 72}, {IC_POWER, "All off", 0}};

static const char *const SWING[] = {"off", "Full", "Upper", "Lower"};
static const char *const SWING_LABEL[] = {"Off", "Full", "Upper", "Lower"};
static const char *const HSWING[] = {"off", "Full", "Left", "Center", "Right"};
static const char *const HSWING_LABEL[] = {"Off", "Full", "Left", "Center", "Right"};
static const int TIMER_MIN[] = {30, 60, 120, 240, 480};
static const char *const TIMER_LABEL[] = {"30 min", "1 hour", "2 hours", "4 hours", "8 hours"};

struct View {  // one room page (unit 0/1) or the Both Rooms page
  int unit;
  bool built = false;
  lv_obj_t *glow, *arc, *pulse, *modeLbl, *roll, *rollLbl[2], *now, *saved, *chips[N_MODES];
  lv_obj_t *autoChip, *fanIco, *slider, *fanLbl, *timerBtn;
  lv_point_precise_t tick[5][2];
  int cur = 0, target = 72, shownTarget = -1, modeIdx = -1, fanIdx = -1, spinMs = -1;
  uint32_t holdTemp = 0, holdMode = 0, holdFan = 0, sendAt = 0;
  int pendField = 0, pendVal = 0;  // 1 temp, 2 mode, 3 fan
  uint32_t pendUntil = 0, savedUntil = 0;
  char lastOn[16] = "cool";
  explicit View(int u) : unit(u) {}
};
struct OvCard {
  lv_obj_t *card, *name, *pill, *big, *info1, *fanIco, *info2, *info3, *pwr;
  int spinMs = -1;
};

static HaSnapshot S;
static View V[3] = {View(0), View(1), View(BOTH)};
static OvCard OV[AC_COUNT];
static lv_obj_t *scrMain, *tv, *dotsRow, *tiles[3];
static int nTiles = 0;
static bool builtSynced = false, ambient = false;
static int rebuildTo = -1;  // pending page rebuild after a sync toggle (-1 none)
static uint32_t holdSync = 0;
static lv_obj_t *statusDot, *statusLbl, *nextLbl, *syncSw;
static lv_obj_t *sheetDim, *sheet;
static lv_obj_t *offBlock, *offBanner, *offLbl;
static lv_obj_t *prevScreen;

const HaSnapshot &ui_snap() { return S; }
lv_obj_t *ui_main_screen() { return scrMain; }
bool ui_ambient_active() { return ambient; }
static bool held(uint32_t until) { return (int32_t)(until - millis()) > 0; }
bool clock_valid() { return time(nullptr) > 1700000000; }
static int wunit(const View &v) { return v.unit == BOTH ? 0 : v.unit; }

// ---------------------------------------------------------------- helpers
lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, uint32_t color, const char *txt) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  lv_label_set_text(l, txt);
  return l;
}

lv_obj_t *mk_panel(lv_obj_t *p, int w, int h, uint32_t bg, int radius) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, hex(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t *mk_btn(lv_obj_t *p, int w, int h, int radius, const char *txt, const lv_font_t *f, lv_event_cb_t cb, void *ud) {
  lv_obj_t *b = mk_panel(p, w, h, C_CHIP, radius);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(b, hex(C_CHIP_P), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b, hex(C_TEXT), 0);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, f, 0);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

void style_chip(lv_obj_t *chip, bool active, uint32_t c1, uint32_t c2) {
  lv_obj_set_style_bg_color(chip, hex(active ? c1 : C_CHIP), 0);
  lv_obj_set_style_bg_grad_color(chip, hex(c2), 0);
  lv_obj_set_style_bg_grad_dir(chip, active ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_text_color(chip, hex(active ? C_DARK : C_TEXT), 0);
}

void set_text(lv_obj_t *l, const char *txt) {
  if (strcmp(lv_label_get_text(l), txt)) lv_label_set_text(l, txt);
}

static void spin_cb(void *o, int32_t v) { lv_obj_set_style_transform_rotation((lv_obj_t *)o, v, 0); }
static void spin(lv_obj_t *o, int ms, int &cur) {  // ms 0 = stopped
  if (ms == cur) return;
  cur = ms;
  lv_anim_delete(o, spin_cb);
  lv_obj_set_style_transform_rotation(o, 0, 0);
  if (!ms) return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, spin_cb);
  lv_anim_set_values(&a, 0, 3600);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

static lv_obj_t *mk_icon(lv_obj_t *p, const lv_font_t *f, int size, const char *icon) {
  lv_obj_t *l = mk_label(p, f, C_TEXT, icon);
  lv_obj_set_size(l, size, size);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_transform_pivot_x(l, size / 2, 0);
  lv_obj_set_style_transform_pivot_y(l, size / 2, 0);
  return l;
}

static void fmt_remaining(char *buf, size_t n, uint32_t offAt, bool words) {
  long left = (long)offAt - (long)time(nullptr);
  if (left < 0) left = 0;
  long m = (left + 59) / 60;
  if (words) snprintf(buf, n, m >= 60 ? "Off in %ldh %ldm" : "Off in %ldm", m >= 60 ? m / 60 : m, m % 60);
  else snprintf(buf, n, "%ld:%02ld", m / 60, m % 60);
}

// ---------------------------------------------------------------- actions
static void pulse_cb(void *o, int32_t x) {
  lv_obj_t *p = (lv_obj_t *)o;
  int s = 200 + x * 70 / 1000;
  lv_obj_set_size(p, s, s);
  lv_obj_set_pos(p, CX - s / 2, CY - s / 2);
  lv_obj_set_style_border_opa(p, (lv_opa_t)(220 - x * 220 / 1000), 0);
}
static void stop_pulse(View &v) {
  if (!v.built) return;
  lv_anim_delete(v.pulse, pulse_cb);
  lv_obj_set_style_border_opa(v.pulse, LV_OPA_TRANSP, 0);
}
static void set_pending(View &v, int field, int val) {
  v.pendField = field;
  v.pendVal = val;
  v.pendUntil = millis() + CONFIRM_MS;
  if (!v.built) return;
  lv_obj_add_flag(v.saved, LV_OBJ_FLAG_HIDDEN);
  lv_anim_delete(v.pulse, pulse_cb);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, v.pulse);
  lv_anim_set_exec_cb(&a, pulse_cb);
  lv_anim_set_values(&a, 0, 1000);
  lv_anim_set_duration(&a, 1100);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

static void y_cb(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, v); }
static void show_target(View &v, int t, bool animate) {
  if (!v.built || t == v.shownTarget) return;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", t);
  lv_arc_set_value(v.arc, t);
  lv_obj_t *old = v.rollLbl[v.cur], *nu = v.rollLbl[1 - v.cur];
  lv_anim_delete(old, y_cb);
  lv_anim_delete(nu, y_cb);
  if (!animate || v.shownTarget < 0) {
    lv_label_set_text(old, buf);
    lv_obj_set_y(old, 0);
    lv_obj_set_y(nu, 130);
  } else {
    int dir = t > v.shownTarget ? 1 : -1;
    lv_label_set_text(nu, buf);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, y_cb);
    lv_anim_set_duration(&a, 280);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_var(&a, old);
    lv_anim_set_values(&a, 0, -dir * 130);
    lv_anim_start(&a);
    lv_anim_set_var(&a, nu);
    lv_anim_set_values(&a, dir * 130, 0);
    lv_anim_start(&a);
    v.cur = 1 - v.cur;
  }
  v.shownTarget = t;
}

static void change_target(View &v, int t, bool animate) {
  v.target = constrain(t, T_MIN, T_MAX);
  v.holdTemp = millis() + HOLD_MS;
  v.sendAt = millis() + (v.unit == BOTH ? SEND_BOTH : SEND_SINGLE);
  show_target(v, v.target, animate);
}

static void paint_mode(View &v, int mi);
static void set_mode(View &v, int mi) {
  int u = wunit(v);
  bool wasOff = modeIndex(S.ac[u].mode) == 0;
  v.modeIdx = mi;
  v.holdMode = millis() + HOLD_MS;
  if (mi > 0 && wasOff) ha_send(CMD_TEMP_MODE, u, MODES[mi].ha, v.target);  // setpoint isn't mirrored while off
  else ha_send(CMD_MODE, u, MODES[mi].ha);
  if (mi > 0) strlcpy(v.lastOn, MODES[mi].ha, sizeof(v.lastOn));
  set_pending(v, 2, mi);
  if (v.built) paint_mode(v, mi);
}

static void set_fan(View &v, int fi) {
  v.fanIdx = fi;
  v.holdFan = millis() + HOLD_MS;
  ha_send(CMD_FAN, wunit(v), FAN_HA[fi]);
  set_pending(v, 3, fi);
}

static View &view_for_card(int i) { return builtSynced ? V[BOTH] : V[i]; }

static void toggle_power(View &v) {
  int u = wunit(v);
  int mi = held(v.holdMode) ? v.modeIdx : modeIndex(S.ac[u].mode);
  set_mode(v, mi > 0 ? 0 : max(1, modeIndex(v.lastOn)));
}

static void apply_preset(int p) {
  int n = builtSynced ? 1 : AC_COUNT;
  for (int k = 0; k < n; k++) {
    View &v = builtSynced ? V[BOTH] : V[k];
    if (!PRESETS[p].temp) { set_mode(v, 0); continue; }
    v.target = PRESETS[p].temp;
    v.holdTemp = v.holdMode = millis() + HOLD_MS;
    v.sendAt = 0;
    v.modeIdx = 1;
    strlcpy(v.lastOn, "cool", sizeof(v.lastOn));
    ha_send(CMD_TEMP_MODE, wunit(v), "cool", v.target);
    set_pending(v, 1, v.target);
    if (v.built) { paint_mode(v, 1); show_target(v, v.target, true); }
  }
}

// ---------------------------------------------------------------- events
static void on_arc(lv_event_t *e) {
  View &v = *(View *)lv_event_get_user_data(e);
  change_target(v, lv_arc_get_value(v.arc), false);
}
static void on_minus(lv_event_t *e) { View &v = *(View *)lv_event_get_user_data(e); change_target(v, v.target - 1, true); }
static void on_plus(lv_event_t *e) { View &v = *(View *)lv_event_get_user_data(e); change_target(v, v.target + 1, true); }
static void on_chip(lv_event_t *e) {
  View &v = *(View *)lv_event_get_user_data(e);
  set_mode(v, (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
}
static void paint_fan(View &v, int fi);
static void on_fan_auto(lv_event_t *e) {
  View &v = *(View *)lv_event_get_user_data(e);
  set_fan(v, 0);
  paint_fan(v, 0);
}
static void on_fan_slide(lv_event_t *e) {
  View &v = *(View *)lv_event_get_user_data(e);
  set_text(v.fanLbl, FAN_LABEL[lv_slider_get_value(v.slider)]);
}
static void on_fan_release(lv_event_t *e) {
  View &v = *(View *)lv_event_get_user_data(e);
  int fi = lv_slider_get_value(v.slider);
  set_fan(v, fi);
  paint_fan(v, fi);
}
static void on_card(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  lv_tileview_set_tile_by_index(tv, builtSynced ? 1 : i + 1, 0, LV_ANIM_ON);
}
static void on_power(lv_event_t *e) {
  lv_event_stop_bubbling(e);
  toggle_power(view_for_card((int)(intptr_t)lv_event_get_user_data(e)));
}
static void on_preset(lv_event_t *e) { apply_preset((int)(intptr_t)lv_event_get_user_data(e)); }
static void on_sync(lv_event_t *) {
  bool on = lv_obj_has_state(syncSw, LV_STATE_CHECKED);
  holdSync = millis() + HOLD_MS;
  ha_send(CMD_SYNC, 0, on ? "on" : "off");
  rebuildTo = on;  // rebuilt from the timer, not inside this switch's own event
}
static void on_title_long(lv_event_t *) { ui_open_settings(); }
static void update_dots();
static void on_tile(lv_event_t *) { update_dots(); }

// ---------------------------------------------------------------- sheets
static void close_sheet() {
  if (!sheet) return;
  lv_obj_delete(sheet);
  lv_obj_delete(sheetDim);
  sheet = sheetDim = nullptr;
}
static void on_sheet_close(lv_event_t *) { close_sheet(); }

static lv_obj_t *open_sheet(const char *title, int h) {
  close_sheet();
  sheetDim = mk_panel(lv_layer_top(), 480, 480, 0x000000, 0);
  lv_obj_set_style_bg_opa(sheetDim, LV_OPA_50, 0);
  lv_obj_add_flag(sheetDim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(sheetDim, on_sheet_close, LV_EVENT_CLICKED, nullptr);
  sheet = mk_panel(lv_layer_top(), 480, h + 30, C_CARD, 26);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);  // swallow taps so they don't reach the dimmer
  lv_obj_t *t = mk_label(sheet, &font_ui20, C_TEXT, title);
  lv_obj_set_pos(t, 22, 20);
  lv_obj_t *x = mk_btn(sheet, 44, 44, 22, IC_CLOSE, &font_ui18, on_sheet_close, nullptr);
  lv_obj_set_pos(x, 418, 12);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, sheet);
  lv_anim_set_exec_cb(&a, y_cb);
  lv_anim_set_values(&a, 480, 480 - h);
  lv_anim_set_duration(&a, 220);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
  return sheet;
}

static View *sheetView;
static lv_obj_t *swingChips[4], *hswingChips[5];
static void paint_swing(const char *sw, const char *hsw) {
  for (int k = 0; k < 4; k++) style_chip(swingChips[k], !strcmp(sw, SWING[k]), C_TEXT, 0xC7D0DB);
  for (int k = 0; k < 5; k++) style_chip(hswingChips[k], !strcmp(hsw, HSWING[k]), C_TEXT, 0xC7D0DB);
}
static void on_swing(lv_event_t *e) {
  int k = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
  bool h = (bool)lv_event_get_user_data(e);
  // the sync flow doesn't mirror swing, so the Both page writes both units itself
  for (int u = 0; u < AC_COUNT; u++)
    if (sheetView->unit == BOTH || sheetView->unit == u) ha_send(h ? CMD_HSWING : CMD_SWING, u, h ? HSWING[k] : SWING[k]);
  const AcState &a = S.ac[wunit(*sheetView)];
  paint_swing(h ? a.swing : SWING[k], h ? HSWING[k] : a.hswing);
}
static void open_airflow(View &v) {
  sheetView = &v;
  lv_obj_t *s = open_sheet("Airflow", 250);
  lv_obj_t *l = mk_label(s, &font_ui14, C_MUTED, IC_VSWING "  Up and down");
  lv_obj_set_pos(l, 22, 66);
  for (int k = 0; k < 4; k++) {
    swingChips[k] = mk_btn(s, 102, 44, 14, SWING_LABEL[k], &font_ui16, on_swing, (void *)false);
    lv_obj_set_user_data(swingChips[k], (void *)(intptr_t)k);
    lv_obj_set_pos(swingChips[k], 22 + k * 110, 90);
  }
  l = mk_label(s, &font_ui14, C_MUTED, IC_HSWING "  Left and right");
  lv_obj_set_pos(l, 22, 150);
  for (int k = 0; k < 5; k++) {
    hswingChips[k] = mk_btn(s, 80, 44, 14, HSWING_LABEL[k], &font_ui16, on_swing, (void *)true);
    lv_obj_set_user_data(hswingChips[k], (void *)(intptr_t)k);
    lv_obj_set_pos(hswingChips[k], 22 + k * 88, 174);
  }
  const AcState &a = S.ac[wunit(v)];
  paint_swing(a.swing, a.hswing);
}

static void units_of(const View &v, bool out[AC_COUNT]) {
  for (int u = 0; u < AC_COUNT; u++) out[u] = v.unit == BOTH || v.unit == u;
}
static void on_timer_pick(lv_event_t *e) {
  int k = (int)(intptr_t)lv_event_get_user_data(e);
  bool us[AC_COUNT];
  units_of(*sheetView, us);
  uint32_t at = k < 0 ? 0 : (uint32_t)time(nullptr) + TIMER_MIN[k] * 60;
  for (int u = 0; u < AC_COUNT; u++) if (us[u]) g_set.offAt[u] = at;
  settings_save();
  close_sheet();
}
static void open_timer(View &v) {
  sheetView = &v;
  bool active = g_set.offAt[wunit(v)] != 0;
  lv_obj_t *s = open_sheet("Turn off after", active ? 230 : 170);
  if (!clock_valid()) {
    lv_obj_t *l = mk_label(s, &font_ui16, C_WARN, IC_WARN "  The clock isn't set yet. Try again in a minute.");
    lv_obj_set_pos(l, 22, 80);
    return;
  }
  for (int k = 0; k < 5; k++) {
    lv_obj_t *b = mk_btn(s, 80, 48, 14, TIMER_LABEL[k], &font_ui14, on_timer_pick, (void *)(intptr_t)k);
    lv_obj_set_pos(b, 22 + k * 88, 76);
  }
  if (active) {
    char buf[32];
    fmt_remaining(buf, sizeof(buf), g_set.offAt[wunit(v)], true);
    lv_obj_t *l = mk_label(s, &font_ui16, C_WARN, buf);
    lv_obj_set_pos(l, 22, 152);
    lv_obj_t *b = mk_btn(s, 160, 44, 14, "Cancel timer", &font_ui16, on_timer_pick, (void *)(intptr_t)-1);
    lv_obj_set_pos(b, 298, 142);
  }
}
static void on_timer_btn(lv_event_t *e) { open_timer(*(View *)lv_event_get_user_data(e)); }
static void on_air_btn(lv_event_t *e) { open_airflow(*(View *)lv_event_get_user_data(e)); }

// ---------------------------------------------------------------- page building
static void build_card(lv_obj_t *t, int i, int y) {
  OvCard &c = OV[i];
  c.spinMs = -1;
  c.card = mk_panel(t, 456, 132, C_CARD, 24);
  lv_obj_set_pos(c.card, 12, y);
  lv_obj_set_style_border_width(c.card, 2, 0);
  lv_obj_set_style_border_opa(c.card, LV_OPA_60, 0);
  lv_obj_set_style_bg_grad_dir(c.card, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_grad_color(c.card, hex(C_CARD), 0);
  lv_obj_set_style_bg_main_stop(c.card, 0, 0);
  lv_obj_set_style_bg_grad_stop(c.card, 190, 0);
  lv_obj_add_flag(c.card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c.card, on_card, LV_EVENT_CLICKED, (void *)(intptr_t)i);

  c.name = mk_label(c.card, &font_ui20, C_TEXT, AC_NAME[i]);
  lv_obj_set_pos(c.name, 18, 12);
  c.pill = mk_label(c.card, &font_ui14, C_MUTED, "");
  lv_obj_set_style_bg_opa(c.pill, LV_OPA_30, 0);
  lv_obj_set_style_radius(c.pill, 12, 0);
  lv_obj_set_style_pad_hor(c.pill, 10, 0);
  lv_obj_set_style_pad_ver(c.pill, 4, 0);
  c.big = mk_label(c.card, &font_n76, C_TEXT, "--");
  lv_obj_set_pos(c.big, 14, 38);
  c.info1 = mk_label(c.card, &font_ui16, C_TEXT, "");
  lv_obj_set_pos(c.info1, 172, 42);
  c.fanIco = mk_icon(c.card, &font_ui14, 16, IC_FAN);
  lv_obj_set_pos(c.fanIco, 172, 70);
  c.info2 = mk_label(c.card, &font_ui14, C_MUTED, "");
  lv_obj_set_pos(c.info2, 196, 70);
  c.info3 = mk_label(c.card, &font_ui14, C_WARN, "");
  lv_obj_set_pos(c.info3, 172, 94);
  c.pwr = mk_btn(c.card, 68, 68, LV_RADIUS_CIRCLE, IC_POWER, &font_ui22, on_power, (void *)(intptr_t)i);
  lv_obj_set_pos(c.pwr, 368, 30);
  lv_obj_set_style_shadow_spread(c.pwr, 0, 0);
}

static void build_overview(lv_obj_t *t, bool synced) {
  lv_obj_t *title = mk_label(t, &font_o34, C_TEXT, "Climate");
  lv_obj_set_pos(title, 20, 4);
  lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(title, on_title_long, LV_EVENT_LONG_PRESSED, nullptr);
  statusDot = mk_panel(t, 9, 9, C_WARN, LV_RADIUS_CIRCLE);
  lv_obj_align(statusDot, LV_ALIGN_TOP_RIGHT, -20, 19);
  statusLbl = mk_label(t, &font_ui14, C_MUTED, "Starting");
  nextLbl = mk_label(t, &font_ui14, C_MUTED, "");
  lv_obj_add_flag(nextLbl, LV_OBJ_FLAG_HIDDEN);

  build_card(t, 0, 58);
  build_card(t, 1, 198);
  if (synced) {
    lv_obj_t *b = mk_label(t, &font_ui14, C_DARK, IC_LINK "  Linked");
    lv_obj_set_style_bg_color(b, hex(C_OK), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 13, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_pad_ver(b, 5, 0);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 181);
  }
  for (int p = 0; p < 3; p++) {
    char buf[40];
    if (PRESETS[p].temp) snprintf(buf, sizeof(buf), "%s  %s %d\xC2\xB0", PRESETS[p].icon, PRESETS[p].name, PRESETS[p].temp);
    else snprintf(buf, sizeof(buf), "%s  %s", PRESETS[p].icon, PRESETS[p].name);
    lv_obj_t *b = mk_btn(t, 146, 46, 16, buf, &font_ui16, on_preset, (void *)(intptr_t)p);
    lv_obj_set_pos(b, 12 + p * 155, 340);
  }
  lv_obj_t *bar = mk_panel(t, 456, 50, C_CARD, 18);
  lv_obj_set_pos(bar, 12, 396);
  lv_obj_t *l = mk_label(bar, &font_ui18, C_TEXT, IC_LINK "  Keep both rooms in sync");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 18, 0);
  syncSw = lv_switch_create(bar);
  lv_obj_set_size(syncSw, 62, 34);
  lv_obj_align(syncSw, LV_ALIGN_RIGHT_MID, -14, 0);
  lv_obj_set_style_bg_color(syncSw, hex(C_CHIP_P), LV_PART_MAIN);
  lv_obj_set_style_bg_color(syncSw, hex(C_OK), LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (synced) lv_obj_add_state(syncSw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(syncSw, on_sync, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void build_detail(View &v, lv_obj_t *p) {
  v.built = true;
  v.cur = 0;
  v.shownTarget = v.modeIdx = v.fanIdx = v.spinMs = -1;
  v.pendField = 0;

  v.glow = lv_image_create(p);
  lv_image_set_src(v.glow, &img_glow);
  lv_obj_set_pos(v.glow, CX - 200, CY - 200);
  lv_obj_set_style_image_recolor_opa(v.glow, LV_OPA_COVER, 0);

  lv_obj_t *title = mk_label(p, &font_o26, C_TEXT, v.unit == BOTH ? "Both Rooms" : AC_NAME[v.unit]);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
  v.timerBtn = mk_btn(p, 44, 44, 22, IC_TIMER, &font_ui16, on_timer_btn, &v);
  lv_obj_set_pos(v.timerBtn, 12, 8);
  lv_obj_t *air = mk_btn(p, 44, 44, 22, IC_AIR, &font_ui18, on_air_btn, &v);
  lv_obj_set_pos(air, 424, 8);

  v.arc = lv_arc_create(p);
  lv_obj_set_size(v.arc, 300, 300);
  lv_obj_set_pos(v.arc, CX - 150, CY - 150);
  lv_arc_set_rotation(v.arc, 135);
  lv_arc_set_bg_angles(v.arc, 0, 270);
  lv_arc_set_range(v.arc, T_MIN, T_MAX);
  lv_arc_set_value(v.arc, v.target);
  lv_obj_set_style_arc_width(v.arc, 20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(v.arc, 20, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(v.arc, hex(C_TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(v.arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(v.arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(v.arc, hex(C_TEXT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(v.arc, 7, LV_PART_KNOB);
  lv_obj_set_style_pad_all(v.arc, 0, LV_PART_MAIN);
  lv_obj_add_flag(v.arc, LV_OBJ_FLAG_ADV_HITTEST);
  lv_obj_remove_flag(v.arc, LV_OBJ_FLAG_SCROLL_CHAIN);  // dragging the dial must not swipe pages
  lv_obj_add_event_cb(v.arc, on_arc, LV_EVENT_VALUE_CHANGED, &v);

  for (int k = 0; k < 5; k++) {  // ticks every 5 degrees F
    float a = (135 + 270.0f * (65 + 5 * k - T_MIN) / (T_MAX - T_MIN)) * PI / 180;
    v.tick[k][0] = {(lv_value_precise_t)(CX + 122 * cosf(a)), (lv_value_precise_t)(CY + 122 * sinf(a))};
    v.tick[k][1] = {(lv_value_precise_t)(CX + 114 * cosf(a)), (lv_value_precise_t)(CY + 114 * sinf(a))};
    lv_obj_t *ln = lv_line_create(p);
    lv_line_set_points(ln, v.tick[k], 2);
    lv_obj_set_style_line_width(ln, 2, 0);
    lv_obj_set_style_line_color(ln, hex(C_TEXT), 0);
    lv_obj_set_style_line_opa(ln, LV_OPA_30, 0);
  }

  v.pulse = mk_panel(p, 200, 200, 0, LV_RADIUS_CIRCLE);
  lv_obj_set_style_bg_opa(v.pulse, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(v.pulse, 2, 0);
  lv_obj_set_style_border_opa(v.pulse, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(v.pulse, CX - 100, CY - 100);

  v.modeLbl = mk_label(p, &font_ui14, C_MUTED, "");
  lv_obj_set_style_text_letter_space(v.modeLbl, 2, 0);
  lv_obj_align(v.modeLbl, LV_ALIGN_TOP_MID, 0, 98);

  v.roll = mk_panel(p, 300, 124, 0, 0);
  lv_obj_set_style_bg_opa(v.roll, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(v.roll, CX - 150, 118);
  for (int k = 0; k < 2; k++) {
    v.rollLbl[k] = mk_label(v.roll, &font_n112, C_TEXT, "--");
    lv_obj_set_width(v.rollLbl[k], 300);
    lv_obj_set_style_text_align(v.rollLbl[k], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_y(v.rollLbl[k], k ? 130 : 0);
  }
  v.now = mk_label(p, &font_ui16, C_MUTED, "");
  lv_obj_set_width(v.now, 300);
  lv_obj_set_style_text_align(v.now, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(v.now, CX - 150, 244);
  v.saved = mk_label(p, &font_ui14, C_OK, "Saved");
  lv_obj_set_width(v.saved, 300);
  lv_obj_set_style_text_align(v.saved, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(v.saved, CX - 150, 268);
  lv_obj_add_flag(v.saved, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *m = mk_btn(p, 54, 54, LV_RADIUS_CIRCLE, IC_MINUS, &font_ui20, on_minus, &v);
  lv_obj_set_pos(m, 154, 298);
  lv_obj_t *pl = mk_btn(p, 54, 54, LV_RADIUS_CIRCLE, IC_PLUS, &font_ui20, on_plus, &v);
  lv_obj_set_pos(pl, 272, 298);

  for (int k = 0; k < N_MODES; k++) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %s", MODES[k].icon, MODES[k].label);
    v.chips[k] = mk_btn(p, 72, 44, 14, buf, &font_ui14, on_chip, &v);
    lv_obj_set_user_data(v.chips[k], (void *)(intptr_t)k);
    lv_obj_set_pos(v.chips[k], 9 + k * 78, 362);
  }

  v.autoChip = mk_btn(p, 72, 40, 14, "Auto", &font_ui14, on_fan_auto, &v);
  lv_obj_set_pos(v.autoChip, 9, 414);
  v.fanIco = mk_icon(p, &font_icon28, 28, IC_FAN);
  lv_obj_set_pos(v.fanIco, 92, 420);
  v.slider = lv_slider_create(p);
  lv_slider_set_range(v.slider, 1, N_FANS - 1);
  lv_obj_set_size(v.slider, 192, 14);
  lv_obj_set_pos(v.slider, 132, 427);
  lv_obj_set_style_bg_color(v.slider, hex(C_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(v.slider, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(v.slider, hex(C_TEXT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(v.slider, 7, LV_PART_KNOB);
  lv_obj_remove_flag(v.slider, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_add_event_cb(v.slider, on_fan_slide, LV_EVENT_VALUE_CHANGED, &v);
  lv_obj_add_event_cb(v.slider, on_fan_release, LV_EVENT_RELEASED, &v);
  v.fanLbl = mk_label(p, &font_ui16, C_TEXT, "");
  lv_obj_set_pos(v.fanLbl, 338, 424);
}

static void update_dots() {
  lv_obj_t *act = lv_tileview_get_tile_active(tv);
  for (int i = 0; i < nTiles; i++) {
    lv_obj_t *d = lv_obj_get_child(dotsRow, i);
    bool on = act == tiles[i];
    lv_obj_set_width(d, on ? 22 : 7);
    lv_obj_set_style_bg_color(d, hex(on ? C_TEXT : 0x2E3744), 0);
  }
}

static void build_pages(bool synced) {
  int keep = 0;
  if (tv) {
    lv_obj_t *act = lv_tileview_get_tile_active(tv);
    for (int i = 0; i < nTiles; i++) if (tiles[i] == act) keep = i;
    lv_obj_delete(tv);
    lv_obj_delete(dotsRow);
  }
  close_sheet();
  for (View &v : V) v.built = false;
  builtSynced = synced;

  tv = lv_tileview_create(scrMain);
  lv_obj_set_size(tv, 480, 480);
  lv_obj_set_style_bg_color(tv, hex(C_BG), 0);
  lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  nTiles = synced ? 2 : 3;
  for (int i = 0; i < nTiles; i++)
    tiles[i] = lv_tileview_add_tile(tv, i, 0, i == 0 ? LV_DIR_RIGHT : i == nTiles - 1 ? LV_DIR_LEFT : LV_DIR_HOR);
  build_overview(tiles[0], synced);
  if (synced) build_detail(V[BOTH], tiles[1]);
  else { build_detail(V[0], tiles[1]); build_detail(V[1], tiles[2]); }

  dotsRow = mk_panel(scrMain, 80, 8, 0, 0);
  lv_obj_set_style_bg_opa(dotsRow, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(dotsRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dotsRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dotsRow, 8, 0);
  lv_obj_align(dotsRow, LV_ALIGN_BOTTOM_MID, 0, -8);
  for (int i = 0; i < nTiles; i++) mk_panel(dotsRow, 7, 7, 0x2E3744, 4);
  lv_obj_add_event_cb(tv, on_tile, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_tileview_set_tile_by_index(tv, min(keep, nTiles - 1), 0, LV_ANIM_OFF);
  update_dots();
  lv_obj_move_foreground(offBlock);
}

// ---------------------------------------------------------------- refresh
static void paint_mode(View &v, int mi) {
  const ModeDef &m = MODES[mi];
  for (int k = 0; k < N_MODES; k++) style_chip(v.chips[k], k == mi, MODES[k].c1, MODES[k].c2);
  if (m.ring) lv_obj_set_style_arc_image_src(v.arc, m.ring, LV_PART_INDICATOR);
  else {
    lv_obj_set_style_arc_image_src(v.arc, nullptr, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(v.arc, hex(0x4A525E), LV_PART_INDICATOR);
  }
  if (mi) {
    lv_obj_remove_flag(v.glow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_image_recolor(v.glow, hex(m.glow), 0);
  } else {
    lv_obj_add_flag(v.glow, LV_OBJ_FLAG_HIDDEN);
  }
  char buf[32], up[16];
  for (int i = 0; i < 15 && (up[i] = toupper(m.label[i])); i++) up[i + 1] = 0;
  snprintf(buf, sizeof(buf), mi ? "%s  %s" : "%.0s%s", m.icon, up);
  set_text(v.modeLbl, buf);
  lv_obj_set_style_text_color(v.modeLbl, hex(mi ? m.c1 : C_MUTED), 0);
  lv_obj_set_style_border_color(v.pulse, hex(m.c1), 0);
  lv_obj_set_style_bg_color(v.slider, hex(m.c1), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(v.slider, hex(m.c2), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(v.fanIco, hex(mi ? m.c1 : C_MUTED), 0);
  for (int k = 0; k < 2; k++) lv_obj_set_style_text_opa(v.rollLbl[k], mi ? LV_OPA_COVER : LV_OPA_40, 0);
  if (v.fanIdx >= 0) spin(v.fanIco, mi ? FAN_SPIN_MS[v.fanIdx] : 0, v.spinMs);
}

static void paint_fan(View &v, int fi) {
  bool autoFan = fi == 0;
  style_chip(v.autoChip, autoFan, C_TEXT, 0xC7D0DB);
  if (!autoFan) lv_slider_set_value(v.slider, fi, LV_ANIM_OFF);
  lv_obj_set_style_bg_opa(v.slider, autoFan ? LV_OPA_40 : LV_OPA_COVER, LV_PART_INDICATOR);
  set_text(v.fanLbl, FAN_LABEL[fi]);
  spin(v.fanIco, v.modeIdx > 0 ? FAN_SPIN_MS[fi] : 0, v.spinMs);
}

static void refresh_view(View &v) {
  int u = wunit(v);
  const AcState &a = S.ac[u];
  if (v.sendAt && !held(v.sendAt)) {
    v.sendAt = 0;
    ha_send(CMD_TEMP, u, nullptr, v.target);
    set_pending(v, 1, v.target);
  }
  int mi = modeIndex(a.mode), fi = fanIndex(a.fan);
  if (mi > 0) strlcpy(v.lastOn, a.mode, sizeof(v.lastOn));
  if (!a.valid) return;
  if (!held(v.holdTemp) && !v.sendAt && !isnan(a.setTemp)) { v.target = (int)lroundf(a.setTemp); show_target(v, v.target, true); }
  if (mi >= 0 && !held(v.holdMode) && (mi != v.modeIdx || !v.built)) { v.modeIdx = mi; if (v.built) paint_mode(v, mi); }
  if (fi >= 0 && !held(v.holdFan) && (fi != v.fanIdx || !v.built)) { v.fanIdx = fi; if (v.built) paint_fan(v, fi); }
  if (!v.built) return;
  if (v.shownTarget < 0) show_target(v, v.target, false);

  char buf[64];
  if (v.unit == BOTH) {
    const AcState &b = S.ac[1];
    snprintf(buf, sizeof(buf), "Front %d\xC2\xB0  \xC2\xB7  Family %d\xC2\xB0", isnan(a.curTemp) ? 0 : (int)lroundf(a.curTemp),
             isnan(b.curTemp) ? 0 : (int)lroundf(b.curTemp));
  } else if (isnan(a.curTemp)) snprintf(buf, sizeof(buf), "Room --");
  else snprintf(buf, sizeof(buf), "Room %d\xC2\xB0", (int)lroundf(a.curTemp));
  set_text(v.now, buf);

  // confirmation: wait until HA reports what we sent
  if (v.pendField) {
    bool match = (v.pendField == 1 && !isnan(a.setTemp) && lroundf(a.setTemp) == v.pendVal) ||
                 (v.pendField == 2 && mi == v.pendVal) || (v.pendField == 3 && fi == v.pendVal);
    if (match || !held(v.pendUntil)) {
      stop_pulse(v);
      if (match) {
        const AcState &b = S.ac[1];
        bool behind = v.unit == BOTH && (strcmp(a.mode, b.mode) || (v.pendField == 1 && lroundf(b.setTemp) != v.pendVal) || strcmp(a.fan, b.fan));
        set_text(v.saved, behind ? IC_CHECK "  Saved  \xC2\xB7  Family catching up" : IC_CHECK "  Saved");
        lv_obj_set_style_text_color(v.saved, hex(C_OK), 0);
      } else {
        set_text(v.saved, IC_WARN "  No response from Home Assistant");
        lv_obj_set_style_text_color(v.saved, hex(C_WARN), 0);
      }
      lv_obj_remove_flag(v.saved, LV_OBJ_FLAG_HIDDEN);
      v.savedUntil = millis() + (match ? 2500 : 5000);
      v.pendField = 0;
    }
  } else if (v.savedUntil && !held(v.savedUntil)) {
    v.savedUntil = 0;
    lv_obj_add_flag(v.saved, LV_OBJ_FLAG_HIDDEN);
  }

  uint32_t off = g_set.offAt[u];
  if (off) {
    fmt_remaining(buf, sizeof(buf), off, false);
    char t[24];
    snprintf(t, sizeof(t), IC_TIMER " %s", buf);
    set_text(lv_obj_get_child(v.timerBtn, 0), t);
    lv_obj_set_width(v.timerBtn, 92);
    lv_obj_set_style_text_color(v.timerBtn, hex(C_WARN), 0);
  } else {
    set_text(lv_obj_get_child(v.timerBtn, 0), IC_TIMER);
    lv_obj_set_width(v.timerBtn, 44);
    lv_obj_set_style_text_color(v.timerBtn, hex(C_TEXT), 0);
  }
}

static void refresh_card(int i) {
  OvCard &c = OV[i];
  const AcState &a = S.ac[i];
  View &v = (builtSynced && i == 0) ? V[BOTH] : V[builtSynced ? 1 : i];
  bool useView = !(builtSynced && i == 1);  // synced Family card shows what HA reports
  int mi = useView && held(v.holdMode) ? v.modeIdx : modeIndex(a.mode);
  int target = useView && (held(v.holdTemp) || v.sendAt) ? v.target : (isnan(a.setTemp) ? 0 : (int)lroundf(a.setTemp));
  int fi = useView && held(v.holdFan) ? v.fanIdx : fanIndex(a.fan);
  lv_obj_set_style_opa(c.card, a.valid || !S.haOk ? LV_OPA_COVER : LV_OPA_50, 0);
  char buf[48];
  if (!a.valid || mi < 0) {
    set_text(c.big, "--");
    set_text(c.info1, S.haOk ? "Unavailable" : "");
    return;
  }
  const ModeDef &m = MODES[mi];
  if (isnan(a.curTemp)) set_text(c.big, "--");
  else { snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(a.curTemp)); set_text(c.big, buf); }

  lv_obj_set_style_bg_color(c.card, lv_color_mix(hex(m.c2), hex(C_CARD), mi ? 70 : 0), 0);
  lv_obj_set_style_border_color(c.card, hex(m.c1), 0);
  snprintf(buf, sizeof(buf), mi ? "%s %s" : "%.0s%s", m.icon, m.label);
  set_text(c.pill, buf);
  lv_obj_set_style_text_color(c.pill, hex(mi ? m.c1 : C_MUTED), 0);
  lv_obj_set_style_bg_color(c.pill, hex(mi ? m.c1 : C_CHIP_P), 0);
  lv_obj_align_to(c.pill, c.name, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  if (mi == 0) {
    set_text(c.info1, "Idle");
    set_text(c.info2, "");
    lv_obj_add_flag(c.fanIco, LV_OBJ_FLAG_HIDDEN);
  } else {
    if (mi == 4) snprintf(buf, sizeof(buf), "Fan only");
    else snprintf(buf, sizeof(buf), "%s to %d\xC2\xB0", m.verb, target);
    set_text(c.info1, buf);
    snprintf(buf, sizeof(buf), "Fan %s", fi >= 0 ? FAN_LABEL[fi] : "-");
    set_text(c.info2, buf);
    lv_obj_remove_flag(c.fanIco, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(c.fanIco, hex(m.c1), 0);
  }
  spin(c.fanIco, mi > 0 && fi >= 0 ? FAN_SPIN_MS[fi] : 0, c.spinMs);
  if (g_set.offAt[i]) { fmt_remaining(buf, sizeof(buf), g_set.offAt[i], true); set_text(c.info3, IC_TIMER "  "); lv_label_ins_text(c.info3, LV_LABEL_POS_LAST, buf); }
  else set_text(c.info3, "");

  bool on = mi > 0;
  lv_obj_set_style_bg_color(c.pwr, hex(on ? m.c1 : C_CHIP), 0);
  lv_obj_set_style_bg_grad_color(c.pwr, hex(m.c2), 0);
  lv_obj_set_style_bg_grad_dir(c.pwr, on ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_text_color(c.pwr, hex(on ? C_DARK : C_MUTED), 0);
  lv_obj_set_style_shadow_width(c.pwr, on ? 24 : 0, 0);
  lv_obj_set_style_shadow_color(c.pwr, hex(m.c2), 0);
  lv_obj_set_style_shadow_opa(c.pwr, LV_OPA_60, 0);
}

static void refresh_overlay() {
  bool show = lv_screen_active() == scrMain && !ambient && (S.noCreds || !S.wifi || !S.haOk);
  if (!show) { lv_obj_add_flag(offBlock, LV_OBJ_FLAG_HIDDEN); return; }
  const char *msg = S.noCreds ? IC_WARN "  WiFi isn't set up on this panel."
                  : !S.wifi   ? IC_WIFI "  WiFi disconnected. Reconnecting..."
                  : S.everOk  ? IC_WARN "  Can't reach Home Assistant. Retrying..."
                              : IC_WIFI "  Connecting to Home Assistant...";
  set_text(offLbl, msg);
  lv_obj_set_style_bg_color(offBanner, hex(S.everOk || S.noCreds ? 0x3A1418 : 0x2A2410), 0);
  lv_obj_remove_flag(offBlock, LV_OBJ_FLAG_HIDDEN);
}

static void fire_timers() {
  if (!clock_valid()) return;
  uint32_t now = time(nullptr);
  bool changed = false;
  for (int i = 0; i < AC_COUNT; i++) {
    if (!g_set.offAt[i] || now < g_set.offAt[i]) continue;
    g_set.offAt[i] = 0;
    changed = true;
    if (builtSynced) {  // one write to the Front Room; the flow turns the Family Room off too
      for (int k = 0; k < AC_COUNT; k++) g_set.offAt[k] = 0;
      set_mode(V[BOTH], 0);
      break;
    }
    set_mode(V[i], 0);
  }
  if (changed) settings_save();
}

static void tick(lv_timer_t *) {
  ha_snapshot(S);
  if (rebuildTo >= 0) { build_pages(rebuildTo); rebuildTo = -1; }
  else if (S.syncValid && S.sync != builtSynced && !held(holdSync)) build_pages(S.sync);

  fire_timers();
  if (lv_screen_active() == scrMain) {
    const char *st = "Connected";
    uint32_t col = C_OK;
    if (S.noCreds || !S.wifi) { st = "No WiFi"; col = C_WARN; }
    else if (!S.haOk) { st = S.everOk ? "Offline" : "Connecting"; col = S.everOk ? C_BAD : C_WARN; }
    set_text(statusLbl, st);
    lv_obj_set_style_bg_color(statusDot, hex(col), 0);
    lv_obj_align_to(statusLbl, statusDot, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    if (S.nextValid) {
      char buf[64];
      snprintf(buf, sizeof(buf), IC_CAL "  %s", S.nextAction);
      set_text(nextLbl, buf);
      lv_obj_align(nextLbl, LV_ALIGN_TOP_RIGHT, -20, 34);
      lv_obj_remove_flag(nextLbl, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(nextLbl, LV_OBJ_FLAG_HIDDEN);
    if (S.syncValid && !held(holdSync) && S.sync != lv_obj_has_state(syncSw, LV_STATE_CHECKED)) {
      if (S.sync) lv_obj_add_state(syncSw, LV_STATE_CHECKED); else lv_obj_remove_state(syncSw, LV_STATE_CHECKED);
    }
    for (int i = 0; i < AC_COUNT; i++) refresh_card(i);
  }
  for (View &v : V) if (v.built || v.sendAt) refresh_view(v);
  refresh_overlay();
  if (ambient) amb_update(S);
  else if (lv_screen_active() == set_screen()) set_update(S);
}

// ---------------------------------------------------------------- screens
void ui_enter_ambient() {
  if (ambient) return;
  close_sheet();
  ambient = true;
  prevScreen = lv_screen_active();
  amb_update(S);
  lv_obj_add_flag(offBlock, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load_anim(amb_screen(), LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
}

void ui_exit_ambient() {
  if (!ambient) return;
  ambient = false;
  lv_screen_load(prevScreen ? prevScreen : scrMain);
}

void ui_open_settings() {
  close_sheet();
  lv_screen_load(set_screen());
}
static void on_info(lv_event_t *) { ui_open_settings(); }

void ui_build() {
  scrMain = lv_screen_active();
  lv_obj_set_style_bg_color(scrMain, hex(C_BG), 0);
  lv_obj_remove_flag(scrMain, LV_OBJ_FLAG_SCROLLABLE);

  // offline overlay: dims and blocks the main screen, banner explains why
  offBlock = mk_panel(lv_layer_top(), 480, 480, 0x000000, 0);
  lv_obj_set_style_bg_opa(offBlock, LV_OPA_40, 0);
  lv_obj_add_flag(offBlock, LV_OBJ_FLAG_CLICKABLE);
  offBanner = mk_panel(offBlock, 456, 52, 0x3A1418, 16);
  lv_obj_set_pos(offBanner, 12, 8);
  offLbl = mk_label(offBanner, &font_ui16, C_TEXT, "");
  lv_obj_align(offLbl, LV_ALIGN_LEFT_MID, 16, 0);
  lv_obj_t *info = mk_btn(offBanner, 64, 36, 12, "Info", &font_ui14, on_info, nullptr);
  lv_obj_align(info, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_add_flag(offBlock, LV_OBJ_FLAG_HIDDEN);

  amb_build();
  set_build();
  ha_snapshot(S);
  build_pages(S.syncValid && S.sync);
  lv_timer_create(tick, 250, nullptr);
}

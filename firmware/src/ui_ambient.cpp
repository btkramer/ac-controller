// Ambient screen shown after the idle timeout: clock, outdoor temperature, both rooms, 6-hour trend.
#include "ui_internal.h"
#include <time.h>

static lv_obj_t *scr, *clk, *ampm, *date, *outT, *outL, *next, *chart, *chartCap;
static lv_obj_t *glow[AC_COUNT], *dot[AC_COUNT], *state[AC_COUNT], *temp[AC_COUNT];
static lv_chart_series_t *ser[AC_COUNT];
static int lastHist = -1, lastMode[AC_COUNT] = {-2, -2};
static const char *const DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                                     "August", "September", "October", "November", "December"};

lv_obj_t *amb_screen() { return scr; }
static void show(lv_obj_t *o, bool on) { if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }

void amb_build() {
  scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, hex(0x030406), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  const int gx[] = {-120, 200}, gy[] = {180, -160};
  for (int i = 0; i < AC_COUNT; i++) {
    glow[i] = lv_image_create(scr);
    lv_image_set_src(glow[i], &img_glow);
    lv_obj_set_pos(glow[i], gx[i], gy[i]);
    lv_obj_set_style_image_recolor_opa(glow[i], LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(glow[i], LV_OPA_50, 0);
  }

  clk = mk_label(scr, &font_n150, C_TEXT, "--:--");
  lv_obj_set_pos(clk, 22, 6);
  ampm = mk_label(scr, &font_o34, 0xB8C1CD, "");
  date = mk_label(scr, &font_ui18, 0x8F99A7, "");
  lv_obj_set_pos(date, 30, 168);
  outT = mk_label(scr, &font_n54, C_TEXT, "");
  outL = mk_label(scr, &font_ui14, 0x8F99A7, "");
  next = mk_label(scr, &font_ui16, C_MUTED, "");
  lv_obj_set_pos(next, 30, 198);

  for (int i = 0; i < AC_COUNT; i++) {
    int y = 228 + i * 58;
    lv_obj_t *sep = mk_panel(scr, 420, 1, 0x161C24, 0);
    lv_obj_set_pos(sep, 30, y);
    dot[i] = mk_panel(scr, 10, 10, C_MUTED, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(dot[i], 32, y + 24);
    lv_obj_t *n = mk_label(scr, &font_ui18, C_TEXT, AC_NAME[i]);
    lv_obj_set_pos(n, 56, y + 8);
    state[i] = mk_label(scr, &font_ui14, 0x8F99A7, "");
    lv_obj_set_pos(state[i], 56, y + 32);
    temp[i] = mk_label(scr, &font_n40, C_TEXT, "--");
    lv_obj_set_width(temp[i], 120);
    lv_obj_set_style_text_align(temp[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(temp[i], 330, y + 6);
  }

  chartCap = mk_label(scr, &font_ui14, 0x4A5361, "Room temperature, last 6 hours");
  lv_obj_set_pos(chartCap, 30, 352);
  chart = lv_chart_create(scr);
  lv_obj_set_size(chart, 420, 86);
  lv_obj_set_pos(chart, 30, 376);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(chart, 0, 0);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_pad_all(chart, 4, 0);
  lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
  for (int i = 0; i < AC_COUNT; i++) ser[i] = lv_chart_add_series(chart, hex(C_MUTED), LV_CHART_AXIS_PRIMARY_Y);
}

static const char *weather_icon(const char *w) {
  if (!strcmp(w, "sunny")) return IC_SUN;
  if (!strcmp(w, "clear-night")) return IC_MOON;
  if (!strcmp(w, "partlycloudy")) return IC_CLOUD_SUN;
  if (strstr(w, "rain") || !strcmp(w, "pouring") || strstr(w, "lightning") || strstr(w, "snow") || !strcmp(w, "hail")) return IC_RAIN;
  return IC_CLOUD;
}

static void update_chart(const HaSnapshot &s) {
  if (s.histCount == lastHist) return;
  lastHist = s.histCount;
  int n = s.histCount;
    show(chart, n >= 2);
  set_text(chartCap, n >= 2 ? "Room temperature, last 6 hours" : "Room temperature trend starts after a few minutes");
  if (n < 2) return;
  int lo = INT16_MAX, hi = INT16_MIN;
  for (int i = 0; i < AC_COUNT; i++)
    for (int k = 0; k < n; k++)
      if (s.hist[i][k] != INT16_MIN) { lo = min(lo, (int)s.hist[i][k]); hi = max(hi, (int)s.hist[i][k]); }
  if (lo > hi) return;
  int mid = (lo + hi) / 2, half = max(15, (hi - lo) / 2 + 5);  // at least a 3 degree window
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, mid - half, mid + half);
  lv_chart_set_point_count(chart, n);
  for (int i = 0; i < AC_COUNT; i++)
    for (int k = 0; k < n; k++)
      lv_chart_set_value_by_id(chart, ser[i], k, s.hist[i][k] == INT16_MIN ? LV_CHART_POINT_NONE : s.hist[i][k]);
  lv_chart_refresh(chart);
}

void amb_update(const HaSnapshot &s) {
  char buf[64];
  time_t now = time(nullptr);
  if (clock_valid()) {
    struct tm t;
    localtime_r(&now, &t);
    int h = t.tm_hour % 12 ? t.tm_hour % 12 : 12;
    snprintf(buf, sizeof(buf), "%d:%02d", h, t.tm_min);
    set_text(clk, buf);
    set_text(ampm, t.tm_hour < 12 ? "AM" : "PM");
    snprintf(buf, sizeof(buf), "%s, %s %d", DAYS[t.tm_wday], MONTHS[t.tm_mon], t.tm_mday);
    set_text(date, buf);
  } else {
    set_text(clk, "--:--");
    set_text(ampm, "");
    set_text(date, "Setting the clock...");
  }
  lv_obj_align_to(ampm, clk, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -26);

  if (s.weatherValid) {
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(s.outTemp));
    set_text(outT, buf);
    snprintf(buf, sizeof(buf), "%s  Outside", weather_icon(s.weather));
    set_text(outL, buf);
  } else {
    set_text(outT, "");
    set_text(outL, "");
  }
  lv_obj_align(outT, LV_ALIGN_TOP_RIGHT, -28, 30);
  lv_obj_align_to(outL, outT, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 4);

  if (s.nextValid) { snprintf(buf, sizeof(buf), IC_CAL "  Next: %s", s.nextAction); set_text(next, buf); }
  else set_text(next, "");

  for (int i = 0; i < AC_COUNT; i++) {
    const AcState &a = s.ac[i];
    int mi = a.valid ? modeIndex(a.mode) : -1;
    if (mi != lastMode[i]) {
      lastMode[i] = mi;
      uint32_t c = mi > 0 ? MODES[mi].c1 : 0x4A525E;
      lv_obj_set_style_bg_color(dot[i], hex(c), 0);
      lv_obj_set_style_shadow_width(dot[i], mi > 0 ? 12 : 0, 0);
      lv_obj_set_style_shadow_color(dot[i], hex(c), 0);
      show(glow[i], mi > 0);
      if (mi > 0) lv_obj_set_style_image_recolor(glow[i], hex(MODES[mi].glow), 0);
      lv_chart_set_series_color(chart, ser[i], hex(mi > 0 ? MODES[mi].c1 : C_MUTED));
    }
    if (mi < 0) set_text(state[i], "Unavailable");
    else if (mi == 0) set_text(state[i], "Off");
    else if (mi == 4) set_text(state[i], "Fan only");
    else {
      snprintf(buf, sizeof(buf), "%s to %d\xC2\xB0", MODES[mi].verb, isnan(a.setTemp) ? 0 : (int)lroundf(a.setTemp));
      set_text(state[i], buf);
    }
    if (isnan(a.curTemp)) set_text(temp[i], "--");
    else { snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(a.curTemp)); set_text(temp[i], buf); }
  }
  update_chart(s);
}

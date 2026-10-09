// Shared declarations between the UI files and main.cpp.
#pragma once
#include "theme.h"
#include "ha.h"
#include "settings.h"

// ui.cpp: main screen (overview + room pages), sheets, offline overlay
void ui_build();
const HaSnapshot &ui_snap();
lv_obj_t *ui_main_screen();
bool ui_ambient_active();
void ui_enter_ambient();
void ui_exit_ambient();
void ui_open_settings();

// helpers (ui.cpp)
lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, uint32_t color, const char *txt = "");
lv_obj_t *mk_panel(lv_obj_t *p, int w, int h, uint32_t bg, int radius);
lv_obj_t *mk_btn(lv_obj_t *p, int w, int h, int radius, const char *txt, const lv_font_t *f, lv_event_cb_t cb, void *ud);
void style_chip(lv_obj_t *chip, bool active, uint32_t c1, uint32_t c2);
void set_text(lv_obj_t *label, const char *txt);  // no-op when unchanged, avoids needless redraws
bool clock_valid();

// ui_ambient.cpp
void amb_build();
void amb_update(const HaSnapshot &s);
lv_obj_t *amb_screen();

// ui_settings.cpp
void set_build();
void set_update(const HaSnapshot &s);
lv_obj_t *set_screen();

// main.cpp
void backlight_apply();
const char *fw_version();

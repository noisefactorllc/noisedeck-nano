#include "ui.h"
#include <stdio.h>
#include "../fx/palette.h"
#include "../fx/text.h"

#define ROW_SCALE 3
#define ROW_SPACING 3
#define SMALL_SCALE 2
#define SMALL_SPACING 2

static const char *const ROW_LABELS[UI_ROWS] = {"EFFECT", "PALETTE", "BRIGHT", "AUTO", "SHUFFLE", "SCREEN OFF", "CLOSE"};

void ui_fill_rect(uint16_t *out, int band_w, int y0, int band_h, int x, int y, int w, int h, uint16_t color) {
  int ya = y > y0 ? y : y0;
  int yb = (y + h) < (y0 + band_h) ? (y + h) : (y0 + band_h);
  int xa = x > 0 ? x : 0;
  int xb = (x + w) < band_w ? (x + w) : band_w;
  for (int py = ya; py < yb; py++) {
    uint16_t *line = out + (py - y0) * band_w;
    for (int px = xa; px < xb; px++) line[px] = color;
  }
}

static void text_at(uint16_t *out, int band_w, int y0, int band_h, const char *s, int x, int y, int scale, int spacing,
                    uint16_t color) {
  text_draw_band(out, band_w, y0, band_h, s, x, y, scale, spacing, color);
}

void ui_menu_draw_band(uint16_t *out, int band_w, int y0, int band_h, const ui_model_t *m, int highlight_row) {
  const uint16_t bg = rgb565_be(10, 10, 18);
  const uint16_t border = rgb565_be(0, 229, 255);
  const uint16_t white = rgb565_be(255, 255, 255);
  const uint16_t dim = rgb565_be(150, 150, 170);
  const uint16_t rule = rgb565_be(40, 40, 60);
  const uint16_t hi = rgb565_be(0, 70, 90);
  const int px = UI_PANEL_X, py = UI_PANEL_Y, pw = UI_PANEL_W, ph = UI_PANEL_H;

  if (y0 + band_h <= py || y0 >= py + ph) return;

  ui_fill_rect(out, band_w, y0, band_h, px, py, pw, ph, bg);
  ui_fill_rect(out, band_w, y0, band_h, px, py, pw, 2, border);
  ui_fill_rect(out, band_w, y0, band_h, px, py + ph - 2, pw, 2, border);
  ui_fill_rect(out, band_w, y0, band_h, px, py, 2, ph, border);
  ui_fill_rect(out, band_w, y0, band_h, px + pw - 2, py, 2, ph, border);

  text_at(out, band_w, y0, band_h, "NOISEDECK NANO", px + 20, py + 16, SMALL_SCALE, SMALL_SPACING, border);
  int mw = text_width("MENU", SMALL_SCALE, SMALL_SPACING);
  text_at(out, band_w, y0, band_h, "MENU", px + pw - 20 - mw, py + 16, SMALL_SCALE, SMALL_SPACING, dim);
  ui_fill_rect(out, band_w, y0, band_h, px + 2, UI_ROW_Y0 - 8, pw - 4, 1, rule);

  for (int r = 0; r < UI_ROWS; r++) {
    int ry = UI_ROW_Y0 + r * UI_ROW_H;
    if (ry >= y0 + band_h || ry + UI_ROW_H <= y0) continue;
    if (r == highlight_row) ui_fill_rect(out, band_w, y0, band_h, px + 2, ry, pw - 4, UI_ROW_H, hi);
    int ty = ry + (UI_ROW_H - TEXT_GLYPH_H * ROW_SCALE) / 2;
    char value[24] = "";
    int arrows = 0;
    switch (r) {
    case UI_ROW_EFFECT: snprintf(value, sizeof(value), "%s", m->effect); arrows = 1; break;
    case UI_ROW_PALETTE: snprintf(value, sizeof(value), "%s", m->palette); arrows = !m->mood_locked; break;
    case UI_ROW_BRIGHT: snprintf(value, sizeof(value), "%d%%", m->bright_pct); arrows = 1; break;
    case UI_ROW_AUTO: snprintf(value, sizeof(value), "%s", m->auto_on ? "ON" : "OFF"); break;
    default: break;
    }
    if (arrows) {
      text_at(out, band_w, y0, band_h, "<", px + 20, ty, ROW_SCALE, ROW_SPACING, border);
      text_at(out, band_w, y0, band_h, ">", px + pw - 20 - TEXT_GLYPH_W * ROW_SCALE, ty, ROW_SCALE, ROW_SPACING, border);
    }
    text_at(out, band_w, y0, band_h, ROW_LABELS[r], px + 52, ty, ROW_SCALE, ROW_SPACING, r >= UI_ROW_SHUFFLE ? white : dim);
    if (value[0]) {
      int vw = text_width(value, ROW_SCALE, ROW_SPACING);
      text_at(out, band_w, y0, band_h, value, px + pw - 52 - vw, ty, ROW_SCALE, ROW_SPACING, white);
    }
    if (r < UI_ROWS - 1) ui_fill_rect(out, band_w, y0, band_h, px + 2, ry + UI_ROW_H - 1, pw - 4, 1, rule);
  }

  const char *f1 = "SWIPE: EFFECT / PALETTE";
  const char *f2 = "HOLD+DRAG: STEER   SHAKE: SHUFFLE";
  int fy = UI_ROW_Y0 + UI_ROWS * UI_ROW_H + 6;
  int w1 = text_width(f1, SMALL_SCALE, SMALL_SPACING), w2 = text_width(f2, SMALL_SCALE, SMALL_SPACING);
  text_at(out, band_w, y0, band_h, f1, px + (pw - w1) / 2, fy, SMALL_SCALE, SMALL_SPACING, dim);
  text_at(out, band_w, y0, band_h, f2, px + (pw - w2) / 2, fy + 18, SMALL_SCALE, SMALL_SPACING, dim);
}

int ui_menu_hit(int x, int y, int *action) {
  if (x < UI_PANEL_X || x >= UI_PANEL_X + UI_PANEL_W || y < UI_PANEL_Y || y >= UI_PANEL_Y + UI_PANEL_H) {
    *action = UI_ACT_OUTSIDE;
    return -1;
  }
  if (y < UI_ROW_Y0) {
    *action = UI_ACT_NONE;
    return -1;
  }
  int row = (y - UI_ROW_Y0) / UI_ROW_H;
  if (row >= UI_ROWS) {
    *action = UI_ACT_NONE;
    return -1;
  }
  if (row <= UI_ROW_BRIGHT) *action = (x < UI_PANEL_X + UI_PANEL_W / 2) ? UI_ACT_PREV : UI_ACT_NEXT;
  else *action = UI_ACT_SELECT;
  return row;
}

void ui_hint_draw_band(uint16_t *out, int band_w, int y0, int band_h, const char *const lines[UI_HINT_LINES]) {
  int n = 0;
  for (int i = 0; i < UI_HINT_LINES; i++)
    if (lines[i] && lines[i][0]) n++;
  if (n == 0) return;
  const int line_h = TEXT_GLYPH_H * SMALL_SCALE + 4;
  int strip_h = 10 + n * line_h;
  int sy = 480 - strip_h;
  if (sy >= y0 + band_h) return;
  ui_fill_rect(out, band_w, y0, band_h, 0, sy, band_w, strip_h, rgb565_be(0, 0, 0));
  ui_fill_rect(out, band_w, y0, band_h, 0, sy, band_w, 1, rgb565_be(0, 90, 110));
  int ty = sy + 6;
  for (int i = 0; i < UI_HINT_LINES; i++) {
    if (!lines[i] || !lines[i][0]) continue;
    int w = text_width(lines[i], SMALL_SCALE, SMALL_SPACING);
    uint16_t c = (ty == sy + 6) ? rgb565_be(255, 255, 255) : rgb565_be(0, 229, 255);
    text_at(out, band_w, y0, band_h, lines[i], (band_w - w) / 2, ty, SMALL_SCALE, SMALL_SPACING, c);
    ty += line_h;
  }
}

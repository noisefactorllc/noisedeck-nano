// On-screen menu and hint bar, drawn into band buffers over the running effect.
// Portable C (no Arduino/IDF) so the layout is testable on a host.
#ifndef NANO_UI_H
#define NANO_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { UI_ROW_EFFECT = 0, UI_ROW_PALETTE, UI_ROW_BRIGHT, UI_ROW_AUTO, UI_ROW_SHUFFLE, UI_ROW_SLEEP, UI_ROW_CLOSE, UI_ROWS };
enum { UI_ACT_NONE = 0, UI_ACT_PREV, UI_ACT_NEXT, UI_ACT_SELECT, UI_ACT_OUTSIDE };

#define UI_PANEL_X 20
#define UI_PANEL_Y 20
#define UI_PANEL_W 440
#define UI_PANEL_H 440
#define UI_ROW_Y0 74
#define UI_ROW_H 48
#define UI_HINT_LINES 3

typedef struct {
  const char *effect;
  const char *palette;
  int bright_pct;
  int auto_on;
  int mood_locked;
} ui_model_t;

void ui_fill_rect(uint16_t *out, int band_w, int y0, int band_h, int x, int y, int w, int h, uint16_t color);

// Draw the menu into the band covering rows [y0, y0 + band_h). highlight_row < 0 for none.
void ui_menu_draw_band(uint16_t *out, int band_w, int y0, int band_h, const ui_model_t *m, int highlight_row);

// Map a touch to a menu row and action. Returns the row (or -1); *action tells
// prev/next/select, or UI_ACT_OUTSIDE when the touch is off the panel.
int ui_menu_hit(int x, int y, int *action);

// Bottom hint strip with up to UI_HINT_LINES centred lines (NULL lines are skipped).
void ui_hint_draw_band(uint16_t *out, int band_w, int y0, int band_h, const char *const lines[UI_HINT_LINES]);

#ifdef __cplusplus
}
#endif

#endif

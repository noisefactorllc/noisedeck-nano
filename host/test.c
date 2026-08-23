// Host-side check of the portable engine: renders every effect, prints ASCII
// previews, dumps the font, and soaks the frame loop under sanitizers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../noisedeck_nano/src/fx/fx.h"
#include "../noisedeck_nano/src/fx/palette.h"
#include "../noisedeck_nano/src/fx/text.h"
#include "../noisedeck_nano/src/ui/ui.h"

static uint16_t frame[FX_W * FX_H];
static uint16_t pal[256];
static uint32_t pal32[256];

static int lum_of(uint16_t be) {
  uint16_t c = (uint16_t)((be >> 8) | (be << 8));
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return (r * 8 * 30 + g * 4 * 59 + b * 8 * 11) / 100;
}

static void render_frame(fx_state_t *s, uint32_t t) {
  fx_frame_begin(s, t);
  for (int b = 0; b < FX_BANDS; b++) fx_render_band(s, b * FX_BAND_H, frame + b * FX_BAND_H * FX_W, pal, pal32);
}

static void ascii_preview(const char *title) {
  static const char ramp[] = " .:-=+*#%@";
  printf("--- %s ---\n", title);
  for (int y = 0; y < FX_H; y += 16) {
    for (int x = 0; x < FX_W; x += 6) {
      int l = lum_of(frame[y * FX_W + x]);
      putchar(ramp[l * 9 / 255]);
    }
    putchar('\n');
  }
}

static void font_dump(const char *str) {
  for (int row = 0; row < 7; row++) {
    for (const char *p = str; *p; p++) {
      const uint8_t *g = text_glyph(*p);
      for (int col = 0; col < 5; col++) putchar((g[col] >> row) & 1 ? '#' : ' ');
      putchar(' ');
    }
    putchar('\n');
  }
}

int main(void) {
  fx_state_t *s = calloc(1, sizeof(*s));
  fx_init(s, 12345);
  printf("fx_state_t size: %zu bytes\n", sizeof(*s));

  for (int i = 0; i < PAL_COUNT; i++) {
    palette_build(&PALETTES[i], pal, pal32);
    int lo = 999, hi = -1;
    for (int k = 0; k < 256; k++) { int l = lum_of(pal[k]); if (l < lo) lo = l; if (l > hi) hi = l; }
    printf("palette %-9s lum range %3d..%3d  entry0=%04x entry128=%04x\n", PALETTES[i].name, lo, hi, pal[0], pal[128]);
  }
  palette_build(&PALETTES[5], pal, pal32);
  if (pal[0] != 0x0000) { printf("FAIL mono[0] should be black\n"); return 1; }
  if (pal[128] != 0xFFFF) { printf("FAIL mono[128] should be white: %04x\n", pal[128]); return 1; }
  if (lum_of(pal[255]) > 8) { printf("FAIL mono must loop back to black at 255: %04x\n", pal[255]); return 1; }
  if (rgb565_be(255, 0, 0) != 0x00F8) { printf("FAIL red swap: %04x\n", rgb565_be(255, 0, 0)); return 1; }

  palette_build(&PALETTES[5], pal, pal32);
  const char *names[] = {"plasma", "zone", "flow", "static"};
  for (int f = 0; f < FX_COUNT; f++) {
    fx_set(s, f);
    s->last_ms = 0;
    uint32_t t = 1000;
    for (int i = 0; i < 30; i++) { t += 33; render_frame(s, t); }
    ascii_preview(names[f]);
    long hist[10] = {0};
    for (int i = 0; i < FX_W * FX_H; i++) hist[lum_of(frame[i]) * 9 / 255]++;
    printf("lum histogram:");
    for (int i = 0; i < 10; i++) printf(" %ld", hist[i]);
    printf("\n");
  }

  // Focus tracking + long soak across all effects, catches overflow / UB.
  uint32_t t = 5000;
  for (int f = 0; f < FX_COUNT; f++) {
    fx_set(s, f);
    s->focus_on = 1; s->focus_x = 10; s->focus_y = 470;
    for (int i = 0; i < 3000; i++) { t += 40; render_frame(s, t); }
    s->focus_on = 0;
  }
  fx_set(s, FX_FLOW);
  for (int i = 0; i < 2000; i++) { t += 33; render_frame(s, t); }

  memset(frame, 0, sizeof(frame));
  int w = text_width("NOISE FACTOR", 4, 6);
  for (int b = 0; b < FX_BANDS; b++)
    text_draw_band(frame + b * FX_BAND_H * FX_W, FX_W, b * FX_BAND_H, FX_BAND_H, "NOISE FACTOR", (FX_W - w) / 2, 200, 4, 6, 0xFFFF);
  long lit = 0;
  for (int i = 0; i < FX_W * FX_H; i++) lit += frame[i] != 0;
  printf("splash text width %d px, lit pixels %ld\n", w, lit);
  if (lit == 0) { printf("FAIL no text pixels\n"); return 1; }
  font_dump("NOISEDECK NANO");
  font_dump("by noisefactor.io");
  font_dump("<>%+=^?() 60%");

  // Menu over a dimmed effect, previewed at finer sampling so the rows are legible.
  palette_build(&PALETTES[0], pal, pal32);
  uint16_t dimp[256]; uint32_t dimp32[256];
  palette_dim(pal, dimp, dimp32, 22);
  fx_set(s, FX_PLASMA);
  fx_frame_begin(s, t += 33);
  ui_model_t model = {"zone", "acid", 60, 1, 0};
  for (int b = 0; b < FX_BANDS; b++) {
    fx_render_band(s, b * FX_BAND_H, frame + b * FX_BAND_H * FX_W, dimp, dimp32);
    ui_menu_draw_band(frame + b * FX_BAND_H * FX_W, FX_W, b * FX_BAND_H, FX_BAND_H, &model, UI_ROW_PALETTE);
  }
  printf("--- menu (1 char = 4x12 px) ---\n");
  for (int y = 0; y < FX_H; y += 12) {
    for (int x = 0; x < FX_W; x += 4) { int l = lum_of(frame[y * FX_W + x]); putchar(" .:-=+*#%@"[l * 9 / 255]); }
    putchar('\n');
  }
  int act;
  if (ui_menu_hit(5, 240, &act) != -1 || act != UI_ACT_OUTSIDE) { printf("FAIL outside hit\n"); return 1; }
  if (ui_menu_hit(60, UI_ROW_Y0 + 10, &act) != UI_ROW_EFFECT || act != UI_ACT_PREV) { printf("FAIL effect prev\n"); return 1; }
  if (ui_menu_hit(400, UI_ROW_Y0 + UI_ROW_H + 10, &act) != UI_ROW_PALETTE || act != UI_ACT_NEXT) { printf("FAIL palette next\n"); return 1; }
  if (ui_menu_hit(240, UI_ROW_Y0 + 6 * UI_ROW_H + 10, &act) != UI_ROW_CLOSE || act != UI_ACT_SELECT) { printf("FAIL close\n"); return 1; }
  if (ui_menu_hit(240, UI_ROW_Y0 + 7 * UI_ROW_H + 10, &act) != -1 || act != UI_ACT_NONE) { printf("FAIL footer\n"); return 1; }
  printf("menu hit tests ok\n");

  const char *hints[UI_HINT_LINES] = {"SWIPE LEFT/RIGHT: EFFECT", "SWIPE UP/DOWN: PALETTE", "TAP: MENU   SHAKE: SHUFFLE"};
  memset(frame, 0, sizeof(frame));
  for (int b = 0; b < FX_BANDS; b++) ui_hint_draw_band(frame + b * FX_BAND_H * FX_W, FX_W, b * FX_BAND_H, FX_BAND_H, hints);
  printf("--- hint strip, bottom 72 rows (1 char = 3x6 px) ---\n");
  for (int y = FX_H - 72; y < FX_H; y += 6) {
    for (int x = 0; x < FX_W; x += 3) { int l = lum_of(frame[y * FX_W + x]); putchar(l > 40 ? '#' : ' '); }
    putchar('\n');
  }

  clock_t c0 = clock();
  fx_set(s, FX_PLASMA);
  for (int i = 0; i < 100; i++) { t += 33; render_frame(s, t); }
  clock_t c1 = clock();
  printf("host plasma: %.2f ms/frame\n", (double)(c1 - c0) * 1000.0 / CLOCKS_PER_SEC / 100.0);
  printf("OK\n");
  free(s);
  return 0;
}

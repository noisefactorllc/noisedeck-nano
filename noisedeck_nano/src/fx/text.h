// Tiny 5x7 bitmap font (upper and lower case, digits, a little punctuation),
// blitted into band buffers for the splash, menu and overlays. Unknown glyphs draw blank.
#ifndef NANO_TEXT_H
#define NANO_TEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEXT_GLYPH_W 5
#define TEXT_GLYPH_H 7

int text_width(const char *str, int scale, int spacing);

// Draw str with its top-left at (x, y) in full-resolution coordinates into a band
// buffer covering rows [y0, y0 + band_h). Rows outside the band are skipped.
void text_draw_band(uint16_t *out, int band_w, int y0, int band_h, const char *str, int x, int y, int scale,
                    int spacing, uint16_t color);

// For host tests: returns the 5 column bytes for a glyph.
const uint8_t *text_glyph(char c);

#ifdef __cplusplus
}
#endif

#endif

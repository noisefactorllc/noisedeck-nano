// Looped 256-entry palettes built from a few RGB stops. The last stop wraps back to
// the first so effects that wrap their index through the palette stay seamless.
#ifndef NANO_PALETTE_H
#define NANO_PALETTE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *name;
  int n;
  const uint8_t (*stops)[3];
} pal_def_t;

extern const pal_def_t PALETTES[];
extern const int PAL_COUNT;

// Mood palettes are not part of the shuffle rotation; they are selected by the
// serial `mood` command so the device can act as an ops status light.
extern const pal_def_t PAL_MOOD_OK;
extern const pal_def_t PAL_MOOD_WARN;
extern const pal_def_t PAL_MOOD_CRIT;

int palette_by_name(const char *name);

// out: 256 RGB565 entries, byte-swapped for the panel (big-endian on the wire).
// out32: each entry duplicated into both halves of a uint32 for 2-pixel stores.
void palette_build(const pal_def_t *def, uint16_t *out, uint32_t *out32);

// Scale every entry of a built palette to pct percent brightness (menu backdrop).
void palette_dim(const uint16_t *in, uint16_t *out, uint32_t *out32, int pct);

// Big-endian RGB565 as the CO5300 expects it when pushed by DMA.
uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif

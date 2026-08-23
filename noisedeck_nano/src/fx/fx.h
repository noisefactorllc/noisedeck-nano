// Noisedeck Nano procedural effects engine.
// Pure integer C99: no floats in the per-pixel paths (the ESP32-C6 has no FPU),
// no Arduino or IDF dependencies, so it also compiles and runs on a host for tests.
#ifndef NANO_FX_H
#define NANO_FX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FX_W 480
#define FX_H 480
#define FX_BAND_H 48
#define FX_BANDS (FX_H / FX_BAND_H)

enum { FX_PLASMA = 0, FX_ZONE, FX_FLOW, FX_STATIC, FX_COUNT };

typedef struct {
  int fx;
  uint32_t rng;
  uint32_t now_ms, last_ms;
  int speed_pct;

  // Touch focal point in full-resolution pixels.
  int focus_x, focus_y, focus_on;

  // Plasma: phases are 24.8 fixed LUT-index units, spatial freqs are 8.8.
  uint32_t pp[4];
  int32_t pk[3];
  uint32_t lfo;
  int pcx, pcy;

  // Zone plates: two ring centres in full-res pixels, phases 24.8.
  int zax, zay, zbx, zby;
  uint32_t zpa, zpb;
  int zka, zkb;

  // Flow: 32x32 value-noise lattices, two octaves, each morphing between keyframes.
  uint8_t la[1024], lb[1024], lcur[1024];
  uint8_t ma[1024], mb[1024], mcur[1024];
  uint32_t lf, mf;
  int32_t ox, oy;
  uint32_t oang;

  // Slow global palette rotation, 24.8.
  uint32_t pal_shift;

  // Per-frame per-column scratch tables.
  uint16_t colA[FX_W];
  int32_t colB[FX_W];
  int32_t colC[FX_W];
} fx_state_t;

extern uint8_t fx_sin8[256];

void fx_init(fx_state_t *s, uint32_t seed);
void fx_set(fx_state_t *s, int fx);
int fx_scale(int fx);
const char *fx_name(int fx);
int fx_by_name(const char *name);
uint32_t fx_rand(fx_state_t *s);

// Advance time and precompute per-frame tables. Call once per frame before rendering bands.
void fx_frame_begin(fx_state_t *s, uint32_t now_ms);

// Render FX_BAND_H rows starting at y0 into out (FX_W * FX_BAND_H pixels, RGB565 as
// stored in pal). pal32 holds each palette entry duplicated into both halves of a
// uint32 so pixel-doubled effects can write two pixels per store.
void fx_render_band(fx_state_t *s, int y0, uint16_t *out, const uint16_t *pal, const uint32_t *pal32);

// TV static, used both as an effect and as the transition between states.
void fx_render_static_band(fx_state_t *s, int y0, uint16_t *out, const uint16_t *pal);

#ifdef __cplusplus
}
#endif

#endif

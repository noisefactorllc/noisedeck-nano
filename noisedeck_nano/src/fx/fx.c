#include "fx.h"
#include <math.h>
#include <string.h>

uint8_t fx_sin8[256];
static uint8_t smooth8[256];
static uint8_t sqrt8[2048];

static const char *const FX_NAMES[FX_COUNT] = {"plasma", "zone", "flow", "static"};

static uint32_t xorshift(uint32_t *s) {
  uint32_t x = *s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *s = x;
  return x;
}

uint32_t fx_rand(fx_state_t *s) { return xorshift(&s->rng); }

static void fill_lattice(fx_state_t *s, uint8_t *l) {
  for (int i = 0; i < 1024; i++) l[i] = (uint8_t)(xorshift(&s->rng) >> 24);
}

const char *fx_name(int fx) { return (fx >= 0 && fx < FX_COUNT) ? FX_NAMES[fx] : "?"; }

int fx_by_name(const char *name) {
  for (int i = 0; i < FX_COUNT; i++) {
    const char *a = FX_NAMES[i], *b = name;
    while (*a && *b && *a == *b) { a++; b++; }
    if (*a == 0 && *b == 0) return i;
  }
  return -1;
}

int fx_scale(int fx) { return (fx == FX_PLASMA || fx == FX_FLOW) ? 2 : 1; }

void fx_init(fx_state_t *s, uint32_t seed) {
  memset(s, 0, sizeof(*s));
  s->rng = seed ? seed : 0x9E3779B9u;
  for (int i = 0; i < 256; i++) {
    fx_sin8[i] = (uint8_t)(127.5 + 127.5 * sin(i * 6.283185307179586 / 256.0));
    double t = i / 255.0;
    smooth8[i] = (uint8_t)(255.0 * t * t * (3.0 - 2.0 * t) + 0.5);
  }
  for (int i = 0; i < 2048; i++) sqrt8[i] = (uint8_t)(sqrt((double)(i << 7)) * 255.0 / 512.0);
  s->speed_pct = 100;
  s->pcx = s->pcy = 120;
  s->zax = 160; s->zay = 200; s->zbx = 320; s->zby = 280;
  s->zka = 6; s->zkb = 6;
  fill_lattice(s, s->la); fill_lattice(s, s->lb);
  fill_lattice(s, s->ma); fill_lattice(s, s->mb);
  fx_set(s, FX_PLASMA);
}

void fx_set(fx_state_t *s, int fx) {
  if (fx < 0 || fx >= FX_COUNT) fx = 0;
  s->fx = fx;
  if (fx == FX_ZONE) {
    s->zka = 6;
    s->zkb = 5 + (int)(fx_rand(s) % 2u);
  }
  if (fx == FX_FLOW) {
    fill_lattice(s, s->la); fill_lattice(s, s->lb);
    fill_lattice(s, s->ma); fill_lattice(s, s->mb);
    s->lf = s->mf = 0;
  }
}

static int lfo_val(const fx_state_t *s, uint32_t mult, int phase) {
  return (int)fx_sin8[(((s->lfo * mult) >> 8) + phase) & 255] - 128;
}

static void blend_lattice(uint8_t *cur, const uint8_t *a, const uint8_t *b, int f) {
  for (int i = 0; i < 1024; i++) cur[i] = (uint8_t)(a[i] + ((((int)b[i] - (int)a[i]) * f) >> 8));
}

void fx_frame_begin(fx_state_t *s, uint32_t now_ms) {
  uint32_t dt = s->last_ms ? now_ms - s->last_ms : 0;
  if (dt > 100) dt = 100;
  s->last_ms = now_ms;
  s->now_ms = now_ms;
  uint32_t dts = dt * (uint32_t)s->speed_pct / 100u;

  s->pal_shift += dts * 5;
  s->lfo += dts * 3;

  switch (s->fx) {
  case FX_PLASMA: {
    s->pp[0] += dts * 22; s->pp[1] += dts * 17; s->pp[2] += dts * 13; s->pp[3] += dts * 31;
    s->pk[0] = 0x180 + ((lfo_val(s, 1, 0) + 128) * 0x100 >> 8);
    s->pk[1] = 0x140 + ((lfo_val(s, 1, 85) + 128) * 0xC0 >> 8);
    s->pk[2] = 0x0C0 + ((lfo_val(s, 1, 170) + 128) * 0x80 >> 8);
    int tx, ty;
    if (s->focus_on) { tx = s->focus_x >> 1; ty = s->focus_y >> 1; }
    else { tx = 120 + (lfo_val(s, 3, 0) * 70 >> 7); ty = 120 + (lfo_val(s, 2, 64) * 70 >> 7); }
    s->pcx += (tx - s->pcx) >> 3;
    s->pcy += (ty - s->pcy) >> 3;
    int p0 = (int)(s->pp[0] >> 8);
    for (int x = 0; x < FX_W / 2; x++) {
      s->colA[x] = fx_sin8[(((x * s->pk[0]) >> 8) + p0) & 255];
      s->colB[x] = (x * s->pk[2]) >> 8;
      int dx = x - s->pcx;
      s->colC[x] = dx * dx;
    }
    break;
  }
  case FX_ZONE: {
    s->zpa += dts * 40; s->zpb += dts * 28;
    int tax, tay;
    if (s->focus_on) { tax = s->focus_x; tay = s->focus_y; }
    else { tax = 240 + (lfo_val(s, 4, 0) * 160 >> 7); tay = 240 + (lfo_val(s, 3, 64) * 160 >> 7); }
    s->zax += (tax - s->zax) >> 3;
    s->zay += (tay - s->zay) >> 3;
    int tbx = 240 + (lfo_val(s, 2, 128) * 180 >> 7);
    int tby = 240 + (lfo_val(s, 5, 200) * 180 >> 7);
    s->zbx += (tbx - s->zbx) >> 3;
    s->zby += (tby - s->zby) >> 3;
    for (int x = 0; x < FX_W; x++) {
      int dxa = x - s->zax, dxb = x - s->zbx;
      s->colB[x] = dxa * dxa;
      s->colC[x] = dxb * dxb;
    }
    break;
  }
  case FX_FLOW: {
    s->lf += dts * 26;
    if (s->lf >= 65536u) { memcpy(s->la, s->lb, 1024); fill_lattice(s, s->lb); s->lf -= 65536u; }
    s->mf += dts * 44;
    if (s->mf >= 65536u) { memcpy(s->ma, s->mb, 1024); fill_lattice(s, s->mb); s->mf -= 65536u; }
    blend_lattice(s->lcur, s->la, s->lb, (int)(s->lf >> 8));
    blend_lattice(s->mcur, s->ma, s->mb, (int)(s->mf >> 8));
    s->oang += dts * 2;
    int vx = (int)fx_sin8[((s->oang >> 8) + 64) & 255] - 128;
    int vy = (int)fx_sin8[(s->oang >> 8) & 255] - 128;
    s->ox += (vx * (int)dts * 6) >> 7;
    s->oy += (vy * (int)dts * 6) >> 7;
    int32_t ox2 = s->ox + (s->ox >> 1);
    for (int x = 0; x < FX_W / 2; x++) {
      int32_t sx = (x << 8) + s->ox;
      s->colA[x] = (uint16_t)((((sx >> 12) & 31) << 8) | smooth8[(sx >> 4) & 255]);
      int32_t sx2 = (x << 8) + ox2;
      s->colC[x] = (((sx2 >> 11) & 31) << 8) | smooth8[(sx2 >> 3) & 255];
    }
    break;
  }
  default:
    break;
  }
}

static void render_plasma(fx_state_t *s, int y0, uint16_t *out, const uint32_t *pal32) {
  const int W2 = FX_W / 2;
  const int shift = (int)(s->pal_shift >> 8);
  const int p1 = (int)(s->pp[1] >> 8), p2 = (int)(s->pp[2] >> 8), p4 = (int)(s->pp[3] >> 8);
  const uint16_t *colA = s->colA;
  const int32_t *colB = s->colB, *colC = s->colC;
  for (int r = 0; r < FX_BAND_H / 2; r++) {
    int y = (y0 >> 1) + r;
    int rowT = fx_sin8[(((y * s->pk[1]) >> 8) + p1) & 255] + shift;
    int rowD = ((y * s->pk[2]) >> 8) + p2;
    int dy = y - s->pcy;
    int dy2 = dy * dy;
    uint32_t *o0 = (uint32_t *)(out + (r * 2) * FX_W);
    uint32_t *o1 = o0 + W2;
    for (int x = 0; x < W2; x++) {
      int v = colA[x] + fx_sin8[(colB[x] + rowD) & 255] + fx_sin8[((sqrt8[(colC[x] + dy2) >> 7] << 1) + p4) & 255];
      uint32_t c = pal32[((v >> 2) + rowT) & 255];
      o0[x] = c;
      o1[x] = c;
    }
  }
}

static void render_zone(fx_state_t *s, int y0, uint16_t *out, const uint16_t *pal) {
  const int shift = (int)(s->pal_shift >> 8);
  const int pa = (int)(s->zpa >> 8), pb = (int)(s->zpb >> 8);
  const int ka = s->zka, kb = s->zkb;
  const int32_t *colB = s->colB, *colC = s->colC;
  for (int r = 0; r < FX_BAND_H; r++) {
    int y = y0 + r;
    int dya = y - s->zay, dyb = y - s->zby;
    int dya2 = dya * dya, dyb2 = dyb * dyb;
    uint16_t *o = out + r * FX_W;
    for (int x = 0; x < FX_W; x++) {
      int va = fx_sin8[(((colB[x] + dya2) >> ka) + pa) & 255];
      int vb = fx_sin8[(((colC[x] + dyb2) >> kb) - pb) & 255];
      o[x] = pal[(((va + vb) >> 1) + shift) & 255];
    }
  }
}

static inline int bilerp(const uint8_t *r0, const uint8_t *r1, int cx, int wx, int wy) {
  int cx1 = (cx + 1) & 31;
  int n00 = r0[cx], n10 = r0[cx1], n01 = r1[cx], n11 = r1[cx1];
  int top = n00 + (((n10 - n00) * wx) >> 8);
  int bot = n01 + (((n11 - n01) * wx) >> 8);
  return top + (((bot - top) * wy) >> 8);
}

static void render_flow(fx_state_t *s, int y0, uint16_t *out, const uint32_t *pal32) {
  const int W2 = FX_W / 2;
  const int shift = (int)(s->pal_shift >> 8);
  const int32_t oy2 = s->oy + (s->oy >> 1);
  const uint16_t *colA = s->colA;
  const int32_t *colC = s->colC;
  for (int r = 0; r < FX_BAND_H / 2; r++) {
    int y = (y0 >> 1) + r;
    int32_t sy = (y << 8) + s->oy;
    int cy = (sy >> 12) & 31;
    int wy = smooth8[(sy >> 4) & 255];
    const uint8_t *r0 = s->lcur + cy * 32;
    const uint8_t *r1 = s->lcur + ((cy + 1) & 31) * 32;
    int32_t sy2 = (y << 8) + oy2;
    int cy2 = (sy2 >> 11) & 31;
    int wy2 = smooth8[(sy2 >> 3) & 255];
    const uint8_t *m0 = s->mcur + cy2 * 32;
    const uint8_t *m1 = s->mcur + ((cy2 + 1) & 31) * 32;
    uint32_t *o0 = (uint32_t *)(out + (r * 2) * FX_W);
    uint32_t *o1 = o0 + W2;
    for (int x = 0; x < W2; x++) {
      int a = colA[x];
      int v1 = bilerp(r0, r1, a >> 8, a & 255, wy);
      int b = colC[x];
      int v2 = bilerp(m0, m1, b >> 8, b & 255, wy2);
      int v = (v1 * 3 + v2) >> 2;
      uint32_t c = pal32[((v << 1) + shift) & 255];
      o0[x] = c;
      o1[x] = c;
    }
  }
}

void fx_render_static_band(fx_state_t *s, int y0, uint16_t *out, const uint16_t *pal) {
  (void)y0;
  uint32_t r = s->rng;
  uint16_t *end = out + FX_W * FX_BAND_H;
  while (out < end) {
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    out[0] = pal[r >> 24];
    out[1] = pal[(r >> 16) & 255];
    out[2] = pal[(r >> 8) & 255];
    out[3] = pal[r & 255];
    out += 4;
  }
  s->rng = r;
}

void fx_render_band(fx_state_t *s, int y0, uint16_t *out, const uint16_t *pal, const uint32_t *pal32) {
  switch (s->fx) {
  case FX_PLASMA: render_plasma(s, y0, out, pal32); break;
  case FX_ZONE: render_zone(s, y0, out, pal); break;
  case FX_FLOW: render_flow(s, y0, out, pal32); break;
  default: fx_render_static_band(s, y0, out, pal); break;
  }
}

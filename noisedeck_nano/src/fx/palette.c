#include "palette.h"

static const uint8_t S_DECK[][3]     = {{5, 3, 15}, {26, 11, 94}, {0, 229, 255}, {248, 250, 255}, {255, 43, 214}, {70, 0, 120}};
static const uint8_t S_MAGMA[][3]    = {{0, 0, 0}, {58, 10, 10}, {194, 24, 7}, {255, 159, 28}, {255, 246, 213}, {120, 40, 0}};
static const uint8_t S_PHOSPHOR[][3] = {{0, 0, 0}, {0, 40, 10}, {0, 255, 102}, {204, 255, 221}, {0, 120, 60}};
static const uint8_t S_VAPOR[][3]    = {{46, 26, 71}, {255, 110, 199}, {127, 219, 255}, {255, 244, 194}, {120, 60, 160}};
static const uint8_t S_ICE[][3]      = {{2, 8, 20}, {10, 61, 98}, {96, 163, 217}, {255, 255, 255}, {40, 90, 140}};
static const uint8_t S_MONO[][3]     = {{0, 0, 0}, {255, 255, 255}};
static const uint8_t S_ACID[][3]     = {{0, 0, 0}, {127, 255, 0}, {255, 0, 255}, {20, 0, 40}};
static const uint8_t S_SUNSET[][3]   = {{27, 16, 53}, {106, 5, 114}, {255, 78, 0}, {255, 209, 102}, {120, 30, 80}};
static const uint8_t S_OK[][3]       = {{2, 26, 22}, {11, 110, 90}, {46, 230, 184}, {232, 255, 249}, {8, 70, 58}};
static const uint8_t S_WARN[][3]     = {{28, 16, 0}, {138, 75, 0}, {255, 176, 0}, {255, 241, 201}, {90, 50, 0}};
static const uint8_t S_CRIT[][3]     = {{26, 0, 0}, {122, 0, 0}, {255, 26, 26}, {255, 209, 209}, {80, 0, 0}};

#define DEF(nm, arr) {nm, (int)(sizeof(arr) / sizeof(arr[0])), arr}

const pal_def_t PALETTES[] = {
  DEF("deck", S_DECK),
  DEF("magma", S_MAGMA),
  DEF("phosphor", S_PHOSPHOR),
  DEF("vapor", S_VAPOR),
  DEF("ice", S_ICE),
  DEF("mono", S_MONO),
  DEF("acid", S_ACID),
  DEF("sunset", S_SUNSET),
};
const int PAL_COUNT = (int)(sizeof(PALETTES) / sizeof(PALETTES[0]));

const pal_def_t PAL_MOOD_OK = DEF("ok", S_OK);
const pal_def_t PAL_MOOD_WARN = DEF("warn", S_WARN);
const pal_def_t PAL_MOOD_CRIT = DEF("crit", S_CRIT);

int palette_by_name(const char *name) {
  for (int i = 0; i < PAL_COUNT; i++) {
    const char *a = PALETTES[i].name, *b = name;
    while (*a && *b && *a == *b) { a++; b++; }
    if (*a == 0 && *b == 0) return i;
  }
  return -1;
}

uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return (uint16_t)((c >> 8) | (c << 8));
}

void palette_dim(const uint16_t *in, uint16_t *out, uint32_t *out32, int pct) {
  for (int i = 0; i < 256; i++) {
    uint16_t c = (uint16_t)((in[i] >> 8) | (in[i] << 8));
    int r = ((c >> 11) & 31) * pct / 100, g = ((c >> 5) & 63) * pct / 100, b = (c & 31) * pct / 100;
    uint16_t d = (uint16_t)((r << 11) | (g << 5) | b);
    d = (uint16_t)((d >> 8) | (d << 8));
    out[i] = d;
    if (out32) out32[i] = ((uint32_t)d << 16) | d;
  }
}

void palette_build(const pal_def_t *def, uint16_t *out, uint32_t *out32) {
  int n = def->n;
  for (int i = 0; i < 256; i++) {
    int pos = i * n;              // position in stops * 256
    int seg = pos >> 8;
    int f = pos & 255;
    const uint8_t *a = def->stops[seg % n];
    const uint8_t *b = def->stops[(seg + 1) % n];
    uint8_t r = (uint8_t)(a[0] + (((int)b[0] - (int)a[0]) * f >> 8));
    uint8_t g = (uint8_t)(a[1] + (((int)b[1] - (int)a[1]) * f >> 8));
    uint8_t bl = (uint8_t)(a[2] + (((int)b[2] - (int)a[2]) * f >> 8));
    uint16_t c = rgb565_be(r, g, bl);
    out[i] = c;
    if (out32) out32[i] = ((uint32_t)c << 16) | c;
  }
}

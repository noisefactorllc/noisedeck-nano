// Noisedeck Nano: a pocket generative-noise instrument for the Waveshare
// ESP32-C6-Touch-AMOLED-2.16. Procedural plasma / zone-plate / flow / static
// effects through looped palettes, with an on-screen menu, shuffled by touch,
// button, or a shake, and steerable over USB serial so it can double as an ops
// mood light.
//
// On the device:
//   tap                 open the menu (effect, palette, brightness, auto, shuffle,
//                       screen off, close); tap a row's left/right half to step it
//   swipe left/right    previous/next effect        (judged by distance, not speed)
//   swipe up/down       previous/next palette
//   hold, then drag     steer the effect's focal point
//   BOOT button, shake  shuffle effect and palette
//   SCREEN OFF          sleeps; any touch, the button, or a shake wakes it
//
// Serial (115200, newline-terminated):
//   up                      health JSON in the scaffold /up shape
//   help                    command list
//   fx <name|n|next|prev|rand>
//   pal <name|n|next|prev|rand>
//   mood ok|warn|crit|off   lock a status palette (crit also speeds up + flickers)
//   auto on|off|<seconds>   auto-shuffle
//   bright <0-255>
//   speed <10-400>          percent
//   text <message>          flash a message on screen
//   menu | sleep | wake | shuffle

#include <Arduino.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <esp_heap_caps.h>
#include "src/bsp/board.h"
#include "src/bsp/i2c_bus.h"
#include "src/bsp/pmic.h"
#include "src/bsp/touch_cst9220.h"
#include "src/bsp/imu_qmi8658.h"
#include "src/display.h"
#include "src/fx/fx.h"
#include "src/fx/palette.h"
#include "src/fx/text.h"
#include "src/ui/ui.h"

#define NANO_VERSION "0.2"

static I2cBus bus;
static TouchCst9220 touch;
static ImuQmi8658 imu;
static bool imu_ok = false, touch_ok = false, display_ok = false;

static fx_state_t fx;
static uint16_t pal[256], pal_dim[256];
static uint32_t pal32[256], pal32_dim[256];
static int pal_idx = 0;

enum Mood { MOOD_NONE, MOOD_OK, MOOD_WARN, MOOD_CRIT };
static Mood mood = MOOD_NONE;
static int base_speed = 100;
static uint8_t brightness = 200;
static bool auto_shuffle = true;
static uint32_t auto_interval_ms = 60000, next_auto = 0;
static uint32_t static_until = 0, splash_until = 0, focus_until = 0, next_flicker = 0;

enum Mode { MODE_RUN, MODE_MENU, MODE_SLEEP };
static Mode mode = MODE_RUN;
static uint32_t menu_idle_until = 0;
static int menu_highlight = -1;
static bool menu_seen = false;

struct Overlay {
  char text[40];
  uint32_t until;
  int scale;
  uint16_t color;
};
static Overlay overlay = {{0}, 0, 3, 0xFFFF};

static const char *hint_lines[UI_HINT_LINES] = {NULL, NULL, NULL};
static uint32_t hint_until = 0;   // 0 while the hint is persistent

static uint32_t frames = 0, fps_t0 = 0, next_status = 0;
static float fps = 0;

struct TouchState {
  bool down;
  uint32_t t0;
  int x0, y0, x, y;
  bool moved_early, steer, swallow;
  int press_row;
};
static TouchState tp = {};

static bool btn_last = true;
static uint32_t btn_t = 0;

static uint32_t imu_next = 0, shake_hits = 0, shake_first = 0, shake_cooldown = 0;

static const uint16_t COL_WHITE = 0xFFFF;
static uint16_t COL_CYAN, COL_BLACK;

static const char *mode_name() { return mode == MODE_MENU ? "menu" : (mode == MODE_SLEEP ? "sleep" : "run"); }

static const char *mood_name() {
  switch (mood) {
  case MOOD_OK: return "ok";
  case MOOD_WARN: return "warn";
  case MOOD_CRIT: return "crit";
  default: return "none";
  }
}

static const char *palette_name() {
  switch (mood) {
  case MOOD_OK: return PAL_MOOD_OK.name;
  case MOOD_WARN: return PAL_MOOD_WARN.name;
  case MOOD_CRIT: return PAL_MOOD_CRIT.name;
  default: return PALETTES[pal_idx].name;
  }
}

static int bright_pct() { return (brightness * 100 + 127) / 255; }

static void apply_speed() {
  int factor = 100;
  if (mood == MOOD_OK) factor = 70;
  else if (mood == MOOD_WARN) factor = 130;
  else if (mood == MOOD_CRIT) factor = 200;
  fx.speed_pct = base_speed * factor / 100;
}

static void apply_palette() {
  const pal_def_t *def = &PALETTES[pal_idx];
  if (mood == MOOD_OK) def = &PAL_MOOD_OK;
  else if (mood == MOOD_WARN) def = &PAL_MOOD_WARN;
  else if (mood == MOOD_CRIT) def = &PAL_MOOD_CRIT;
  palette_build(def, pal, pal32);
  palette_dim(pal, pal_dim, pal32_dim, 22);
}

static void burst(uint32_t ms) { static_until = millis() + ms; }

static void say(const char *text, uint32_t ms, int scale, uint16_t color) {
  strncpy(overlay.text, text, sizeof(overlay.text) - 1);
  overlay.text[sizeof(overlay.text) - 1] = 0;
  overlay.until = millis() + ms;
  overlay.scale = scale;
  overlay.color = color;
}

static void show_hint(const char *l1, const char *l2, const char *l3, uint32_t ms) {
  hint_lines[0] = l1;
  hint_lines[1] = l2;
  hint_lines[2] = l3;
  hint_until = ms ? millis() + ms : 0;
}

static void clear_hint() { hint_lines[0] = hint_lines[1] = hint_lines[2] = NULL; hint_until = 0; }

static void hint_after_menu() {
  show_hint("SWIPE LEFT/RIGHT: EFFECT", "SWIPE UP/DOWN: PALETTE", "TAP: MENU   SHAKE: SHUFFLE", 6000);
}

static void announce_state() {
  char msg[40];
  snprintf(msg, sizeof(msg), "%s / %s", fx_name(fx.fx), palette_name());
  say(msg, 1500, 3, COL_WHITE);
}

static void step_palette(int dir) {
  if (mood != MOOD_NONE) {
    say("PALETTE LOCKED BY MOOD", 1200, 2, COL_CYAN);
    return;
  }
  pal_idx = ((pal_idx + dir) % PAL_COUNT + PAL_COUNT) % PAL_COUNT;
  apply_palette();
  burst(120);
}

static void set_fx(int id) {
  fx_set(&fx, ((id % FX_COUNT) + FX_COUNT) % FX_COUNT);
  burst(160);
}

static void shuffle_all() {
  int next = fx.fx;
  while (next == fx.fx) next = (int)(fx_rand(&fx) % (uint32_t)FX_COUNT);
  set_fx(next);
  if (mood == MOOD_NONE) {
    pal_idx = (int)(fx_rand(&fx) % (uint32_t)PAL_COUNT);
    apply_palette();
  }
  burst(220);
}

static void set_brightness(int level) {
  if (level < 13) level = 13;
  if (level > 255) level = 255;
  brightness = (uint8_t)level;
  if (mode != MODE_SLEEP) display_set_brightness(brightness);
}

static void open_menu(uint32_t now) {
  mode = MODE_MENU;
  menu_seen = true;
  menu_idle_until = now + 15000;
  menu_highlight = -1;
  clear_hint();
  overlay.until = 0;
  fx.focus_on = 0;
}

static void close_menu() {
  mode = MODE_RUN;
  menu_highlight = -1;
  hint_after_menu();
}

static void go_sleep() {
  mode = MODE_SLEEP;
  menu_highlight = -1;
  clear_hint();
  overlay.until = 0;
  display_wait_all(200);
  display_set_brightness(0);
  Serial.println("[nano] sleep");
}

static void wake_up(uint32_t now) {
  mode = MODE_RUN;
  display_set_brightness(brightness);
  burst(150);
  show_hint("TAP: MENU", NULL, NULL, 5000);
  next_auto = now + auto_interval_ms;
  Serial.println("[nano] wake");
}

static void print_up() {
  Serial.printf("{\"status\":\"ok\",\"service\":\"noisedeck-nano\",\"version\":\"%s\",\"mode\":\"%s\",\"fx\":\"%s\","
                "\"palette\":\"%s\",\"mood\":\"%s\",\"auto\":%s,\"auto_interval_s\":%u,\"speed_pct\":%d,"
                "\"brightness\":%u,\"fps\":%.1f,\"uptime_s\":%u,\"heap_free\":%u,\"display\":%s,\"touch\":%s,"
                "\"imu\":%s}\n",
                NANO_VERSION, mode_name(), fx_name(fx.fx), palette_name(), mood_name(), auto_shuffle ? "true" : "false",
                (unsigned)(auto_interval_ms / 1000), base_speed, brightness, fps, (unsigned)(millis() / 1000),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT), display_ok ? "true" : "false",
                touch_ok ? "true" : "false", imu_ok ? "true" : "false");
}

static void print_status() {
  Serial.printf("[nano] fps=%.1f mode=%s fx=%s pal=%s mood=%s auto=%d bright=%u heap=%u\n", fps, mode_name(),
                fx_name(fx.fx), palette_name(), mood_name(), auto_shuffle ? 1 : 0, brightness,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
}

static void handle_command(char *line) {
  uint32_t now = millis();
  while (*line == ' ') line++;
  char *arg = strchr(line, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
  for (char *p = line; *p; p++) *p = (char)tolower((unsigned char)*p);

  if (!strcmp(line, "up")) { print_up(); return; }
  if (!strcmp(line, "help")) {
    Serial.println("commands: up | help | fx <name|n|next|prev|rand> | pal <name|n|next|prev|rand> | mood ok|warn|crit|off | "
                   "auto on|off|<seconds> | bright <0-255> | speed <10-400> | text <message> | menu | sleep | wake | shuffle");
    Serial.printf("effects:");
    for (int i = 0; i < FX_COUNT; i++) Serial.printf(" %s", fx_name(i));
    Serial.printf("\npalettes:");
    for (int i = 0; i < PAL_COUNT; i++) Serial.printf(" %s", PALETTES[i].name);
    Serial.println();
    return;
  }
  if (!strcmp(line, "shuffle")) { shuffle_all(); announce_state(); Serial.println("ok shuffle"); return; }
  if (!strcmp(line, "menu")) {
    if (mode == MODE_MENU) close_menu(); else { if (mode == MODE_SLEEP) wake_up(now); open_menu(now); }
    Serial.printf("ok mode %s\n", mode_name());
    return;
  }
  if (!strcmp(line, "sleep")) { if (mode != MODE_SLEEP) go_sleep(); Serial.println("ok sleep"); return; }
  if (!strcmp(line, "wake")) { if (mode == MODE_SLEEP) wake_up(now); Serial.println("ok wake"); return; }
  if (!strcmp(line, "fx") && arg) {
    if (!strcmp(arg, "next")) set_fx(fx.fx + 1);
    else if (!strcmp(arg, "prev")) set_fx(fx.fx - 1);
    else if (!strcmp(arg, "rand")) { int n = fx.fx; while (n == fx.fx) n = (int)(fx_rand(&fx) % FX_COUNT); set_fx(n); }
    else if (isdigit((unsigned char)arg[0])) set_fx(atoi(arg));
    else { int id = fx_by_name(arg); if (id < 0) { Serial.println("err unknown fx"); return; } set_fx(id); }
    announce_state();
    Serial.printf("ok fx %s\n", fx_name(fx.fx));
    return;
  }
  if (!strcmp(line, "pal") && arg) {
    if (!strcmp(arg, "rand")) { int n = pal_idx; while (n == pal_idx && PAL_COUNT > 1) n = (int)(fx_rand(&fx) % PAL_COUNT); step_palette(n - pal_idx); }
    else if (!strcmp(arg, "next")) step_palette(1);
    else if (!strcmp(arg, "prev")) step_palette(-1);
    else {
      int id = isdigit((unsigned char)arg[0]) ? atoi(arg) % PAL_COUNT : palette_by_name(arg);
      if (id < 0) { Serial.println("err unknown palette"); return; }
      step_palette(id - pal_idx);
    }
    announce_state();
    Serial.printf("ok pal %s\n", palette_name());
    return;
  }
  if (!strcmp(line, "mood") && arg) {
    if (!strcmp(arg, "ok")) mood = MOOD_OK;
    else if (!strcmp(arg, "warn")) mood = MOOD_WARN;
    else if (!strcmp(arg, "crit")) mood = MOOD_CRIT;
    else if (!strcmp(arg, "off") || !strcmp(arg, "none")) mood = MOOD_NONE;
    else { Serial.println("err mood ok|warn|crit|off"); return; }
    apply_palette();
    apply_speed();
    burst(200);
    if (mood != MOOD_NONE) say(mood_name(), 1500, 4, COL_WHITE);
    Serial.printf("ok mood %s\n", mood_name());
    return;
  }
  if (!strcmp(line, "auto") && arg) {
    if (!strcmp(arg, "on")) auto_shuffle = true;
    else if (!strcmp(arg, "off")) auto_shuffle = false;
    else if (isdigit((unsigned char)arg[0])) { auto_shuffle = true; int secs = atoi(arg); if (secs < 5) secs = 5; auto_interval_ms = (uint32_t)secs * 1000u; }
    else { Serial.println("err auto on|off|<seconds>"); return; }
    next_auto = now + auto_interval_ms;
    Serial.printf("ok auto %s %us\n", auto_shuffle ? "on" : "off", (unsigned)(auto_interval_ms / 1000));
    return;
  }
  if (!strcmp(line, "bright") && arg) { set_brightness(atoi(arg)); Serial.printf("ok bright %u\n", brightness); return; }
  if (!strcmp(line, "speed") && arg) {
    base_speed = constrain(atoi(arg), 10, 400);
    apply_speed();
    Serial.printf("ok speed %d\n", base_speed);
    return;
  }
  if (!strcmp(line, "text") && arg) { say(arg, 4000, 3, COL_WHITE); Serial.println("ok text"); return; }
  if (*line) Serial.printf("err unknown command '%s' (try help)\n", line);
}

static void poll_serial() {
  static char buf[96];
  static size_t n = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      buf[n] = 0;
      if (n) handle_command(buf);
      n = 0;
    } else if (n < sizeof(buf) - 1) {
      buf[n++] = c;
    }
  }
}

static void menu_action(int row, int action, uint32_t now) {
  menu_idle_until = now + 15000;
  const char *what = "";
  switch (row) {
  case UI_ROW_EFFECT: set_fx(fx.fx + (action == UI_ACT_PREV ? -1 : 1)); what = fx_name(fx.fx); break;
  case UI_ROW_PALETTE: step_palette(action == UI_ACT_PREV ? -1 : 1); what = palette_name(); break;
  case UI_ROW_BRIGHT: set_brightness(brightness + (action == UI_ACT_PREV ? -25 : 25)); what = "brightness"; break;
  case UI_ROW_AUTO: auto_shuffle = !auto_shuffle; next_auto = now + auto_interval_ms; what = auto_shuffle ? "auto on" : "auto off"; break;
  case UI_ROW_SHUFFLE: shuffle_all(); what = "shuffle"; break;
  case UI_ROW_SLEEP: go_sleep(); what = "sleep"; break;
  case UI_ROW_CLOSE: close_menu(); announce_state(); what = "close"; break;
  default: break;
  }
  Serial.printf("[nano] menu row %d %s -> %s (fx=%s pal=%s bright=%d%%)\n", row,
                action == UI_ACT_PREV ? "prev" : (action == UI_ACT_NEXT ? "next" : "select"), what, fx_name(fx.fx),
                palette_name(), bright_pct());
}

static void gesture_log(const char *gesture, uint32_t dt) {
  Serial.printf("[nano] touch %s from (%d,%d) to (%d,%d) in %ums -> mode=%s fx=%s pal=%s\n", gesture, tp.x0, tp.y0, tp.x,
                tp.y, (unsigned)dt, mode_name(), fx_name(fx.fx), palette_name());
}

static void poll_touch(uint32_t now) {
  if (!touch_ok) return;
  uint16_t x = 0, y = 0;
  bool pressed = touch.read(&x, &y);

  if (pressed && !tp.down) {
    tp.down = true;
    tp.t0 = now;
    tp.x0 = tp.x = x;
    tp.y0 = tp.y = y;
    tp.moved_early = tp.steer = tp.swallow = false;
    tp.press_row = -1;
    if (mode == MODE_SLEEP) {
      wake_up(now);
      tp.swallow = true;
    } else if (mode == MODE_MENU) {
      int action;
      tp.press_row = ui_menu_hit(x, y, &action);
      menu_highlight = tp.press_row;
      menu_idle_until = now + 15000;
    }
    return;
  }

  if (pressed) {
    tp.x = x;
    tp.y = y;
    if (tp.swallow || mode != MODE_RUN) return;
    int dx = tp.x - tp.x0, dy = tp.y - tp.y0;
    bool far = dx * dx + dy * dy > 24 * 24;
    if (!tp.moved_early && !tp.steer) {
      if (far && now - tp.t0 <= 300) tp.moved_early = true;
      else if (now - tp.t0 > 300) {
        tp.steer = true;
        say("STEER", 800, 3, COL_CYAN);
      }
    }
    if (tp.steer) {
      fx.focus_on = 1;
      fx.focus_x = x;
      fx.focus_y = y;
    }
    return;
  }

  if (!pressed && tp.down) {
    tp.down = false;
    uint32_t dt = now - tp.t0;
    int dx = tp.x - tp.x0, dy = tp.y - tp.y0;
    if (tp.swallow) { gesture_log("wake", dt); return; }

    if (mode == MODE_MENU) {
      int action;
      int row = ui_menu_hit(tp.x, tp.y, &action);
      menu_highlight = -1;
      if (action == UI_ACT_OUTSIDE) { close_menu(); announce_state(); gesture_log("menu-outside-close", dt); }
      else if (row >= 0 && row == tp.press_row) { menu_action(row, action, now); gesture_log("menu-tap", dt); }
      else gesture_log("menu-miss", dt);
      return;
    }

    bool small = dx * dx + dy * dy <= 24 * 24;
    if (tp.steer) {
      focus_until = now + 4000;
      gesture_log("steer", dt);
    } else if (small && dt < 600) {
      open_menu(now);
      gesture_log("tap -> menu", dt);
    } else if (abs(dx) >= 90 && abs(dx) * 2 >= abs(dy) * 3) {
      set_fx(fx.fx + (dx < 0 ? 1 : -1));
      announce_state();
      gesture_log(dx < 0 ? "swipe-left -> next effect" : "swipe-right -> prev effect", dt);
    } else if (abs(dy) >= 90 && abs(dy) * 2 >= abs(dx) * 3) {
      step_palette(dy < 0 ? 1 : -1);
      announce_state();
      gesture_log(dy < 0 ? "swipe-up -> next palette" : "swipe-down -> prev palette", dt);
    } else {
      show_hint("TAP: MENU", "SWIPE: CHANGE   HOLD: STEER", NULL, 3000);
      gesture_log("unclear", dt);
    }
  }

  if (!tp.down && fx.focus_on && focus_until && now > focus_until) {
    fx.focus_on = 0;
    focus_until = 0;
  }
}

static void poll_button(uint32_t now) {
  bool level = digitalRead(BOARD_BUTTON);
  if (level != btn_last && now - btn_t > 40) {
    btn_t = now;
    btn_last = level;
    if (!level) {
      if (mode == MODE_SLEEP) { wake_up(now); Serial.println("[nano] button wake"); return; }
      if (mode == MODE_MENU) close_menu();
      shuffle_all();
      announce_state();
      Serial.println("[nano] button shuffle");
    }
  }
}

static void poll_imu(uint32_t now) {
  if (!imu_ok || now < imu_next) return;
  imu_next = now + 20;
  int16_t rx, ry, rz;
  if (!imu.read_accel(&rx, &ry, &rz)) return;
  int ax = rx >> 4, ay = ry >> 4, az = rz >> 4;
  int32_t mag2 = ax * ax + ay * ay + az * az;   // 256 LSB per g, so 1g = 65536
  bool hit = mag2 > 262144 || mag2 < 5898;       // > 2g or < 0.3g
  if (hit) {
    if (shake_hits == 0 || now - shake_first > 400) { shake_hits = 0; shake_first = now; }
    shake_hits++;
    if (shake_hits >= 2 && now > shake_cooldown) {
      shake_cooldown = now + 1200;
      shake_hits = 0;
      if (mode == MODE_SLEEP) { wake_up(now); Serial.println("[nano] shake wake"); return; }
      if (mode == MODE_MENU) close_menu();
      shuffle_all();
      announce_state();
      Serial.println("[nano] shake shuffle");
    }
  }
}

static void draw_overlay(uint16_t *buf, int y0, uint32_t now) {
  if (now >= overlay.until || !overlay.text[0]) return;
  int scale = overlay.scale, spacing = scale + 1;
  int w = text_width(overlay.text, scale, spacing);
  int x = (FX_W - w) / 2, y = (FX_H - TEXT_GLYPH_H * scale) / 2;
  text_draw_band(buf, FX_W, y0, FX_BAND_H, overlay.text, x + scale, y + scale, scale, spacing, COL_BLACK);
  text_draw_band(buf, FX_W, y0, FX_BAND_H, overlay.text, x, y, scale, spacing, overlay.color);
}

static void draw_splash_band(uint16_t *buf, int y0) {
  memset(buf, 0, FX_W * FX_BAND_H * 2);
  int w1 = text_width("NOISEDECK NANO", 4, 6);
  text_draw_band(buf, FX_W, y0, FX_BAND_H, "NOISEDECK NANO", (FX_W - w1) / 2, 196, 4, 6, COL_WHITE);
  int w2 = text_width("by noisefactor.io", 2, 3);
  text_draw_band(buf, FX_W, y0, FX_BAND_H, "by noisefactor.io", (FX_W - w2) / 2, 244, 2, 3, COL_CYAN);
  int w3 = text_width("V" NANO_VERSION, 2, 3);
  text_draw_band(buf, FX_W, y0, FX_BAND_H, "V" NANO_VERSION, (FX_W - w3) / 2, 268, 2, 3, rgb565_be(90, 90, 110));
}

static void render_frame(uint32_t now) {
  fx_frame_begin(&fx, now);
  bool splash = now < splash_until;
  bool in_static = now < static_until;
  bool menu = mode == MODE_MENU;
  ui_model_t model = {fx_name(fx.fx), palette_name(), bright_pct(), auto_shuffle ? 1 : 0, mood != MOOD_NONE ? 1 : 0};
  for (int b = 0; b < FX_BANDS; b++) {
    int slot = b & 1;
    uint16_t *buf = display_band_buffer(slot);
    if (!display_acquire(slot, 200)) continue;
    int y0 = b * FX_BAND_H;
    if (splash) draw_splash_band(buf, y0);
    else if (in_static) fx_render_static_band(&fx, y0, buf, menu ? pal_dim : pal);
    else fx_render_band(&fx, y0, buf, menu ? pal_dim : pal, menu ? pal32_dim : pal32);
    if (menu) ui_menu_draw_band(buf, FX_W, y0, FX_BAND_H, &model, menu_highlight);
    else if (!splash) {
      draw_overlay(buf, y0, now);
      if (hint_lines[0]) ui_hint_draw_band(buf, FX_W, y0, FX_BAND_H, hint_lines);
    }
    display_push(y0, FX_BAND_H, buf);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.printf("\n[nano] noisedeck-nano %s booting, chip %s rev %d, cpu %u MHz\n", NANO_VERSION, ESP.getChipModel(),
                ESP.getChipRevision(), (unsigned)ESP.getCpuFreqMHz());

  COL_CYAN = rgb565_be(0, 229, 255);
  COL_BLACK = rgb565_be(0, 0, 0);

  bool bus_ok = bus.begin(BOARD_I2C_SCL, BOARD_I2C_SDA, BOARD_I2C_PORT);
  bool pmic_ok = bus_ok && pmic_begin(bus, BOARD_PMIC_ADDR);
  Serial.printf("[nano] i2c %s, pmic %s (AXP2101 @0x%02x)\n", bus_ok ? "ok" : "FAILED", pmic_ok ? "ok" : "FAILED", BOARD_PMIC_ADDR);

  display_ok = pmic_ok && display_init();
  Serial.printf("[nano] display %s\n", display_ok ? "ok (CO5300 480x480 QSPI)" : "FAILED");
  if (display_ok) display_set_brightness(brightness);

  touch_ok = bus_ok && touch.begin(bus, BOARD_TOUCH_ADDR, BOARD_TOUCH_RST, BOARD_LCD_W, BOARD_LCD_H);
  Serial.printf("[nano] touch %s (CST9220 @0x%02x)\n", touch_ok ? "ok" : "not responding", BOARD_TOUCH_ADDR);

  imu_ok = bus_ok && imu.begin(bus, BOARD_IMU_ADDR);
  int16_t ax = 0, ay = 0, az = 0;
  if (imu_ok) {
    delay(5);
    imu_ok = imu.read_accel(&ax, &ay, &az);
  }
  Serial.printf("[nano] imu %s (QMI8658 @0x%02x) accel mg x=%d y=%d z=%d\n", imu_ok ? "ok, shake to shuffle" : "not found",
                BOARD_IMU_ADDR, ax * 1000 / 4096, ay * 1000 / 4096, az * 1000 / 4096);

  pinMode(BOARD_BUTTON, INPUT_PULLUP);

  uint32_t seed = (uint32_t)esp_random();
  fx_init(&fx, seed);
  pal_idx = 0;
  apply_palette();
  apply_speed();
  Serial.printf("[nano] engine ready: %d effects, %d palettes, seed %08x, band %dx%d x2 DMA\n", FX_COUNT, PAL_COUNT,
                (unsigned)seed, FX_W, FX_BAND_H);

  uint32_t now = millis();
  splash_until = now + 1800;
  static_until = splash_until + 300;
  next_auto = now + auto_interval_ms;
  next_flicker = now + 3000;
  fps_t0 = now;
  next_status = now + 10000;
  show_hint("TAP: MENU", NULL, NULL, 0);
  Serial.println("[nano] type 'help' for commands");
}

void loop() {
  uint32_t now = millis();
  poll_serial();
  poll_touch(now);
  poll_button(now);
  poll_imu(now);

  if (mode == MODE_MENU && now >= menu_idle_until) {
    close_menu();
    Serial.println("[nano] menu auto-closed");
  }
  if (hint_until && now >= hint_until) clear_hint();
  if (!menu_seen && mode == MODE_RUN && !hint_lines[0] && now > splash_until) show_hint("TAP: MENU", NULL, NULL, 0);

  if (mode == MODE_RUN && auto_shuffle && now >= next_auto && now >= splash_until) {
    next_auto = now + auto_interval_ms;
    shuffle_all();
    announce_state();
    Serial.printf("[nano] auto shuffle -> fx=%s pal=%s\n", fx_name(fx.fx), palette_name());
  }
  if (mood == MOOD_CRIT && now >= next_flicker) {
    next_flicker = now + 3000 + (fx_rand(&fx) % 2000);
    burst(80);
  }

  if (mode == MODE_SLEEP) {
    delay(20);
  } else if (display_ok) {
    render_frame(now);
    frames++;
  } else {
    delay(50);
  }

  if (now - fps_t0 >= 2000) {
    fps = frames * 1000.0f / (float)(now - fps_t0);
    frames = 0;
    fps_t0 = now;
  }
  if (now >= next_status) {
    next_status = now + 10000;
    print_status();
  }
}

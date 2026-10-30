// Mengão — Flamengo-themed watchface (rubro-negro) — v1.3
// Time, date (pt-BR), weather (Open-Meteo), next match / live score (ESPN), battery.
// Styles (chosen in the settings page): Clássico listrado (default), Minimalista, Placar.
#include <pebble.h>

#define COLOR_RED    PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)
#define COLOR_ACCENT PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)

static Window *s_window;
static Layer *s_canvas;
static GFont s_anton, s_bebas, s_black_font, s_bold_font, s_label_font, s_day_font, s_mini_font;
static char s_time_buf[8];
static char s_date_buf[16];
static int s_battery = 100;
static bool s_charging = false;
static bool s_bt_connected = true;

// ---------- Settings (from the Clay page in the phone app) ----------
#define PERSIST_KEY_WEATHER  1
#define PERSIST_KEY_SETTINGS 2
#define PERSIST_KEY_MATCH    3

typedef struct {
  uint8_t fahrenheit;
  uint8_t show_weather;
  uint8_t show_match;
  uint8_t vibe_goal;
  uint8_t vibe_bt;
  uint8_t wx_interval;   // minutes
  uint8_t style;         // 0 Clássico, 1 Minimalista, 2 Placar (added in v1.3)
} Settings;

static Settings s_cfg = {.fahrenheit = 0, .show_weather = 1, .show_match = 1,
                         .vibe_goal = 1, .vibe_bt = 1, .wx_interval = 30, .style = 0};

// ---------- Weather ----------
#define WEATHER_MAX_AGE (6 * 60 * 60)     // ignore cache older than 6h
#define WEATHER_ERR_AGE (2 * 60 * 60)     // on fetch error, keep cache up to 2h

typedef struct {
  int16_t temp, tmin, tmax;
  uint16_t code;
  uint8_t is_day;
  uint8_t valid;
  int32_t ts;
} Weather;

static Weather s_weather;

typedef enum { WX_NONE, WX_CLEAR, WX_PARTLY, WX_CLOUD, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM } WxIcon;

static WxIcon wx_icon_for(uint16_t c) {
  if (c == 0) return WX_CLEAR;
  if (c == 1 || c == 2) return WX_PARTLY;
  if (c == 3) return WX_CLOUD;
  if (c == 45 || c == 48) return WX_FOG;
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return WX_RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_SNOW;
  if (c >= 95) return WX_STORM;
  return WX_CLOUD;
}

static bool weather_usable(void) {
  return s_weather.valid && (time(NULL) - s_weather.ts) < WEATHER_MAX_AGE;
}

// ---------- Match ----------
enum { M_NONE = 0, M_NEXT = 1, M_LIVE = 2, M_FINAL = 3 };
#define FINAL_SHOW_SECS (6 * 60 * 60)    // final score shown until kickoff + 6h (~4h after the end)
#define LIVE_WINDOW_SECS (3 * 60 * 60)

typedef struct {
  uint8_t mode;
  uint8_t home;          // 1 = Flamengo at home
  int8_t fla, opp;       // scores
  int32_t kickoff;       // unix time (UTC)
  int32_t id;            // ESPN event id
  char opp_abbr[6];      // e.g. "EST"
  char comp[6];          // e.g. "LIB"
  char clock[8];         // e.g. "67'" / "INT"
} Match;

static Match s_match;

static bool match_visible(void) {
  if (!s_cfg.show_match || s_match.mode == M_NONE) return false;
  if (s_match.mode == M_FINAL && time(NULL) > s_match.kickoff + FINAL_SHOW_SECS) return false;
  return true;
}

static const char *DIAS[] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"};
static const char *MESES[] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN",
                              "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};

static void update_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(s_time_buf, sizeof(s_time_buf), clock_is_24h_style() ? "%H:%M" : "%l:%M", t);
  if (s_time_buf[0] == ' ') memmove(s_time_buf, s_time_buf + 1, strlen(s_time_buf));
  snprintf(s_date_buf, sizeof(s_date_buf), "%s %d %s", DIAS[t->tm_wday], t->tm_mday, MESES[t->tm_mon]);
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// Simple stylised shield (generic shape, not the official crest):
// rectangle top that tapers to a point, filled with red/black horizontal stripes.
static int shield_half_width(int dy, int h, int hw) {
  int straight = h * 55 / 100;
  if (dy <= straight) return hw;
  return hw * (h - dy) / (h - straight);
}

static void draw_shield(GContext *ctx, GRect r) {
  int hw = r.size.w / 2;
  int cx = r.origin.x + hw;
  int h = r.size.h;
  int band = h / 5;
  if (band < 1) band = 1;
  for (int dy = 0; dy < h; dy++) {
    int w = shield_half_width(dy, h, hw);
    if (w <= 0) continue;
    GColor c = ((dy / band) % 2 == 0) ? COLOR_RED : GColorBlack;
    graphics_context_set_stroke_color(ctx, c);
    graphics_draw_line(ctx, GPoint(cx - w, r.origin.y + dy), GPoint(cx + w - 1, r.origin.y + dy));
  }
  // white outline
  GPoint pts[] = {
    {cx - hw, r.origin.y}, {cx + hw - 1, r.origin.y},
    {cx + hw - 1, r.origin.y + h * 55 / 100}, {cx, r.origin.y + h - 1},
    {cx - hw, r.origin.y + h * 55 / 100},
  };
  GPathInfo info = {.num_points = 5, .points = pts};
  GPath *path = gpath_create(&info);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 1);
  gpath_draw_outline(ctx, path);
  gpath_destroy(path);
}

// Bluetooth rune with a slash (shown only when disconnected)
static void draw_bt_off(GContext *ctx, GPoint o, int s) {
  graphics_context_set_stroke_color(ctx, COLOR_ACCENT);
  graphics_context_set_stroke_width(ctx, 2);
  int x = o.x, y = o.y, q = s / 4;
  graphics_draw_line(ctx, GPoint(x, y), GPoint(x, y + s));
  graphics_draw_line(ctx, GPoint(x, y), GPoint(x + q, y + q));
  graphics_draw_line(ctx, GPoint(x + q, y + q), GPoint(x - q, y + 3 * q));
  graphics_draw_line(ctx, GPoint(x, y + s), GPoint(x + q, y + 3 * q));
  graphics_draw_line(ctx, GPoint(x + q, y + 3 * q), GPoint(x - q, y + q));
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_line(ctx, GPoint(x - q - 2, y + s), GPoint(x + q + 2, y));
}

#define C_SUN   PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite)
#define C_MOON  PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorWhite)
#define C_CLOUD PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define C_RAIN  PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite)

static void draw_sun(GContext *ctx, GPoint c, int r) {
  graphics_context_set_stroke_color(ctx, C_SUN);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < 8; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 8;
    int32_t sn = sin_lookup(a), cs = cos_lookup(a);
    GPoint p1 = GPoint(c.x + (r + 2) * sn / TRIG_MAX_RATIO, c.y - (r + 2) * cs / TRIG_MAX_RATIO);
    GPoint p2 = GPoint(c.x + (r * 3 / 2 + 2) * sn / TRIG_MAX_RATIO, c.y - (r * 3 / 2 + 2) * cs / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, p1, p2);
  }
  graphics_context_set_fill_color(ctx, C_SUN);
  graphics_fill_circle(ctx, c, r);
}

static void draw_moon(GContext *ctx, GPoint c, int r) {
  graphics_context_set_fill_color(ctx, C_MOON);
  graphics_fill_circle(ctx, c, r);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(c.x + r * 2 / 3, c.y - r / 3), r);
}

// cloud fitted in a square box of side s at (x, y)
static void draw_cloud(GContext *ctx, int x, int y, int s, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(x + s * 30 / 100, y + s * 60 / 100), s * 22 / 100);
  graphics_fill_circle(ctx, GPoint(x + s * 55 / 100, y + s * 45 / 100), s * 28 / 100);
  graphics_fill_circle(ctx, GPoint(x + s * 77 / 100, y + s * 62 / 100), s * 20 / 100);
  graphics_fill_rect(ctx, GRect(x + s * 30 / 100, y + s * 55 / 100, s * 48 / 100, s * 27 / 100 + 1), 0, GCornerNone);
}

static void draw_weather_icon(GContext *ctx, WxIcon icon, bool day, int x, int y, int s) {
  switch (icon) {
    case WX_CLEAR:
      if (day) draw_sun(ctx, GPoint(x + s / 2, y + s / 2), s * 22 / 100);
      else draw_moon(ctx, GPoint(x + s / 2, y + s / 2), s * 36 / 100);
      break;
    case WX_PARTLY:
      if (day) draw_sun(ctx, GPoint(x + s * 36 / 100, y + s * 34 / 100), s * 16 / 100);
      else draw_moon(ctx, GPoint(x + s * 34 / 100, y + s * 32 / 100), s * 24 / 100);
      draw_cloud(ctx, x + s * 15 / 100, y + s * 18 / 100, s * 85 / 100, C_CLOUD);
      break;
    case WX_CLOUD:
      draw_cloud(ctx, x, y + s * 5 / 100, s, C_CLOUD);
      break;
    case WX_FOG: {
      graphics_context_set_stroke_color(ctx, C_CLOUD);
      graphics_context_set_stroke_width(ctx, 3);
      for (int i = 0; i < 3; i++) {
        int yy = y + s * (28 + i * 22) / 100;
        int off = (i % 2) ? s / 8 : 0;
        graphics_draw_line(ctx, GPoint(x + s / 8 + off, yy), GPoint(x + s - s / 8 - (s / 8 - off), yy));
      }
      break;
    }
    case WX_RAIN:
    case WX_SNOW:
    case WX_STORM: {
      draw_cloud(ctx, x, y - s * 12 / 100, s, C_CLOUD);
      int top = y + s * 74 / 100;
      if (icon == WX_RAIN) {
        graphics_context_set_stroke_color(ctx, C_RAIN);
        graphics_context_set_stroke_width(ctx, 2);
        for (int i = 0; i < 3; i++) {
          int xx = x + s * (32 + i * 20) / 100;
          graphics_draw_line(ctx, GPoint(xx, top), GPoint(xx - s / 10, top + s / 5));
        }
      } else if (icon == WX_SNOW) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        for (int i = 0; i < 3; i++) {
          graphics_fill_circle(ctx, GPoint(x + s * (30 + i * 20) / 100, top + ((i % 2) ? s / 7 : s / 20)), 2);
        }
      } else {
        GPoint bolt[] = {
          {x + s * 58 / 100, y + s * 50 / 100}, {x + s * 34 / 100, y + s * 80 / 100},
          {x + s * 50 / 100, y + s * 80 / 100}, {x + s * 40 / 100, y + s * 104 / 100},
          {x + s * 72 / 100, y + s * 70 / 100}, {x + s * 56 / 100, y + s * 70 / 100},
        };
        GPathInfo info = {.num_points = 6, .points = bolt};
        GPath *p = gpath_create(&info);
        graphics_context_set_fill_color(ctx, C_SUN);
        gpath_draw_filled(ctx, p);
        gpath_destroy(p);
      }
      break;
    }
    default:
      break;
  }
}

// ---------- Colours (64-colour palette; falls back to B/W on diorite) ----------
#define C_DRED      PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorWhite)   // #AA0000
#define C_MAROON    PBL_IF_COLOR_ELSE(GColorBulgarianRose, GColorBlack)       // #550000
#define C_LGRY      PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define C_DGRY      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)
#define C_AMBER     PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite)
#define C_YELLOW    PBL_IF_COLOR_ELSE(GColorIcterine, GColorWhite)

enum { STYLE_CLASSICO = 0, STYLE_MINIMAL = 1, STYLE_PLACAR = 2 };

// Layouts are designed for emery (200x228) and scaled for 144x168 screens.
static int s_w = 200, s_h = 228;
#define X(v) ((v) * s_w / 200)
#define Y(v) ((v) * s_h / 228)

static GSize text_size(const char *t, GFont f) {
  return graphics_text_layout_get_content_size(t, f, GRect(0, 0, 400, 200), GTextOverflowModeFill, GTextAlignmentLeft);
}

// Distance from the text box top to the middle of the capitals, per font (measured on the emulator).
static int cap_mid(GFont f, int box_h) {
  if (f == s_anton) return box_h * 56 / 100;
  if (f == s_bebas) return box_h * 64 / 100;
  if (f == s_mini_font) return box_h * 64 / 100;
  return box_h * 61 / 100;   // Barlow Condensed
}

// Text boxes are kept inside the screen: the basalt firmware froze when a text box
// ran past the screen edge.
static GRect safe_box(int x, int y, int w, int h) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > s_w) w = s_w - x;
  if (y + h > s_h) h = s_h - y;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  return GRect(x, y, w, h);
}

// Draws t with its capitals vertically centred on cy. align: -1 = x is left edge, 0 = centre, 1 = right edge.
// Returns the text width.
static int text_at(GContext *ctx, const char *t, GFont f, GColor c, int x, int cy, int align) {
  GSize sz = text_size(t, f);
  int left = align < 0 ? x : (align == 0 ? x - sz.w / 2 : x - sz.w);
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, t, f, safe_box(left, cy - cap_mid(f, sz.h), sz.w + 6, sz.h + 10),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  return sz.w;
}

static int temp_display(int c) {
  if (!s_cfg.fahrenheit) return c;
  int f10 = c * 18 + 320;                       // tenths of °F
  return (f10 >= 0) ? (f10 + 5) / 10 : (f10 - 5) / 10;
}

// Abstract vulture in flight (original glyph): two swept wings and a small head.
static void draw_urubu(GContext *ctx, int cx, int cy, int s, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  for (int side = -1; side <= 1; side += 2) {
    GPoint pts[] = {
      {cx, cy}, {cx + side * s, cy - s * 45 / 100}, {cx + side * s * 75 / 100, cy - s * 15 / 100},
      {cx + side * s * 110 / 100, cy - s * 5 / 100}, {cx + side * s * 30 / 100, cy + s * 20 / 100},
    };
    GPathInfo info = {.num_points = 5, .points = pts};
    GPath *p = gpath_create(&info);
    gpath_draw_filled(ctx, p);
    gpath_destroy(p);
  }
  graphics_fill_circle(ctx, GPoint(cx, cy - s * 17 / 100), s * 14 / 100 + 1);
}

static void draw_battery_c(GContext *ctx, GRect r, GColor outline) {
  graphics_context_set_stroke_color(ctx, outline);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, r);
  graphics_context_set_fill_color(ctx, outline);
  graphics_fill_rect(ctx, GRect(r.origin.x + r.size.w, r.origin.y + r.size.h / 4, 2, r.size.h - r.size.h / 2), 0, GCornerNone);
  int fill_w = (r.size.w - 4) * s_battery / 100;
  if (fill_w < 1) fill_w = 1;
  GColor level = s_battery <= 20 ? PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite) : COLOR_RED;
  if (s_charging) level = PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite);
  graphics_context_set_fill_color(ctx, level);
  graphics_fill_rect(ctx, GRect(r.origin.x + 2, r.origin.y + 2, fill_w, r.size.h - 4), 0, GCornerNone);
}

// ---------- Match text ----------
typedef struct {
  char teams[24];      // "FLA x PAL" / "FLA 2 x 1 PAL"
  char score[12];      // "2 x 1" (FLA first)
  char when[16];       // next: "DOM 16h" / "HOJE 21h30" / "25/10 16h"; live: clock; final: "FIM"
  char when_mid[16];   // next: "DOM 16h" (weekday + hour, also for tomorrow) / "25/10"
  char when_short[16]; // next: "DOM" / "16h" / "25/10"
  char ko[8];          // next: "16:00" (kick-off time, for LED)
  const char *opp;
} MatchText;

static void fmt_hour(char *buf, size_t n, struct tm *tk) {
  if (clock_is_24h_style()) {
    if (tk->tm_min == 0) snprintf(buf, n, "%dh", tk->tm_hour);
    else snprintf(buf, n, "%dh%02d", tk->tm_hour, tk->tm_min);
  } else {
    strftime(buf, n, "%l:%M", tk);
    if (buf[0] == ' ') memmove(buf, buf + 1, strlen(buf));
  }
}

static MatchText match_text(void) {
  MatchText m;
  memset(&m, 0, sizeof(m));
  m.opp = s_match.opp_abbr[0] ? s_match.opp_abbr : "???";
  bool scored = (s_match.mode == M_LIVE || s_match.mode == M_FINAL);
  snprintf(m.score, sizeof(m.score), "%d x %d", s_match.fla, s_match.opp);
  if (scored) {
    if (s_match.home) snprintf(m.teams, sizeof(m.teams), "FLA %d x %d %s", s_match.fla, s_match.opp, m.opp);
    else snprintf(m.teams, sizeof(m.teams), "%s %d x %d FLA", m.opp, s_match.opp, s_match.fla);
    snprintf(m.when, sizeof(m.when), "%s", s_match.mode == M_LIVE ? s_match.clock : "FIM");
    snprintf(m.when_short, sizeof(m.when_short), "%s", m.when);
  } else {
    if (s_match.home) snprintf(m.teams, sizeof(m.teams), "FLA x %s", m.opp);
    else snprintf(m.teams, sizeof(m.teams), "%s x FLA", m.opp);
    time_t now = time(NULL), ko = s_match.kickoff;
    struct tm tn = *localtime(&now);
    struct tm tk = *localtime(&ko);
    char hm[10];
    fmt_hour(hm, sizeof(hm), &tk);
    strftime(m.ko, sizeof(m.ko), clock_is_24h_style() ? "%H:%M" : "%l:%M", &tk);
    if (m.ko[0] == ' ') memmove(m.ko, m.ko + 1, strlen(m.ko));
    struct tm a = tn, b = tk;
    a.tm_hour = b.tm_hour = 12; a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
    int ddays = (int)((mktime(&b) - mktime(&a) + 43200) / 86400);
    if (ddays < 7) snprintf(m.when_mid, sizeof(m.when_mid), "%s %s", DIAS[tk.tm_wday], hm);
    else snprintf(m.when_mid, sizeof(m.when_mid), "%02d/%02d", tk.tm_mday, tk.tm_mon + 1);
    if (ddays <= 0) { snprintf(m.when, sizeof(m.when), "HOJE %s", hm); snprintf(m.when_short, sizeof(m.when_short), "%s", hm); }
    else if (ddays == 1) { snprintf(m.when, sizeof(m.when), "AMANHÃ %s", hm); snprintf(m.when_short, sizeof(m.when_short), "AMANHÃ"); }
    else if (ddays < 7) { snprintf(m.when, sizeof(m.when), "%s %s", DIAS[tk.tm_wday], hm); snprintf(m.when_short, sizeof(m.when_short), "%s", DIAS[tk.tm_wday]); }
    else { snprintf(m.when, sizeof(m.when), "%02d/%02d %s", tk.tm_mday, tk.tm_mon + 1, hm); snprintf(m.when_short, sizeof(m.when_short), "%02d/%02d", tk.tm_mday, tk.tm_mon + 1); }
  }
  return m;
}

static const char *comp_name(const char *c) {
  if (!strcmp(c, "BR")) return "BRASILEIRÃO";
  if (!strcmp(c, "LIB")) return "LIBERTADORES";
  if (!strcmp(c, "CDB")) return "COPA DO BRASIL";
  if (!strcmp(c, "CAR")) return "CARIOCA";
  if (!strcmp(c, "SUL")) return "SUL-AMERICANA";
  if (!strcmp(c, "REC")) return "RECOPA";
  if (!strcmp(c, "MUN")) return "MUNDIAL";
  if (!strcmp(c, "INT")) return "INTERCONTINENTAL";
  if (!strcmp(c, "SUP")) return "SUPERCOPA";
  return c[0] ? c : "FLAMENGO";
}

// ---------- Shared bottom info ----------
static void draw_weather_block(GContext *ctx, int x, int cy, int icon_s, GFont tf, bool unit, bool minmax) {
  bool ok = weather_usable();
  static char temp[12], tmax[8], tmin[8];
  if (ok) snprintf(temp, sizeof(temp), unit ? "%d°%s" : "%d°", temp_display(s_weather.temp), s_cfg.fahrenheit ? "F" : "C");
  else snprintf(temp, sizeof(temp), "--°");
  if (ok) {
    draw_weather_icon(ctx, wx_icon_for(s_weather.code), s_weather.is_day, x, cy - icon_s / 2, icon_s);
    x += icon_s + 3;
  }
  x += text_at(ctx, temp, tf, GColorWhite, x, cy, -1) + 4;
  if (ok && minmax) {
    snprintf(tmax, sizeof(tmax), "%d°", temp_display(s_weather.tmax));
    snprintf(tmin, sizeof(tmin), "%d°", temp_display(s_weather.tmin));
    int d = Y(7);
    text_at(ctx, tmax, s_mini_font, COLOR_RED, x, cy - d, -1);
    text_at(ctx, tmin, s_mini_font, PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite), x, cy + d, -1);
  }
}

// battery icon + "80%", right-aligned at x_right (or centred when centre = true). Shows BT-off rune first.
static void draw_battery_block(GContext *ctx, int x_right, int cy, GColor col, GFont f, bool centre) {
  static char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", s_battery);
  int bw = X(22), bh = Y(12) < 10 ? 10 : Y(12);
  int pw = text_size(pct, f).w;
  int bt = s_bt_connected ? 0 : 16;
  int total = bt + bw + 2 + 5 + pw;
  int x = centre ? x_right - total / 2 : x_right - total;
  if (!s_bt_connected) { draw_bt_off(ctx, GPoint(x + 5, cy - 8), 16); x += bt; }
  draw_battery_c(ctx, GRect(x, cy - bh / 2, bw, bh), col);
  text_at(ctx, pct, f, col, x + bw + 2 + 5, cy, -1);
}

// ---------- Dot-matrix LED (Placar) ----------
static const uint8_t LED_DIGITS[10][7] = {
  {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
  {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
  {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
  {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
  {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
};
static const uint8_t LED_COLON[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
static const uint8_t LED_APOS[7]  = {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00};
static const uint8_t LED_PLUS[7]  = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};

static bool led_glyph(char ch, const uint8_t **g, int *cols) {
  *cols = 5;
  if (ch >= '0' && ch <= '9') { *g = LED_DIGITS[ch - '0']; return true; }
  if (ch == ':') { *g = LED_COLON; *cols = 3; return true; }
  if (ch == '\'') { *g = LED_APOS; *cols = 3; return true; }
  if (ch == '+') { *g = LED_PLUS; return true; }
  return false;
}
static bool led_ok(const char *t) {
  const uint8_t *g; int c;
  if (!t[0]) return false;
  for (; *t; t++) if (!led_glyph(*t, &g, &c)) return false;
  return true;
}
static int led_width(const char *t, int p) {
  const uint8_t *g; int c, w = 0;
  for (; *t; t++) if (led_glyph(*t, &g, &c)) w += (c + 1) * p;
  return w ? w - p : 0;
}
// lit pixels are (p-1)x(p-1) rounded squares; unlit pixels drawn as a tiny dot if off is not clear
static void led_draw(GContext *ctx, int x, int y, const char *t, int p, GColor on, GColor off) {
  const uint8_t *g; int cols;
  int rad = p >= 4 ? 1 : 0;
  for (; *t; t++) {
    if (!led_glyph(*t, &g, &cols)) continue;
    int shift = cols == 5 ? 4 : 3;   // 3-wide glyphs use bits 3..1
    for (int j = 0; j < 7; j++) {
      for (int i = 0; i < cols; i++) {
        bool lit = (g[j] >> (shift - i)) & 1;
        int x0 = x + i * p, y0 = y + j * p;
        if (lit) {
          graphics_context_set_fill_color(ctx, on);
          graphics_fill_rect(ctx, GRect(x0, y0, p - 1, p - 1), rad, GCornersAll);
        } else if (!gcolor_equal(off, GColorClear) && p >= 4) {
          graphics_context_set_fill_color(ctx, off);
          graphics_fill_rect(ctx, GRect(x0 + p / 2 - 1, y0 + p / 2 - 1, 2, 2), 0, GCornerNone);
        }
      }
    }
    x += (cols + 1) * p;
  }
}

// Big outlined text: black halo around white glyphs (for the striped background)
static void text_outlined(GContext *ctx, const char *t, GFont f, int cx, int cy, int r) {
  GSize sz = text_size(t, f);
  int left = cx - sz.w / 2, top = cy - cap_mid(f, sz.h);
  graphics_context_set_text_color(ctx, GColorBlack);
  for (int dy = -r; dy <= r; dy += 2) {
    for (int dx = -r; dx <= r; dx += 2) {
      if (dx * dx + dy * dy > r * r + 2 || (dx == 0 && dy == 0)) continue;
      graphics_draw_text(ctx, t, f, safe_box(left + dx, top + dy, sz.w + 6, sz.h + 10),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    }
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, t, f, safe_box(left, top, sz.w + 6, sz.h + 10), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// ======================= 1. CLÁSSICO LISTRADO =======================
static void draw_classico(GContext *ctx, bool match) {
  int w = s_w, h = s_h;
  int stripes = 10;
  for (int i = 0; i < stripes; i++) {
    int y0 = h * i / stripes, y1 = h * (i + 1) / stripes;
    graphics_context_set_fill_color(ctx, (i % 2 == 0) ? COLOR_RED : GColorBlack);
    graphics_fill_rect(ctx, GRect(0, y0, w, y1 - y0), 0, GCornerNone);
  }
  // top bar: shield + MENGÃO + date
  int top_h = Y(34);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, w, top_h), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, top_h, w, 2), 0, GCornerNone);
  int sh = top_h * 70 / 100, sw = sh * 80 / 100;
  draw_shield(ctx, GRect(X(9), (top_h - sh) / 2, sw, sh));
  text_at(ctx, "MENGÃO", s_black_font, GColorWhite, X(9) + sw + 6, top_h / 2, -1);
  text_at(ctx, s_date_buf, s_bold_font, COLOR_RED, w - X(8), top_h / 2, 1);

  // bottom bar: weather + battery
  int bar_y = Y(192);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, bar_y, w, h - bar_y), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, bar_y, w, 2), 0, GCornerNone);
  int bar_cy = bar_y + (h - bar_y) / 2 + 1;
  if (s_cfg.show_weather) {
    draw_weather_block(ctx, X(7), bar_cy, Y(26), s_label_font, false, true);
    draw_battery_block(ctx, w - X(8), bar_cy, GColorWhite, s_bold_font, false);
  } else {
    draw_battery_block(ctx, w / 2, bar_cy, GColorWhite, s_bold_font, true);
  }

  // time
  int time_cy = match ? Y(92) : (top_h + bar_y) / 2;
  text_outlined(ctx, s_time_buf, s_anton, w / 2, time_cy, X(4) < 3 ? 3 : X(4));

  if (!match) return;
  // game card
  MatchText m = match_text();
  bool live = s_match.mode == M_LIVE, fin = s_match.mode == M_FINAL;
  GRect card = GRect(X(8), Y(146), w - 2 * X(8), Y(36));
  int ccy = card.origin.y + card.size.h / 2;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, card, 6, GCornersAll);
  // left tag: competition (next), live clock (live) or FIM
  const char *tag = live ? s_match.clock : (fin ? "FIM" : s_match.comp);
  int tag_w = text_size(tag, s_bold_font).w + X(12) + (live ? 8 : 0);
  if (tag_w < X(32)) tag_w = X(32);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(card.origin.x, card.origin.y, tag_w, card.size.h), 6, GCornersLeft);
  int tx = card.origin.x + tag_w / 2;
  if (live) {
    int tw = text_size(tag, s_bold_font).w + 8;
    graphics_context_set_fill_color(ctx, COLOR_RED);
    graphics_fill_circle(ctx, GPoint(tx - tw / 2 + 2, ccy), 3);
    text_at(ctx, tag, s_bold_font, COLOR_RED, tx - tw / 2 + 8, ccy, -1);
  } else {
    text_at(ctx, tag, s_bold_font, fin ? GColorWhite : COLOR_RED, tx, ccy, 0);
  }
  // teams + when
  int ax = card.origin.x + tag_w + X(6), ar = card.origin.x + card.size.w - X(6);
  int avail = ar - ax;
  GColor ink = PBL_IF_COLOR_ELSE(GColorBlack, GColorBlack);
  if (live || fin) {
    int tw = text_size(m.teams, s_black_font).w;
    text_at(ctx, m.teams, (tw <= avail) ? s_black_font : s_bold_font, ink, ax + avail / 2, ccy, 0);
  } else {
    int tw = text_size(m.teams, s_black_font).w;
    const char *wh = m.when;
    if (tw + 6 + text_size(wh, s_bold_font).w > avail) wh = m.when_mid;
    if (tw + 6 + text_size(wh, s_bold_font).w > avail) wh = m.when_short;
    text_at(ctx, m.teams, s_black_font, ink, ax, ccy, -1);
    text_at(ctx, wh, s_bold_font, C_DRED, ar, ccy, 1);
  }
}

// ======================= 2. MINIMALISTA =======================
static void draw_minimal(GContext *ctx, bool match) {
  int w = s_w;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, w, s_h), 0, GCornerNone);
  // hours / minutes stacked
  char hh[4], mm[4];
  const char *colon = strchr(s_time_buf, ':');
  int hl = colon ? (int)(colon - s_time_buf) : 2;
  snprintf(hh, sizeof(hh), "%.*s", hl, s_time_buf);
  snprintf(mm, sizeof(mm), "%s", colon ? colon + 1 : "");
  int col_l = X(4), col_r = X(118);
  int cx = (col_l + col_r) / 2;
  text_at(ctx, hh, s_bebas, GColorWhite, cx, Y(62), 0);
  text_at(ctx, mm, s_bebas, COLOR_RED, cx, Y(164), 0);
  // thin red rule
  graphics_context_set_fill_color(ctx, COLOR_RED);
  graphics_fill_rect(ctx, GRect(col_r + X(2), Y(18), 2, Y(192)), 0, GCornerNone);

  // right column
  int x = col_r + X(12), xr = w - X(6);
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char day[4];
  snprintf(day, sizeof(day), "%d", t->tm_mday);
  int y = Y(22);
  text_at(ctx, DIAS[t->tm_wday], s_label_font, C_LGRY, x, y, -1);
  text_at(ctx, day, s_day_font, GColorWhite, x, y + Y(26), -1);
  text_at(ctx, MESES[t->tm_mon], s_label_font, C_LGRY, x, y + Y(52), -1);
  y += Y(70);
  graphics_context_set_fill_color(ctx, C_DGRY);
  graphics_fill_rect(ctx, GRect(x, y, xr - x, 1), 0, GCornerNone);
  if (s_cfg.show_weather) {
    draw_weather_block(ctx, x - 2, y + Y(19), Y(22), s_label_font, false, false);
    y += Y(38);
    graphics_fill_rect(ctx, GRect(x, y, xr - x, 1), 0, GCornerNone);
  }
  if (match) {
    MatchText m = match_text();
    bool scored = s_match.mode == M_LIVE || s_match.mode == M_FINAL;
    draw_urubu(ctx, x + X(10), y + Y(17), X(10), COLOR_RED);
    text_at(ctx, scored ? m.score : m.opp, s_black_font, GColorWhite, x + X(24), y + Y(16), -1);
    static char l2[20];
    if (scored) snprintf(l2, sizeof(l2), "%s %s", m.opp, m.when);
    else snprintf(l2, sizeof(l2), "%s", m.when);
    if (text_size(l2, s_bold_font).w > xr - x) {
      if (scored) snprintf(l2, sizeof(l2), "%s", m.when); else snprintf(l2, sizeof(l2), "%s", m.when_mid);
    }
    if (text_size(l2, s_bold_font).w > xr - x && !scored) snprintf(l2, sizeof(l2), "%s", m.when_short);
    if (s_match.mode == M_LIVE) {
      graphics_context_set_fill_color(ctx, COLOR_RED);
    }
    text_at(ctx, l2, s_bold_font, COLOR_RED, x, y + Y(37), -1);
    y += Y(54);
    graphics_context_set_fill_color(ctx, C_DGRY);
    graphics_fill_rect(ctx, GRect(x, y, xr - x, 1), 0, GCornerNone);
  }
  // battery at the bottom of the column
  int by = Y(201);
  if (!s_bt_connected) draw_bt_off(ctx, GPoint(x + 4, by - Y(30)), 16);
  draw_battery_c(ctx, GRect(x, by - Y(5), X(20), Y(10) < 8 ? 8 : Y(10)), C_LGRY);
  static char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", s_battery);
  text_at(ctx, pct, s_bold_font, C_LGRY, x + X(27), by, -1);
}

// ======================= 3. PLACAR / ESTÁDIO =======================
static void draw_placar(GContext *ctx, bool match) {
  int w = s_w, h = s_h;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, w, h), 0, GCornerNone);
  bool big = w >= 180;
  GRect panel = GRect(X(4), Y(60), w - 2 * X(4), Y(126));
  int bar_cy = Y(209);

  if (match) {
    // top: amber LED clock + date
    int p = big ? 5 : 3;
    led_draw(ctx, (w - led_width(s_time_buf, p)) / 2, Y(6), s_time_buf, p, C_AMBER, GColorClear);
    text_at(ctx, s_date_buf, s_bold_font, C_LGRY, w / 2, Y(50), 0);

    MatchText m = match_text();
    bool live = s_match.mode == M_LIVE, fin = s_match.mode == M_FINAL;
    // panel frame + header
    graphics_context_set_stroke_color(ctx, C_DGRY);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_round_rect(ctx, panel, 4);
    int hh = Y(17);
    graphics_context_set_fill_color(ctx, C_DRED);
    graphics_fill_rect(ctx, GRect(panel.origin.x, panel.origin.y, panel.size.w, hh), 4, GCornersTop);
    text_at(ctx, comp_name(s_match.comp), s_bold_font, GColorWhite, w / 2, panel.origin.y + hh / 2 + 1, 0);
    // team tags: home team on the left
    int tag_w = X(78), tag_h = Y(20), ty = Y(82);
    int lx = X(12), rx = w - X(12) - tag_w;
    int fla_x = s_match.home ? lx : rx, opp_x = s_match.home ? rx : lx;
    graphics_context_set_fill_color(ctx, COLOR_RED);
    graphics_fill_rect(ctx, GRect(fla_x, ty, tag_w, tag_h), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(opp_x, ty, tag_w, tag_h), 0, GCornerNone);
    text_at(ctx, "FLA", s_black_font, GColorWhite, fla_x + tag_w / 2, ty + tag_h / 2, 0);
    text_at(ctx, m.opp, s_black_font, GColorBlack, opp_x + tag_w / 2, ty + tag_h / 2, 0);

    int sy = Y(106);
    int lcx = lx + tag_w / 2, rcx = rx + tag_w / 2;
    int row_cy = Y(171);
    if (live || fin) {
      int p2 = big ? 6 : 4;
      char a[4], b[4];
      int ls = s_match.home ? s_match.fla : s_match.opp, rs = s_match.home ? s_match.opp : s_match.fla;
      snprintf(a, sizeof(a), "%d", ls > 99 ? 99 : ls);
      snprintf(b, sizeof(b), "%d", rs > 99 ? 99 : rs);
      led_draw(ctx, lcx - led_width(a, p2) / 2, sy, a, p2, COLOR_RED, C_MAROON);
      led_draw(ctx, rcx - led_width(b, p2) / 2, sy, b, p2, COLOR_RED, C_MAROON);
      graphics_context_set_fill_color(ctx, C_LGRY);
      graphics_fill_rect(ctx, GRect(w / 2 - X(6), sy + 7 * p2 / 2 - 2, X(12), 4), 0, GCornerNone);
      if (live) {
        int p3 = big ? 3 : 2;
        bool led_clock = led_ok(s_match.clock);
        int cw = led_clock ? led_width(s_match.clock, p3) : text_size(s_match.clock, s_black_font).w;
        int lw = text_size("AO VIVO", s_bold_font).w;
        int total = 13 + lw + X(14) + cw;
        int x = (w - total) / 2;
        graphics_context_set_fill_color(ctx, COLOR_RED);
        graphics_fill_circle(ctx, GPoint(x + 4, row_cy), 4);
        text_at(ctx, "AO VIVO", s_bold_font, GColorWhite, x + 13, row_cy, -1);
        x += 13 + lw + X(14);
        if (led_clock) led_draw(ctx, x, row_cy - 7 * p3 / 2, s_match.clock, p3, C_YELLOW, GColorClear);
        else text_at(ctx, s_match.clock, s_black_font, C_YELLOW, x, row_cy, -1);
      } else {
        text_at(ctx, "ENCERRADO", s_bold_font, C_LGRY, w / 2, row_cy, 0);
      }
    } else {
      // next game: kick-off time in LED + day
      int p2 = big ? 6 : 4;
      led_draw(ctx, (w - led_width(m.ko, p2)) / 2, sy, m.ko, p2, COLOR_RED, GColorClear);
      static char line[24];
      time_t ko = s_match.kickoff;
      struct tm tk = *localtime(&ko);
      const char *wd = m.when_short;
      if (!strncmp(m.when, "HOJE", 4)) wd = "HOJE";
      snprintf(line, sizeof(line), "PRÓXIMO JOGO - %s", wd);
      if (strchr(wd, '/') == NULL && strcmp(wd, "HOJE") && strcmp(wd, "AMANHÃ"))
        snprintf(line, sizeof(line), "PRÓXIMO JOGO - %s %02d/%02d", wd, tk.tm_mday, tk.tm_mon + 1);
      if (text_size(line, s_bold_font).w > panel.size.w - 8) snprintf(line, sizeof(line), "%s %02d/%02d", DIAS[tk.tm_wday], tk.tm_mday, tk.tm_mon + 1);
      text_at(ctx, line, s_bold_font, GColorWhite, w / 2, row_cy, 0);
    }
  } else {
    // no game: big LED clock in the panel
    int p = big ? 6 : 4;
    int lw = led_width(s_time_buf, p);
    text_at(ctx, "MENGÃO", s_black_font, COLOR_RED, w / 2, Y(30), 0);
    led_draw(ctx, (w - lw) / 2, panel.origin.y + Y(20), s_time_buf, p, C_AMBER, GColorClear);
    text_at(ctx, s_date_buf, s_label_font, C_LGRY, w / 2, panel.origin.y + Y(20) + 7 * p + Y(22), 0);
  }

  // bottom row: weather (with unit) + battery
  if (s_cfg.show_weather) {
    draw_weather_block(ctx, X(8), bar_cy, Y(28), s_black_font, true, false);
    draw_battery_block(ctx, w - X(8), bar_cy, GColorWhite, s_bold_font, false);
  } else {
    draw_battery_block(ctx, w / 2, bar_cy, GColorWhite, s_bold_font, true);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  s_w = b.size.w; s_h = b.size.h;
  bool match = match_visible();
  switch (s_cfg.style) {
    case STYLE_MINIMAL: draw_minimal(ctx, match); break;
    case STYLE_PLACAR:  draw_placar(ctx, match); break;
    default:            draw_classico(ctx, match); break;
  }
}

// ---------- Phone communication ----------
static int s_min_since_wx = 0, s_min_since_match = 0;

static int32_t tuple_int(Tuple *t) {
  if (!t) return 0;
  switch (t->type) {
    case TUPLE_CSTRING: return atoi(t->value->cstring);
    case TUPLE_INT:
      return t->length == 1 ? t->value->int8 : t->length == 2 ? t->value->int16 : t->value->int32;
    case TUPLE_UINT:
      return t->length == 1 ? t->value->uint8 : t->length == 2 ? t->value->uint16 : (int32_t)t->value->uint32;
    default: return 0;
  }
}

static void copy_str(char *dst, size_t n, Tuple *t) {
  if (t && t->type == TUPLE_CSTRING) { strncpy(dst, t->value->cstring, n - 1); dst[n - 1] = 0; }
}

static void request_data(uint8_t what) {   // bit 1 = weather, bit 2 = match
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) == APP_MSG_OK) {
    dict_write_uint8(it, MESSAGE_KEY_REQUEST, what);
    app_message_outbox_send();
    if (what & 1) s_min_since_wx = 0;
    if (what & 2) s_min_since_match = 0;
  }
}

static int match_poll_minutes(void) {
  time_t now = time(NULL);
  switch (s_match.mode) {
    case M_LIVE: return 1;
    case M_NEXT:
      if (now >= s_match.kickoff - 120 && now < s_match.kickoff + LIVE_WINDOW_SECS) return 1;
      if (now >= s_match.kickoff - 3600 && now < s_match.kickoff) return 10;
      return 120;
    case M_FINAL:
      return (now > s_match.kickoff + FINAL_SHOW_SECS) ? 1 : 60;
    default: return 120;
  }
}

static void tick_handler(struct tm *t, TimeUnits changed) {
  update_time();
  s_min_since_wx++;
  s_min_since_match++;
  uint8_t what = 0;
  if (s_cfg.show_weather && s_min_since_wx >= s_cfg.wx_interval) what |= 1;
  if (s_cfg.show_match && s_min_since_match >= match_poll_minutes()) what |= 2;
  if (what) request_data(what);
}

static void apply_match(DictionaryIterator *it) {
  /* M_ERR: phone failed; keep the cached match (do not clear). */
  if (dict_find(it, MESSAGE_KEY_M_ERR)) {
    APP_LOG(APP_LOG_LEVEL_INFO, "match fetch error — keeping cache");
    return;
  }
  Tuple *mode = dict_find(it, MESSAGE_KEY_M_MODE);
  if (!mode) return;
  Match m = s_match;
  int32_t id = tuple_int(dict_find(it, MESSAGE_KEY_M_ID));
  bool same_event = (id == s_match.id);
  m.mode = tuple_int(mode);
  m.id = id;
  m.home = tuple_int(dict_find(it, MESSAGE_KEY_M_HOME));
  m.kickoff = tuple_int(dict_find(it, MESSAGE_KEY_M_TIME));
  m.fla = tuple_int(dict_find(it, MESSAGE_KEY_M_FLA));
  m.opp = tuple_int(dict_find(it, MESSAGE_KEY_M_OPP));
  m.opp_abbr[0] = m.comp[0] = m.clock[0] = 0;
  copy_str(m.opp_abbr, sizeof(m.opp_abbr), dict_find(it, MESSAGE_KEY_M_OPP_NAME));
  copy_str(m.comp, sizeof(m.comp), dict_find(it, MESSAGE_KEY_M_COMP));
  copy_str(m.clock, sizeof(m.clock), dict_find(it, MESSAGE_KEY_M_CLOCK));
  // Goal alert: same match, live, Flamengo's score went up
  if (s_cfg.vibe_goal && same_event && m.mode == M_LIVE &&
      (s_match.mode == M_LIVE || s_match.mode == M_NEXT) && m.fla > s_match.fla) {
    static const uint32_t segs[] = {300, 150, 300, 150, 600};
    VibePattern pat = {.durations = segs, .num_segments = ARRAY_LENGTH(segs)};
    vibes_enqueue_custom_pattern(pat);
  }
  s_match = m;
  persist_write_data(PERSIST_KEY_MATCH, &s_match, sizeof(s_match));
}

static void apply_settings(DictionaryIterator *it) {
  bool changed = false;
  Tuple *t;
  if ((t = dict_find(it, MESSAGE_KEY_CFG_FAHRENHEIT))) { s_cfg.fahrenheit = tuple_int(t) ? 1 : 0; changed = true; }
  if ((t = dict_find(it, MESSAGE_KEY_CFG_SHOW_WEATHER))) { s_cfg.show_weather = tuple_int(t) ? 1 : 0; changed = true; }
  if ((t = dict_find(it, MESSAGE_KEY_CFG_SHOW_MATCH))) { s_cfg.show_match = tuple_int(t) ? 1 : 0; changed = true; }
  if ((t = dict_find(it, MESSAGE_KEY_CFG_VIBE_GOAL))) { s_cfg.vibe_goal = tuple_int(t) ? 1 : 0; changed = true; }
  if ((t = dict_find(it, MESSAGE_KEY_CFG_VIBE_BT))) { s_cfg.vibe_bt = tuple_int(t) ? 1 : 0; changed = true; }
  if ((t = dict_find(it, MESSAGE_KEY_CFG_WX_INTERVAL))) {
    int v = tuple_int(t);
    s_cfg.wx_interval = (v >= 5 && v <= 240) ? v : 30;
    changed = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_CFG_STYLE))) {
    int v = tuple_int(t);
    s_cfg.style = (v >= 0 && v <= 2) ? v : 0;
    changed = true;
  }
  if (changed) {
    persist_write_data(PERSIST_KEY_SETTINGS, &s_cfg, sizeof(s_cfg));
    // fetch anything that was just switched on
    s_min_since_wx = s_min_since_match = 9999;
  }
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  apply_settings(it);
#ifndef PREVIEW_MOCK
  Tuple *temp = dict_find(it, MESSAGE_KEY_TEMP);
  if (temp) {
    s_weather.temp = tuple_int(temp);
    s_weather.code = tuple_int(dict_find(it, MESSAGE_KEY_CODE));
    s_weather.is_day = tuple_int(dict_find(it, MESSAGE_KEY_IS_DAY));
    s_weather.tmin = tuple_int(dict_find(it, MESSAGE_KEY_TMIN));
    s_weather.tmax = tuple_int(dict_find(it, MESSAGE_KEY_TMAX));
    s_weather.valid = 1;
    s_weather.ts = time(NULL);
    persist_write_data(PERSIST_KEY_WEATHER, &s_weather, sizeof(s_weather));
  } else if (dict_find(it, MESSAGE_KEY_ERROR)) {
    // location / network failed: keep a recent cache, otherwise show "--°"
    if (time(NULL) - s_weather.ts > WEATHER_ERR_AGE) s_weather.valid = 0;
  }
  apply_match(it);   // M_ERR keeps the cached match (read above)
#endif
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void battery_handler(BatteryChargeState s) {
  s_battery = s.charge_percent;
  s_charging = s.is_charging;
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void bt_handler(bool connected) {
  if (s_bt_connected && !connected && s_cfg.vibe_bt) vibes_double_pulse();
  s_bt_connected = connected;
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) { layer_destroy(s_canvas); s_canvas = NULL; }

static void init(void) {
#if defined(PBL_PLATFORM_EMERY)
  s_anton = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ANTON_80));
  s_bebas = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BEBAS_100));
  s_black_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BLACK_21));
  s_bold_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BOLD_17));
  s_label_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LABEL_22));
  s_day_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DAY_34));
#else
  s_anton = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ANTON_58));
  s_bebas = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BEBAS_80));
  s_black_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BLACK_17));
  s_bold_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BOLD_14));
  s_label_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LABEL_18));
  s_day_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DAY_26));
#endif
  s_mini_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  if (persist_exists(PERSIST_KEY_SETTINGS)) persist_read_data(PERSIST_KEY_SETTINGS, &s_cfg, sizeof(s_cfg));
  if (s_cfg.wx_interval < 5) s_cfg.wx_interval = 30;
  if (s_cfg.style > 2) s_cfg.style = 0;
  if (persist_exists(PERSIST_KEY_WEATHER)) persist_read_data(PERSIST_KEY_WEATHER, &s_weather, sizeof(s_weather));
  if (persist_exists(PERSIST_KEY_MATCH)) persist_read_data(PERSIST_KEY_MATCH, &s_match, sizeof(s_match));
#ifdef PREVIEW_MOCK
#ifndef PREVIEW_OPP
#define PREVIEW_OPP "PAL"
#endif
#ifndef PREVIEW_COMP
#define PREVIEW_COMP "BR"
#endif
#ifndef PREVIEW_CLOCK
#define PREVIEW_CLOCK "67'"
#endif
  s_weather = (Weather){.temp = 27, .tmin = 21, .tmax = 31, .code = 2, .is_day = PREVIEW_DAY, .valid = 1, .ts = time(NULL)};
  s_match = (Match){.mode = PREVIEW_MODE, .home = 1, .fla = 2, .opp = 1, .kickoff = time(NULL) + PREVIEW_KO_OFFSET,
                    .id = 1, .opp_abbr = PREVIEW_OPP, .comp = PREVIEW_COMP, .clock = PREVIEW_CLOCK};
#ifdef PREVIEW_STYLE
  s_cfg.style = PREVIEW_STYLE;
#endif
#ifdef PREVIEW_AWAY
  s_match.home = 0;
#endif
#ifdef PREVIEW_HIDE_WEATHER
  s_cfg.show_weather = 0;
#endif
#ifdef PREVIEW_FAHRENHEIT
  s_cfg.fahrenheit = 1;
#endif
#endif

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){.load = window_load, .unload = window_unload});
  window_stack_push(s_window, true);

  battery_handler(battery_state_service_peek());
  s_bt_connected = connection_service_peek_pebble_app_connection();
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler = bt_handler});
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  app_message_register_inbox_received(inbox_received);
  app_message_open(512, 64);
  update_time();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  app_message_deregister_callbacks();
  window_destroy(s_window);
  fonts_unload_custom_font(s_anton);
  fonts_unload_custom_font(s_bebas);
  fonts_unload_custom_font(s_black_font);
  fonts_unload_custom_font(s_bold_font);
  fonts_unload_custom_font(s_label_font);
  fonts_unload_custom_font(s_day_font);
}

int main(void) { init(); app_event_loop(); deinit(); }

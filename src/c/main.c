// Mengão — Flamengo-themed watchface (rubro-negro) — v1.2
// Time, date (pt-BR), weather (Open-Meteo), next match / live score (ESPN), battery.
#include <pebble.h>

#define COLOR_RED    PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)
#define COLOR_ACCENT PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)

static Window *s_window;
static Layer *s_canvas;
static GFont s_time_font, s_label_font, s_small_font, s_mini_font;
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
} Settings;

static Settings s_cfg = {.fahrenheit = 0, .show_weather = 1, .show_match = 1,
                         .vibe_goal = 1, .vibe_bt = 1, .wx_interval = 30};

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

static void draw_battery(GContext *ctx, GRect r) {
  // body
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, r);
  // nub
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(r.origin.x + r.size.w, r.origin.y + r.size.h / 4, 2, r.size.h / 2), 0, GCornerNone);
  // level
  int inner_w = r.size.w - 4;
  int fill_w = inner_w * s_battery / 100;
  if (fill_w < 1) fill_w = 1;
  GColor level = s_battery <= 20 ? PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite) : PBL_IF_COLOR_ELSE(GColorRed, GColorWhite);
  if (s_charging) level = PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite);
  graphics_context_set_fill_color(ctx, level);
  graphics_fill_rect(ctx, GRect(r.origin.x + 2, r.origin.y + 2, fill_w, r.size.h - 4), 0, GCornerNone);
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

static void draw_pill(GContext *ctx, GRect r) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, r, r.size.h / 2, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, r, r.size.h / 2);
}

static GSize text_size(const char *t, GFont f) {
  return graphics_text_layout_get_content_size(t, f, GRect(0, 0, 200, 60), GTextOverflowModeFill, GTextAlignmentLeft);
}

// Draws text vertically centred in a row (custom fonts have extra top padding)
static void draw_text_vc(GContext *ctx, const char *t, GFont f, int x, int row_y, int row_h, int tw, int nudge) {
  GSize sz = text_size(t, f);
  graphics_draw_text(ctx, t, f, GRect(x, row_y + (row_h - sz.h) / 2 - nudge, tw + 4, sz.h + 8),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// ---------- Layout pieces ----------
static void draw_top_pill(GContext *ctx, GRect top, int h, bool compact) {
  draw_pill(ctx, top);
  int sh = top.size.h * 78 / 100, sw = sh * 85 / 100;
  GRect shield = GRect(top.origin.x + top.size.h / 2 - 7, top.origin.y + (top.size.h - sh) / 2, sw, sh);
  draw_shield(ctx, shield);
  int nx = shield.origin.x + sw + 2;
  int nw = top.origin.x + top.size.w - nx - 6;
  graphics_context_set_text_color(ctx, GColorWhite);
  GSize ns = text_size("MENGÃO", s_label_font);
  graphics_draw_text(ctx, "MENGÃO", s_label_font,
                     GRect(nx, top.origin.y + (top.size.h - ns.h) / 2 - h / 40 + (compact ? 3 : 0), nw, ns.h + 8),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void draw_center_panel(GContext *ctx, GRect panel, int w, int h, bool compact) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, panel, 10, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, panel, 10);

  int time_up = compact ? h * 6 / 100 + 3 : h * 4 / 100;
  int div_pct = compact ? 64 : 66;
  GRect tr = GRect(panel.origin.x, panel.origin.y - time_up, panel.size.w, panel.size.h * 75 / 100);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_time_buf, s_time_font, tr, GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  int dy = panel.origin.y + panel.size.h * div_pct / 100;
  graphics_context_set_fill_color(ctx, COLOR_ACCENT);
  graphics_fill_rect(ctx, GRect(panel.origin.x + w * 12 / 100, dy, panel.size.w - w * 24 / 100, 3), 0, GCornerNone);

  GRect dr = GRect(panel.origin.x, dy + 1 - (compact ? 2 : 0), panel.size.w, panel.origin.y + panel.size.h - dy + 8);
  graphics_context_set_text_color(ctx, COLOR_ACCENT);
  graphics_draw_text(ctx, s_date_buf, s_label_font, dr, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static int temp_display(int c) {
  if (!s_cfg.fahrenheit) return c;
  int f10 = c * 18 + 320;                       // tenths of °F
  return (f10 >= 0) ? (f10 + 5) / 10 : (f10 - 5) / 10;
}

// Match pill: [comp tag] [live dot] main-text info-text  (drops pieces until it fits)
static void draw_match_pill(GContext *ctx, GRect p, int h) {
  draw_pill(ctx, p);
  GFont f = s_small_font;
  int pad = p.size.h / 4;
  int avail = p.size.w - 2 * pad;
  bool scored = (s_match.mode == M_LIVE || s_match.mode == M_FINAL);

  static char main_full[28], main_short[28], info_full[28], info_short[28], tag[8];
  const char *opp = s_match.opp_abbr[0] ? s_match.opp_abbr : "???";
  if (scored) {
    if (s_match.home) snprintf(main_full, sizeof(main_full), "FLA %d x %d %s", s_match.fla, s_match.opp, opp);
    else snprintf(main_full, sizeof(main_full), "%s %d x %d FLA", opp, s_match.opp, s_match.fla);
    snprintf(main_short, sizeof(main_short), "%d x %d %s", s_match.fla, s_match.opp, opp);
    if (s_match.mode == M_LIVE) snprintf(info_full, sizeof(info_full), "%s", s_match.clock);
    else snprintf(info_full, sizeof(info_full), "FIM");
    snprintf(info_short, sizeof(info_short), "%s", info_full);
  } else {
    if (s_match.home) snprintf(main_full, sizeof(main_full), "FLA x %s", opp);
    else snprintf(main_full, sizeof(main_full), "%s x FLA", opp);
    snprintf(main_short, sizeof(main_short), "x %s", opp);
    time_t now = time(NULL), ko = s_match.kickoff;
    struct tm tn = *localtime(&now);
    struct tm tk = *localtime(&ko);
    char hm[12];
    strftime(hm, sizeof(hm), clock_is_24h_style() ? "%H:%M" : "%l:%M", &tk);
    const char *hmp = (hm[0] == ' ') ? hm + 1 : hm;
    // days between (local dates)
    struct tm a = tn, b = tk;
    a.tm_hour = b.tm_hour = 12; a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
    int ddays = (int)((mktime(&b) - mktime(&a) + 43200) / 86400);
    if (ddays == 0) snprintf(info_full, sizeof(info_full), "HOJE %s", hmp);
    else if (ddays == 1) snprintf(info_full, sizeof(info_full), "AMANHÃ %s", hmp);
    else if (ddays > 1 && ddays < 7) snprintf(info_full, sizeof(info_full), "%s %s", DIAS[tk.tm_wday], hmp);
    else snprintf(info_full, sizeof(info_full), "%02d/%02d %s", tk.tm_mday, tk.tm_mon + 1, hmp);
    if (ddays == 0) snprintf(info_short, sizeof(info_short), "%s", hmp);
    else if (ddays > 0 && ddays < 7) snprintf(info_short, sizeof(info_short), "%s", ddays == 1 ? "AMANHÃ" : DIAS[tk.tm_wday]);
    else snprintf(info_short, sizeof(info_short), "%02d/%02d", tk.tm_mday, tk.tm_mon + 1);
  }
  snprintf(tag, sizeof(tag), "%s", s_match.comp);

  bool live = s_match.mode == M_LIVE;
  int gap = 6, dot = live ? 8 : 0;
  int tag_w = tag[0] ? text_size(tag, s_mini_font).w + 8 : 0;
  // candidates in order of preference
  const char *mains[] = {main_full, main_full, main_full, main_short, main_short};
  const char *infos[] = {info_full, info_full, info_short, info_short, info_short};
  bool tags[] = {true, false, false, false, false};
  int chosen = 4;
  for (int i = 0; i < 5; i++) {
    int wv = dot + text_size(mains[i], f).w + gap + text_size(infos[i], f).w + (tags[i] && tag_w ? tag_w + gap : 0);
    if (wv <= avail) { chosen = i; break; }
  }
  const char *mt = mains[chosen], *it = infos[chosen];
  bool show_tag = tags[chosen] && tag_w;
  int mw = text_size(mt, f).w, iw = text_size(it, f).w;
  int total = dot + mw + gap + iw + (show_tag ? tag_w + gap : 0);
  int x = p.origin.x + (p.size.w - total) / 2;
  int nudge = h / 40;

  if (show_tag) {
    int th = p.size.h * 55 / 100;
    GRect tr = GRect(x, p.origin.y + (p.size.h - th) / 2, tag_w, th);
    graphics_context_set_fill_color(ctx, COLOR_ACCENT);
    graphics_fill_rect(ctx, tr, 3, GCornersAll);
    GSize ts = text_size(tag, s_mini_font);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, tag, s_mini_font, GRect(tr.origin.x, tr.origin.y + (th - ts.h) / 2 - 3, tag_w, ts.h + 6),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    x += tag_w + gap;
  }
  if (live) {
    graphics_context_set_fill_color(ctx, COLOR_ACCENT);
    graphics_fill_circle(ctx, GPoint(x + 3, p.origin.y + p.size.h / 2), 3);
    x += dot;
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  draw_text_vc(ctx, mt, f, x, p.origin.y, p.size.h, mw, nudge);
  x += mw + gap;
  graphics_context_set_text_color(ctx, COLOR_ACCENT);
  draw_text_vc(ctx, it, f, x, p.origin.y, p.size.h, iw, nudge);
}

static void draw_bottom_row(GContext *ctx, int w, int h, int ry, int rh) {
  bool big = PBL_IF_RECT_ELSE(w >= 180, false);
  int rx = w * 2 / 100, rw = w * 96 / 100;
  int gap = 4;
  int pad = rh / 4;
  int nudge = h / 40;

  // --- battery pill (right, or centred when weather is hidden)
  static char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", s_battery);
  int bh = rh * 36 / 100, bw = bh * 2;
  int bt_w = s_bt_connected ? 0 : 14;
  bool show_pct = big || !s_cfg.show_weather;
  int pct_w = show_pct ? text_size(pct, s_small_font).w : 0;
  int bp_w = pad + bt_w + bw + 2 + (show_pct ? 4 + pct_w : 0) + pad - 2;
  int bx = s_cfg.show_weather ? rx + rw - bp_w : (w - bp_w) / 2;
  GRect bp = GRect(bx, ry, bp_w, rh);
  draw_pill(ctx, bp);
  int x = bp.origin.x + pad;
  if (!s_bt_connected) {
    draw_bt_off(ctx, GPoint(x + 4, ry + (rh - bh - 4) / 2), bh + 4);
    x += bt_w;
  }
  draw_battery(ctx, GRect(x, ry + (rh - bh) / 2, bw, bh));
  x += bw + 2 + 4;
  if (show_pct) {
    graphics_context_set_text_color(ctx, GColorWhite);
    draw_text_vc(ctx, pct, s_small_font, x, ry, rh, pct_w, nudge - 1);
  }
  if (!s_cfg.show_weather) return;

  // --- weather pill (left)
  GRect wp = GRect(rx, ry, rw - bp_w - gap, rh);
  draw_pill(ctx, wp);
  bool ok = weather_usable();
  static char temp[12], tmax[12], tmin[12];
  if (ok) snprintf(temp, sizeof(temp), "%d°", temp_display(s_weather.temp));
  else snprintf(temp, sizeof(temp), "--°");
  GFont tf = big ? s_label_font : s_small_font;
  int is = rh * 72 / 100;
  int temp_w = text_size(temp, tf).w;
  int content = (ok ? is + 4 : 0) + temp_w;
  bool show_mm = false;
  int mm_w = 0;
  if (ok && rh >= 30) {
    snprintf(tmax, sizeof(tmax), "%d°", temp_display(s_weather.tmax));
    snprintf(tmin, sizeof(tmin), "%d°", temp_display(s_weather.tmin));
    GSize a = text_size(tmax, s_mini_font), b = text_size(tmin, s_mini_font);
    mm_w = a.w > b.w ? a.w : b.w;
    if (content + 5 + mm_w <= wp.size.w - 2 * pad) { show_mm = true; content += 5 + mm_w; }
  }
  x = wp.origin.x + (wp.size.w - content) / 2;
  if (ok) {
    draw_weather_icon(ctx, wx_icon_for(s_weather.code), s_weather.is_day, x, ry + (rh - is) / 2, is);
    x += is + 4;
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  draw_text_vc(ctx, temp, tf, x, ry, rh, temp_w, nudge);
  x += temp_w + 5;
  if (show_mm) {
    int lh = rh / 2;
    graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
    graphics_draw_text(ctx, tmax, s_mini_font, GRect(x, ry + 1, mm_w + 4, lh + 4), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite));
    graphics_draw_text(ctx, tmin, s_mini_font, GRect(x, ry + lh - 3, mm_w + 4, lh + 4), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w = b.size.w, h = b.size.h;
  bool big = PBL_IF_RECT_ELSE(w >= 180, false);
  bool match = match_visible();

  // Rubro-negro horizontal stripes
  int stripes = 12;
  for (int i = 0; i < stripes; i++) {
    int y0 = h * i / stripes, y1 = h * (i + 1) / stripes;
    graphics_context_set_fill_color(ctx, (i % 2 == 0) ? COLOR_RED : GColorBlack);
    graphics_fill_rect(ctx, GRect(0, y0, w, y1 - y0), 0, GCornerNone);
  }

  if (match && big) {
    // emery: MENGÃO pill, compact time panel, match pill, bottom row
    draw_top_pill(ctx, GRect(w * 14 / 100, h * 2 / 100, w * 72 / 100, h * 14 / 100), h, true);
    draw_center_panel(ctx, GRect(w * 5 / 100, h * 18 / 100, w * 90 / 100, h * 45 / 100), w, h, true);
    draw_match_pill(ctx, GRect(w * 2 / 100, h * 65 / 100, w * 96 / 100, h * 14 / 100), h);
    draw_bottom_row(ctx, w, h, h * 81 / 100, h * 15 / 100);
  } else {
    // v1.1 layout; on small watches the match pill takes the place of the MENGÃO pill
    if (match) draw_match_pill(ctx, GRect(w * 2 / 100, h * 3 / 100, w * 96 / 100, h * 17 / 100), h);
    else draw_top_pill(ctx, GRect(w * 14 / 100, h * 3 / 100, w * 72 / 100, h * 17 / 100), h, false);
    draw_center_panel(ctx, GRect(w * 5 / 100, h * 25 / 100, w * 90 / 100, h * 50 / 100), w, h, false);
    draw_bottom_row(ctx, w, h, h * 80 / 100, h * 15 / 100);
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
  apply_match(it);   // M_ERR (no M_MODE) keeps the cached match
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
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_LG_80));
  s_label_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LABEL_LG_30));
  s_small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LABEL_SM_22));
#else
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_SM_60));
  s_label_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LABEL_SM_22));
  s_small_font = s_label_font;
#endif
  s_mini_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  if (persist_exists(PERSIST_KEY_SETTINGS)) persist_read_data(PERSIST_KEY_SETTINGS, &s_cfg, sizeof(s_cfg));
  if (s_cfg.wx_interval < 5) s_cfg.wx_interval = 30;
  if (persist_exists(PERSIST_KEY_WEATHER)) persist_read_data(PERSIST_KEY_WEATHER, &s_weather, sizeof(s_weather));
  if (persist_exists(PERSIST_KEY_MATCH)) persist_read_data(PERSIST_KEY_MATCH, &s_match, sizeof(s_match));
#ifdef PREVIEW_MOCK
  s_weather = (Weather){.temp = 27, .tmin = 21, .tmax = 31, .code = 2, .is_day = PREVIEW_DAY, .valid = 1, .ts = time(NULL)};
  s_match = (Match){.mode = PREVIEW_MODE, .home = 1, .fla = 2, .opp = 1, .kickoff = time(NULL) + PREVIEW_KO_OFFSET,
                    .id = 1, .opp_abbr = "EST", .comp = "LIB", .clock = "67'"};
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
  fonts_unload_custom_font(s_time_font);
  fonts_unload_custom_font(s_label_font);
#if defined(PBL_PLATFORM_EMERY)
  fonts_unload_custom_font(s_small_font);
#endif
}

int main(void) { init(); app_event_loop(); deinit(); }

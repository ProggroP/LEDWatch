// LED Watch -- Spaltenuhr im Stil einer Binaer-LED-Armbanduhr.
// Zielplattform: ausschliesslich Emery (200 x 228).
//
// Vier Spalten, jeweils zwoelf Zeilen hoch: Stunde, Wochentag, Minutenblock
// (00..55 in Fuenferschritten) und Minuten-Einer (0..4). In jeder Spalte
// leuchtet genau eine Zelle. Ein Schuetteln blendet das Datum ein, das
// naechste die Kalenderwoche, danach geht es zurueck zur Uhrzeit -- ebenso
// automatisch nach MODE_TIMEOUT_MS.

#include <pebble.h>

// ================================================================== Farben

#define COLOR_BG              GColorBlack

#define COLOR_HOUR_ON         GColorPictonBlue
#define COLOR_HOUR_OFF_TEXT   GColorBlueMoon
#define COLOR_HOUR_OFF_LED    GColorOxfordBlue

#define COLOR_WDAY_ON         GColorRed
#define COLOR_WDAY_OFF_TEXT   GColorRed
#define COLOR_WDAY_OFF_LED    GColorBulgarianRose

#define COLOR_MIN_ON          GColorChromeYellow
#define COLOR_MIN_OFF_TEXT    GColorLightGray
#define COLOR_MIN_OFF_LED     GColorDarkGray

#define COLOR_ONE_ON          GColorGreen
#define COLOR_ONE_OFF_TEXT    GColorGreen
#define COLOR_ONE_OFF_LED     GColorDarkGreen

#define COLOR_CELL_TEXT_ON    GColorBlack   // Schrift auf der leuchtenden Zelle
#define COLOR_LED_CORE        GColorWhite   // heller Kern der aktiven LED

#define COLOR_CARD_BG         GColorBlack
#define COLOR_CARD_VALUE      GColorWhite
#define COLOR_DATE_ACCENT     GColorRed
#define COLOR_DATE_SUB        GColorChromeYellow
#define COLOR_WEEK_ACCENT     GColorGreen
#define COLOR_WEEK_SUB        GColorLightGray

// ================================================================== Layout

#define ROWS            12
#define ROW_H           19            // 12 * 19 = 228 = volle Emery-Hoehe

#define LED_W           14
#define LED_H           10
#define LED_RADIUS       3
#define LED_CORE_INSET   3            // Kernpunkt der leuchtenden LED

#define CELL_PAD         2            // Luft links/rechts um die aktive Zelle
#define CELL_RADIUS      4
#define TEXT_DY         (-2)          // Feinjustage der Grundlinie in der Zeile

// Spalte 1 -- Stunde
#define HOUR_LABEL_X     3
#define HOUR_LABEL_W    24
#define HOUR_LED_X      31
// Spalte 2 -- Wochentag
#define WDAY_LABEL_X    55
#define WDAY_LABEL_W    32
#define WDAY_LED_X      91
// Spalte 3 -- Minutenblock
#define MIN_LABEL_X    115
#define MIN_LABEL_W     24
#define MIN_LED_X      143
// Spalte 4 -- Minuten-Einer
#define ONE_LABEL_X    167
#define ONE_LABEL_W     12
#define ONE_LED_X      183

// AM/PM-Feld, nur im 12-Stunden-Modus (Zeile 0 der vierten Spalte)
#define AMPM_X         163
#define AMPM_W          34

// Einblendkarte fuer Datum und Kalenderwoche
#define CARD_X          10
#define CARD_Y          48
#define CARD_W         180
#define CARD_H         132
#define CARD_RADIUS      8
#define CARD_BORDER      3
#define CARD_TOP_Y      (CARD_Y + 8)    // Ueberschrift
#define CARD_VALUE_Y    (CARD_Y + 38)   // grosse Zahl
#define CARD_SUB_Y      (CARD_Y + 96)   // Unterzeile

// ================================================================== Timing

#define MODE_TIMEOUT_MS   6000        // Rueckkehr zur Uhrzeit
#define TAP_DEBOUNCE_MS    600        // ein Schuetteln feuert oft mehrfach

// =================================================================== Modi

#define MODE_TIME  0
#define MODE_DATE  1
#define MODE_WEEK  2
#define MODE_COUNT 3

// ================================================================== Global

static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_mode_timer;
static int s_mode = MODE_TIME;
static uint64_t s_last_tap_ms;

static const char *WDAY_SHORT[7] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
static const char *WDAY_LONG[7]  = { "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY",
                                     "FRIDAY", "SATURDAY", "SUNDAY" };
static const char *MONTH_LONG[12] = { "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY",
                                      "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER",
                                      "NOVEMBER", "DECEMBER" };

// ============================================================ Kalenderwoche

// Wochentag des 31. Dezember nach Zeller-Kurzform: 4 = Donnerstag.
static int last_day_of_year(int year) {
  return (year + year / 4 - year / 100 + year / 400) % 7;
}

static int weeks_in_year(int year) {
  return (last_day_of_year(year) == 4 || last_day_of_year(year - 1) == 3) ? 53 : 52;
}

// ISO-8601-Kalenderwoche. strftime("%V") ist auf der Uhr nicht verlaesslich.
static int iso_week(const struct tm *t) {
  int year = t->tm_year + 1900;
  int wday = (t->tm_wday + 6) % 7;              // 0 = Montag
  int week = (t->tm_yday - wday + 10) / 7;      // tm_yday ist 0-basiert
  if (week < 1) return weeks_in_year(year - 1);
  if (week > weeks_in_year(year)) return 1;
  return week;
}

static int iso_year(const struct tm *t) {
  int year = t->tm_year + 1900;
  int wday = (t->tm_wday + 6) % 7;
  int week = (t->tm_yday - wday + 10) / 7;
  if (week < 1) return year - 1;
  if (week > weeks_in_year(year)) return year + 1;
  return year;
}

// ================================================================= Zeichnen

// Eine Zelle: Beschriftung plus LED. Leuchtet sie, liegt eine gefuellte
// Flaeche darunter -- schwarze Schrift auf Farbe traegt am weitesten.
static void draw_cell(GContext *ctx, int label_x, int label_w, int led_x, int row,
                      const char *label, bool on,
                      GColor on_color, GColor off_text, GColor off_led) {
  const int row_y = row * ROW_H;
  const int led_y = row_y + (ROW_H - LED_H) / 2;

  if (on) {
    GRect cell = GRect(label_x - CELL_PAD, row_y + 1,
                       (led_x + LED_W + CELL_PAD) - (label_x - CELL_PAD), ROW_H - 2);
    graphics_context_set_fill_color(ctx, on_color);
    graphics_fill_rect(ctx, cell, CELL_RADIUS, GCornersAll);

    graphics_context_set_fill_color(ctx, COLOR_LED_CORE);
    graphics_fill_rect(ctx, GRect(led_x + LED_CORE_INSET, led_y + LED_CORE_INSET,
                                  LED_W - 2 * LED_CORE_INSET, LED_H - 2 * LED_CORE_INSET),
                       1, GCornersAll);
  } else {
    graphics_context_set_fill_color(ctx, off_led);
    graphics_fill_rect(ctx, GRect(led_x, led_y, LED_W, LED_H), LED_RADIUS, GCornersAll);
  }

  graphics_context_set_text_color(ctx, on ? COLOR_CELL_TEXT_ON : off_text);
  graphics_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(label_x, row_y + TEXT_DY, label_w, ROW_H + 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void draw_grid(GContext *ctx, const struct tm *t) {
  char buf[4];
  const bool h24 = clock_is_24h_style();
  const int hour = t->tm_hour;
  const int iso_wday = (t->tm_wday + 6) % 7;    // 0 = Montag

  // -- Stunden: 12-Stunden-Modus zeigt 1..12, 24-Stunden-Modus 00..11 bzw. 12..23
  const int hour12 = (hour % 12 == 0) ? 12 : hour % 12;
  const int hour_base = (hour >= 12) ? 12 : 0;
  const int hour_row = h24 ? (11 - (hour % 12)) : (12 - hour12);
  for (int r = 0; r < ROWS; r++) {
    int value = h24 ? hour_base + (11 - r) : 12 - r;
    snprintf(buf, sizeof(buf), "%02d", value);
    draw_cell(ctx, HOUR_LABEL_X, HOUR_LABEL_W, HOUR_LED_X, r, buf, r == hour_row,
              COLOR_HOUR_ON, COLOR_HOUR_OFF_TEXT, COLOR_HOUR_OFF_LED);
  }

  // -- Wochentage: Montag unten (Zeile 11) bis Sonntag (Zeile 5)
  for (int d = 0; d < 7; d++) {
    int r = 11 - d;
    draw_cell(ctx, WDAY_LABEL_X, WDAY_LABEL_W, WDAY_LED_X, r, WDAY_SHORT[d],
              d == iso_wday, COLOR_WDAY_ON, COLOR_WDAY_OFF_TEXT, COLOR_WDAY_OFF_LED);
  }

  // -- Minutenblock: 00 unten bis 55 oben
  const int min_row = 11 - (t->tm_min / 5);
  for (int r = 0; r < ROWS; r++) {
    snprintf(buf, sizeof(buf), "%02d", (11 - r) * 5);
    draw_cell(ctx, MIN_LABEL_X, MIN_LABEL_W, MIN_LED_X, r, buf, r == min_row,
              COLOR_MIN_ON, COLOR_MIN_OFF_TEXT, COLOR_MIN_OFF_LED);
  }

  // -- Minuten-Einer: 0 unten (Zeile 11) bis 4 (Zeile 7)
  const int one_row = 11 - (t->tm_min % 5);
  for (int u = 0; u < 5; u++) {
    int r = 11 - u;
    snprintf(buf, sizeof(buf), "%d", u);
    draw_cell(ctx, ONE_LABEL_X, ONE_LABEL_W, ONE_LED_X, r, buf, r == one_row,
              COLOR_ONE_ON, COLOR_ONE_OFF_TEXT, COLOR_ONE_OFF_LED);
  }

  // -- AM/PM nur dort, wo die Stundenspalte es nicht selbst zeigt
  if (!h24) {
    const bool pm = hour >= 12;
    if (pm) {
      graphics_context_set_fill_color(ctx, COLOR_HOUR_ON);
      graphics_fill_rect(ctx, GRect(AMPM_X, 1, AMPM_W, ROW_H - 2), CELL_RADIUS, GCornersAll);
    }
    graphics_context_set_text_color(ctx, pm ? COLOR_CELL_TEXT_ON : COLOR_HOUR_OFF_TEXT);
    graphics_draw_text(ctx, "PM", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(AMPM_X, TEXT_DY, AMPM_W, ROW_H + 4),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// Einblendkarte ueber dem Raster -- deckend, damit der Text frei steht.
static void draw_card(GContext *ctx, GColor accent, const char *top,
                      const char *value, GColor sub_color, const char *sub) {
  GRect card = GRect(CARD_X, CARD_Y, CARD_W, CARD_H);
  graphics_context_set_fill_color(ctx, COLOR_CARD_BG);
  graphics_fill_rect(ctx, card, CARD_RADIUS, GCornersAll);
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, CARD_BORDER);
  graphics_draw_round_rect(ctx, card, CARD_RADIUS);

  graphics_context_set_text_color(ctx, accent);
  graphics_draw_text(ctx, top, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(CARD_X, CARD_TOP_Y, CARD_W, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, COLOR_CARD_VALUE);
  graphics_draw_text(ctx, value, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
                     GRect(CARD_X, CARD_VALUE_Y, CARD_W, 52),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, sub_color);
  graphics_draw_text(ctx, sub, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(CARD_X, CARD_SUB_Y, CARD_W, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, COLOR_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  draw_grid(ctx, t);

  if (s_mode == MODE_DATE) {
    char day[4];
    snprintf(day, sizeof(day), "%02d", t->tm_mday);
    draw_card(ctx, COLOR_DATE_ACCENT, WDAY_LONG[(t->tm_wday + 6) % 7], day,
              COLOR_DATE_SUB, MONTH_LONG[t->tm_mon]);
  } else if (s_mode == MODE_WEEK) {
    char week[4], year[12];
    snprintf(week, sizeof(week), "%02d", iso_week(t));
    snprintf(year, sizeof(year), "%d", iso_year(t));
    draw_card(ctx, COLOR_WEEK_ACCENT, "WEEK", week, COLOR_WEEK_SUB, year);
  }
}

// ================================================================== Ablauf

static void mode_timer_cb(void *data) {
  s_mode_timer = NULL;
  s_mode = MODE_TIME;
  layer_mark_dirty(s_canvas);
}

static void schedule_return(void) {
  if (s_mode_timer) {
    app_timer_cancel(s_mode_timer);
    s_mode_timer = NULL;
  }
  if (s_mode != MODE_TIME) {
    s_mode_timer = app_timer_register(MODE_TIMEOUT_MS, mode_timer_cb, NULL);
  }
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  // Ein einzelnes Schuetteln loest oft mehrere Taps aus -- kurz entprellen.
  uint64_t now_ms = (uint64_t)time(NULL) * 1000 + time_ms(NULL, NULL);
  if (now_ms - s_last_tap_ms < TAP_DEBOUNCE_MS) return;
  s_last_tap_ms = now_ms;

  s_mode = (s_mode + 1) % MODE_COUNT;   // Zeit -> Datum -> Woche -> Zeit
  schedule_return();
  layer_mark_dirty(s_canvas);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(s_canvas);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, COLOR_BG);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);
}

static void deinit(void) {
  if (s_mode_timer) app_timer_cancel(s_mode_timer);
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

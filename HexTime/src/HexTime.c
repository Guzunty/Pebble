#include "pebble.h"

static Window *window;

static void update_time();
static TextLayer *s_time_layer;
static Layer *s_pix_layer;
static Layer *s_dial_layer;
static Window * hexWindow;
#ifndef PBL_COLOR
//static InverterLayer *s_inv_layer;
#endif
static long hex_jiffies;
static GFont hexFont = NULL;
static GPoint centre = {
  .x = 72,
  .y = 84
};

void draw_dial(GContext *ctx, GFont font);
void gpoint_rotate_to(GPoint * point, int32_t angle);
void gpoint_move_to(GPoint * point, GPoint by);
void draw_minute_hand(GContext *ctx, int angle);
void draw_hour_hand(GContext *ctx, int angle);
void draw_hand(GContext *ctx, GPathInfo *hand_path, int angle);
void draw_text_time();

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void timer_handler(void * data) {
  update_time();
  uint32_t wait = 1318;
  uint32_t error = hex_jiffies & 0x0f;
  if (error > 8) {
    error = -(16 - error);
  }
  wait -= error * 10;
  app_timer_register(wait, timer_handler, NULL);
  layer_mark_dirty(window_get_root_layer(hexWindow));
}

void gpoint_rotate_to(GPoint * point, int32_t angle) {
  point->x = ((float)point->x) / TRIG_MAX_RATIO * sin_lookup(angle);
  point->y = ((float)point->y) / TRIG_MAX_RATIO * cos_lookup(angle);
}

void gpoint_move_to(GPoint * point, GPoint by) {
  point->x = (point->x) + by.x;
  point->y = (point->y) + by.y;
}

static void dial_draw(Layer *layer, GContext *ctx) {
  draw_dial(ctx, hexFont);
}

static void hands_draw(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, centre, 5);

  int32_t hex_mins = hex_jiffies >> 4;
  float angle = ((float)hex_mins / 4096.0) * (float)TRIG_MAX_RATIO;

  draw_minute_hand(ctx, angle);

  int32_t hex_hours = hex_jiffies >> 4;
  angle = ((float)hex_hours / 65536.0) * (float)TRIG_MAX_RATIO;

  draw_hour_hand(ctx, angle);

  // draw second hand
  angle = ((float)(hex_jiffies & 0xf0) / 256.0) * TRIG_MAX_RATIO;
  GPoint tip = GPoint(70, -70);
  gpoint_rotate_to(&tip, (int32_t)angle);
  gpoint_move_to(&tip, centre);
  GPoint base = GPoint(-18, 18);
  gpoint_rotate_to(&base, (int32_t)angle);
  gpoint_move_to(&base, centre);
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_context_set_stroke_color(ctx, GColorRed);
#endif
  graphics_draw_line(ctx, base, tip);
  graphics_fill_circle(ctx, base, 2);

  //graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, centre, 2);
}

static void window_load(Window *window) {
  hexWindow = window;
  s_dial_layer = layer_create(GRect(0, 0, 144, 168));
  layer_set_update_proc(s_dial_layer, dial_draw);
  layer_add_child(window_get_root_layer(window), s_dial_layer);

  // Create time TextLayer
  s_time_layer = text_layer_create(GRect(0, 100, 144, 50));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);

  hexFont = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  text_layer_set_font(s_time_layer, hexFont);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_time_layer));

  s_pix_layer = layer_create(GRect(0, 0, 144, 168));
  layer_set_update_proc(s_pix_layer, hands_draw);

  // Add it as another child layer to the Window's root layer
  layer_add_child(window_get_root_layer(window), s_pix_layer);

}

static void window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  layer_destroy(s_pix_layer);
}

static void init(void) {
  // Register with TickTimerService
  //tick_timer_service_subscribe(SECOND_UNIT, tick_handler);   // We want hex seconds so regular tick service isn't useful
  app_timer_register(1318, timer_handler, NULL); 
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_set_background_color(window, GColorBlack);

  const bool animated = true;
  window_stack_push(window, animated);
  update_time();
}

static void deinit(void) {
  window_destroy(window);
}

int main(void) {
  init();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);
  app_event_loop();
  deinit();
}

static void update_time() {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);

  long msec = ((tick_time->tm_hour * 3600000) + (tick_time->tm_min * 60000) + tick_time->tm_sec * 1000) + time_ms(NULL, NULL);
  hex_jiffies = (long)((float)msec * 0.012136296296296296);
  if (hex_jiffies & 0x08) {           // Round up LS digit
    hex_jiffies += 0x10;
  }
  draw_text_time();
}

void draw_text_time() {
  static char hex_buffer[5] = "@@@@";
  static char output[7] = "______";
  snprintf(hex_buffer, sizeof(hex_buffer), "%04X", (unsigned)(hex_jiffies >> 4));
  output[0] = hex_buffer[0];
  output[2] = hex_buffer[1];
  output[3] = hex_buffer[2];
  output[5] = hex_buffer[3];
  // Display this time on the TextLayer
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_text(s_time_layer, output);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Time updated %s", output);
}

void draw_dial(GContext *ctx, GFont font) {
  graphics_context_set_stroke_color(ctx, GColorWhite);
  for(int i = 0; i < 0x10; i++) {
    int base_angle = i << 12;
    for (int j = 0; j < 16; j += 4) {
      int offset = j << 8;
      int ticksize = 8;
      if (j == 8) {
        ticksize = 6;
      }
      else {
        if (j == 4 || j == 12) {
          ticksize = 4;
        }
      }
      GPoint outer = GPoint(74, 74);          // Major division
      gpoint_rotate_to(&outer, base_angle + offset);
      gpoint_move_to(&outer, centre);
      GPoint inner = GPoint(74 - ticksize, 74 - ticksize);
      gpoint_rotate_to(&inner, base_angle + offset);
      gpoint_move_to(&inner, centre);
      graphics_draw_line(ctx, inner, outer);

      outer = GPoint(74, 74);                // Minor division
      gpoint_rotate_to(&outer, base_angle + offset + 512);
      gpoint_move_to(&outer, centre);
      inner = GPoint(74 - 2, 74 - 2);
      gpoint_rotate_to(&inner, base_angle + offset + 512);
      gpoint_move_to(&inner, centre);
      graphics_draw_line(ctx, inner, outer);
    }
    // Show each digit
    const char * digits[16] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F"};
    const int digitSize = 12;
    GPoint digitPosn = GPoint(58, -58);
    gpoint_rotate_to(&digitPosn, base_angle);
    gpoint_move_to(&digitPosn, centre);
    GRect digitRect = GRect(digitPosn.x - 5, digitPosn.y - 12, digitSize, digitSize);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, digits[i], font, digitRect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
  GRect logoRect = GRect(0, 52, 144, 20);
  GFont logoFont = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  graphics_draw_text(ctx, "guzunty@github", logoFont, logoRect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static GPathInfo MINUTE_HAND = {
  .num_points = 5,
  .points = (GPoint []) {{-2, -64}, {2, -64}, {5, 0}, {0, 24}, {-5, 0}}
};

void draw_minute_hand(GContext *ctx, int angle) {
  draw_hand(ctx, &MINUTE_HAND, angle);
}

static GPathInfo HOUR_HAND = {
  .num_points = 6,
  .points = (GPoint []) {{-1, -50}, {1, -50}, {6, -12}, {2, 0}, {-2, 0}, {-6, -12}}
};

void draw_hour_hand(GContext *ctx, int angle) {
  draw_hand(ctx, &HOUR_HAND, angle);
}

void draw_hand(GContext *ctx, GPathInfo *hand_path, int angle) {
  static GPath* s_hand_ptr = NULL;
  s_hand_ptr = gpath_create(hand_path);
  gpath_rotate_to(s_hand_ptr, angle);
  gpath_move_to(s_hand_ptr, GPoint(72, 84));
  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, s_hand_ptr);
  gpath_destroy(s_hand_ptr);
}

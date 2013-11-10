#include <pebble.h>

#define TIMEOUT 500
#define DEBUG_LEN 31

static Window *window;
static TextLayer *delta_layer;
static TextLayer *magnitude_layer; // max needs tab completion
static AppTimer *timer;
static char zaccel[DEBUG_LEN];
static char maccel[DEBUG_LEN];

static int test_num = 0;
static int is_testing = 0;

static AccelData prev_frame = {0,0,0};
static AccelData cur_frame = {0,0,0};

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    is_testing ^= 1;
    if (is_testing) {
        test_num++;
    }
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    //text_layer_set_text(text_layer, "Up");
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    //text_layer_set_text(text_layer, "Down");
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    delta_layer = text_layer_create((GRect) { .origin = { 0, 72 }, .size = { bounds.size.w, 20 } });
    text_layer_set_text(delta_layer, "delta");
    text_layer_set_text_alignment(delta_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(delta_layer));

    magnitude_layer = text_layer_create((GRect) { .origin = { 0, 99 }, .size = { bounds.size.w, 20 } });
    text_layer_set_text(magnitude_layer, "magnitude");
    text_layer_set_text_alignment(magnitude_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(magnitude_layer));
}

static void window_unload(Window *window) {
    text_layer_destroy(delta_layer);
    text_layer_destroy(magnitude_layer);
}

// actually sqrt
#define SQRT_MAGIC_F 0x5f3759df 
float butt_rt(const float x) {
    const float xhalf = 0.5f*x;

    union { // get bits for floating value
        float x;
        int i;
    } u;
    u.x = x;
    u.i = SQRT_MAGIC_F - (u.i >> 1);  // gives initial guess y0
    return x*u.x*(1.5f - xhalf*u.x*u.x);// Newton step, repeating increases accuracy 
}

static void timer_callback(void *context) {
    int deltax, deltay, deltaz;
    accel_service_peek(&cur_frame);
    deltax = cur_frame.x - prev_frame.x;
    deltay = cur_frame.y - prev_frame.y;
    deltaz = cur_frame.z - prev_frame.z;

    int magnitude = butt_rt(deltax*deltax + deltay*deltay + deltaz*deltaz);

    prev_frame = cur_frame;
    snprintf(zaccel, DEBUG_LEN, "X:%d Y:%d Z:%d", deltax, deltay, deltaz);
    snprintf(maccel, DEBUG_LEN, "a:%d, t:%d, M:%d", is_testing, test_num, magnitude);
    text_layer_set_text(delta_layer, zaccel);
    text_layer_set_text(magnitude_layer, maccel);
    //layer_mark_dirty(window_get_root_layer(window));

    if (is_testing) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", maccel);
        if (magnitude > 2000) {
            vibes_double_pulse();
        }
    }

    const uint32_t timeout_ms = TIMEOUT;
    timer = app_timer_register(timeout_ms, timer_callback, NULL);
}

static void handle_accel(AccelData *accel_data, uint32_t num_samples) {
    // do nothing
}

static void init(void) {
    accel_data_service_subscribe(0, handle_accel);
    window = window_create();
    window_set_click_config_provider(window, click_config_provider);
    window_set_window_handlers(window, (WindowHandlers) {
            .load = window_load,
            .unload = window_unload,
            });
    const bool animated = true;
    window_stack_push(window, animated);

    accel_service_peek(&prev_frame);
    const uint32_t timeout_ms = TIMEOUT;
    timer = app_timer_register(timeout_ms, timer_callback, NULL);
}

static void deinit(void) {
    window_destroy(window);
    accel_data_service_unsubscribe();
    app_timer_cancel(timer);
}

int main(void) {
    init();

    APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

    app_event_loop();
    deinit();
}

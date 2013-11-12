#include <pebble.h>

#define TIMEOUT 500
#define DEBUG_LEN 31
#define HISTORY 10

/****************************************************************************
 * TYPES
 */

// Tick
typedef struct {
    AccelData accel;
} Tick;


// GameState
// intended to have one global of this, storing all the game state
// the idea is that now you can autocomplete any global state
// by typing gamestate.<words>
typedef struct {
    unsigned int is_testing: 1; // bool - should we be recoding data for a developer test?
    unsigned int test_number;   // count - for test logging

    unsigned int tick;          // how many game ticks have elapsed
    Tick history[HISTORY];       // store data for the last HISTORY ticks

    // strings for displaying debug infos
    char print_accel[DEBUG_LEN];
    char print_delta[DEBUG_LEN];
    char print_test[DEBUG_LEN];
} GameState;


/*********************************************************************
 * Global state
 * windows, layers, and the all-important STATE
 */

static Window *window;
static TextLayer *delta_layer;
static TextLayer *magnitude_layer; // max needs tab completion
static AppTimer *timer;
static GameState state; // makes it easy to have more states or something
                        // if we need them, later...



/****************************************************************************
 * UTILS
 */

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

#define get_tick(ARRAY, CUR_TICK) \
    (ARRAY)[(CUR_TICK) % (HISTORY)]


// sets up the `state` global for a new game
void initialize_game_state() {
   state.is_testing = 0;
   state.is_testing = 0;
   state.tick = 0;

   for (int i = 0; i < HISTORY; i++) {
       state.history[i] = (Tick){.accel = {0,0,0}};
   }
   // ignore print_* because those are always written before read
}








/****************************************************************************
 * EVENT HANDLERS
 */
static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    state.is_testing ^= 1;
    if (state.is_testing) {
        state.test_number++;
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
//***************************************************************************



/* game loop 
 * - increment the tick
 * - save acceleration data
 * - calculate deltas
 * - perform logging if in test mode
 * */
static void timer_callback(void *context) {
    int deltax, deltay, deltaz, magnitude;
    Tick *prev_tick, *cur_tick;

    // advance the tick clock, which we use for most things here
    state.tick++;
    prev_tick = & get_tick(state.history, state.tick - 1);
    cur_tick  = & get_tick(state.history, state.tick);

    // get data change for current tick
    accel_service_peek(&(cur_tick->accel));

    // derived numbers
    deltax = cur_tick->accel.x - prev_tick->accel.x;
    deltay = cur_tick->accel.y - prev_tick->accel.y;
    deltaz = cur_tick->accel.z - prev_tick->accel.z;

    magnitude = butt_rt(deltax*deltax + deltay*deltay + deltaz*deltaz);

    // output to watch
    snprintf(state.print_delta, DEBUG_LEN, "X:%d Y:%d Z:%d", deltax, deltay, deltaz);
    snprintf(state.print_test, DEBUG_LEN, "a:%d, t:%d, M:%d", state.is_testing, state.test_number, magnitude);
    text_layer_set_text(delta_layer, state.print_delta);
    text_layer_set_text(magnitude_layer, state.print_test);
    //layer_mark_dirty(window_get_root_layer(window));

    // output to log
    if (state.is_testing) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", state.print_test);

        if (magnitude > 2000) {
            vibes_double_pulse();
        }
    }

    // schedule next tick
    const uint32_t timeout_ms = TIMEOUT;
    timer = app_timer_register(timeout_ms, timer_callback, NULL);
}

// mysteriously required by `accel_data_service_subscribe`
// it errors on NULL, so we pass this do-nothing function instead.
static void handle_accel(AccelData *accel_data, uint32_t num_samples) {
    // do nothing
}


/* app lifecycle ************************************************************
 * pebble init and cleanup
 */
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

    // initialize game state
    initialize_game_state();

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

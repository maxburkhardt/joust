#include <pebble.h>
#include "line.h"

#define TICK_MS 500
#define DEBUG_LEN 31
#define HISTORY 10
#define DEBUG_LAYERS 3
#define SONG_GRAPH_HEIGHT 70


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
    Tick history[HISTORY];      // store data for the last HISTORY ticks
    unsigned int song_length_ticks;

    // strings for displaying debug infos
    char print_accel[DEBUG_LEN];
    char print_delta[DEBUG_LEN];
    char print_test[DEBUG_LEN];
} GameState;


/*********************************************************************
 * GLOBAL STATE
 * windows, layers, and the all-important STATE
 */


static Window *window;
static Layer *song_layer;
static TextLayer* debug_layers[DEBUG_LAYERS];
static AppTimer *timer;
static GameState state; // makes it easy to have more states or something
                        // if we need them, later...

static Line song = {6, {
    {0, 1600}, {30 * 1000, 1600}, {31 * 1000, 3000}, {60 * 1000, 3000},
        {61 * 1000, 1300}, {120 * 1000, 1600}
}};

static GPath *song_path;
static GPathInfo *song_path_info;


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
   state.song_length_ticks = song.points[song.length-1].x / TICK_MS;
   for (int i = 0; i < HISTORY; i++) {
       state.history[i] = (Tick){.accel = {0,0,0}};
   }
   // ignore print_* because those are always written before read
}


// convert between seconds and ticks in a line
// destructive, in-place edit
void line_convert_ms_to_ticks(Line *line) {
    for (int i = 0; i < line->length; i++)
        line->points[i].x /= TICK_MS;
}


// print each point of a GPathInfo struct
void log_gpathinfo(GPathInfo *path_info) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "GPathInfo[%d]", (int)path_info->num_points);
    for (unsigned int i=0; i<path_info->num_points; i++)
        APP_LOG(APP_LOG_LEVEL_DEBUG, "GPathInfo[%d] = {%d, %d}", i,
                path_info->points[i].x, path_info->points[i].y);
}




/***************************************************************************
 * GAME LOOP
 * - increment the tick
 * - save acceleration data
 * - calculate deltas
 * - perform logging if in test mode
 */
static void timer_callback(void *context) {
    int deltax, deltay, deltaz, magnitude, max_mag;
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
    max_mag = line_eval(&song, state.tick);

    // output to watch
    snprintf(state.print_delta, DEBUG_LEN, "X:%d Y:%d Z:%d", deltax, deltay, deltaz);
    snprintf(state.print_test, DEBUG_LEN, "a:%d, t:%d, M:%d", state.is_testing, state.test_number, magnitude);
    snprintf(state.print_accel, DEBUG_LEN, "tick:%d, max:%d", state.tick, max_mag);
    text_layer_set_text(debug_layers[0], state.print_delta);
    text_layer_set_text(debug_layers[1], state.print_test);
    text_layer_set_text(debug_layers[2], state.print_accel);
    //layer_mark_dirty(window_get_root_layer(window));

    // output to log
    if (state.is_testing) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "%s", state.print_test);

        if (magnitude > max_mag) {
            vibes_double_pulse();
        }
    }

    // schedule next tick
    const uint32_t timeout_ms = TICK_MS;
    timer = app_timer_register(timeout_ms, timer_callback, NULL);
}



/****************************************************************************
 * CALLBACKS
 */

static void song_layer_update_proc(Layer *layer, GContext *ctx) {
    static const uint16_t radius = 2; // values over 2 cause crash
    static const uint16_t height = 10;
    // song graph
    graphics_context_set_fill_color(ctx, GColorBlack);
    gpath_draw_filled(ctx, song_path);

    // current location in graph
    GRect bounds = layer_get_bounds(layer);
    float try = (float)(state.tick % state.song_length_ticks) * 
        ((float)bounds.size.w / (float)state.song_length_ticks);
    int x_offset = song_path->offset.x + try;

    GRect progress_bar = (GRect) {
        .origin = { bounds.origin.x, bounds.size.h - height },
        .size = { x_offset, height }
    };

    APP_LOG(APP_LOG_LEVEL_DEBUG, "pixel offset for tick %d=%d (%d), max=%d, b=%d", (int)state.tick, 
            x_offset, (int)try, state.song_length_ticks, (int)bounds.size.w);

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, progress_bar, radius, GCornerTopRight | GCornerBottomRight );
}

// mysteriously required by `accel_data_service_subscribe`
// it errors on NULL, so we pass this do-nothing function instead.
static void handle_accel(AccelData *accel_data, uint32_t num_samples) {
    // do nothing
}

/* button pushes **********************************************************/
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



/* game window **********************************************************/
static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    TextLayer *layer;

    // make some polygons
    song_path_info = line_to_gpathinfo(&song, bounds.size.w, SONG_GRAPH_HEIGHT, 10);
    if (song_path_info != NULL) {
        log_gpathinfo(song_path_info);
        song_path = gpath_create(song_path_info);
        gpath_move_to(song_path, GPoint(0, bounds.size.h - SONG_GRAPH_HEIGHT));

        song_layer = layer_create(bounds);
        layer_set_update_proc(song_layer, song_layer_update_proc);
        layer_add_child(window_layer, song_layer);
    }

    const int line_height = 20;

    for (int i = 0; i < DEBUG_LAYERS; i++) {
        layer = text_layer_create((GRect) { .origin = { 0, (i+1)*line_height }, .size = { bounds.size.w, line_height } });
        text_layer_set_text(layer, "delta");
        text_layer_set_text_alignment(layer, GTextAlignmentCenter);
        layer_add_child(window_layer, text_layer_get_layer(layer));

        debug_layers[i] = layer;
    }
}

static void window_unload(Window *window) {
    for (int i=0; i<DEBUG_LAYERS; i++)
        text_layer_destroy(debug_layers[i]);

    // this seems to cause explosions
    if (song_path_info != NULL) {
        free(song_path_info->points);
        free(song_path_info);

        gpath_destroy(song_path);
        layer_destroy(song_layer);
    }
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
    line_convert_ms_to_ticks(&song);


    const uint32_t timeout_ms = TICK_MS;
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

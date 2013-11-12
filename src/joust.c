#include <pebble.h>
#include <limits.h>

#define TIMEOUT 500
#define DEBUG_LEN 31
#define HISTORY 10
#define DEBUG_LAYERS 3

/****************************************************************************
 * TYPES
 */

// Line
typedef struct {
    int x;
    int y;
} Point;
typedef struct {
    int length;
    Point points[];
} Line;

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

static const GPathInfo dummy_path_info = {
    .num_points = 4,
    .points = (GPoint []) {{0,0}, {30, 0}, {30,30}, {0, 30}}
};

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

#define min(X, Y) (((X) < (Y)) ? (X) : (Y))
#define max(X, Y) (((X) > (Y)) ? (X) : (Y))

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


// evaluate a line as a function at an X
// x points outside the domain are transformed using modulo arithemetic so that they
// lie within the range
// TODO: test this
int line_eval(const Line *line, int x) {
    Point first, last, before, after;
    float m;

    // lines must have at least 2 points
    if (line->length < 2) return 0;

    first = line->points[0];
    last  = line->points[line->length - 1];

    // domain clipping
    if (x > last.x || x < first.x) {
        x = (x % (last.x - first.x)) + first.x;
        APP_LOG(APP_LOG_LEVEL_DEBUG, "x clipped to domain, x:%d", x);
    }

    // find the two points around x. O(n), i don't think bsearch would
    // be faster because stack stuff takes time
    for (int i = 1; i < line->length; i++) {
        if (line->points[i].x == x) return line->points[i].y;

        if (line->points[i].x > x) {
            after = line->points[i];
            break;
        }
    }
    for (int i = line->length - 2; i >= 0; i--) {
        if (line->points[i].x == x) return line->points[i].y;
        if (line->points[i].x < x) {
            before = line->points[i];
            break;
        }
    }

    // line
    m = (after.y - before.y)/(after.x - before.x);
    return (m*(x - before.x)) + before.y;
}


// convert between seconds and ticks in a line
// destructive, in-place edit
void line_convert_ms_to_ticks(Line *line) {
    for (int i = 0; i < line->length; i++)
        line->points[i].x /= TIMEOUT;
}

// x is min, y is max
Point line_minmax_x(const Line *line) {
    return (Point) {
        .x = line->points[0].x,
        .y = line->points[line->length - 1].x
    };
}
Point line_minmax_y(const Line *line) {
    Point minmax = {INT_MAX, INT_MIN};
    for (int i = 0; i < line->length; i++) {
        minmax.x = min(minmax.x, line->points[i].x);
        minmax.y = max(minmax.y, line->points[i].y);
    }
    return minmax;
}

/* fit a line into an area of width*(height-plus_y) and then fill out the box with
 * an extra plus_y pixels at the bottom:
 *       ____
 *    __/    \__    
 * __/          \__ 
 * 
 * to
 *    ____
 * __/    \__
 * |________| (this boxy bit is the plus_y)
 */
GPathInfo* line_to_gpathinfo(const Line *line, int width, int height, int plus_y) {
    GPathInfo *out = malloc(sizeof(GPathInfo));
    GPoint *out_points = malloc(sizeof(GPoint) * line->length + 2);

    out->num_points = line->length+2;
    out->points = out_points;

    Point minmax_x = line_minmax_x(line);
    Point minmax_y = line_minmax_y(line);

    APP_LOG(APP_LOG_LEVEL_DEBUG, "xmin %d, xmax %d, ymin %d, ymax %d", minmax_x.x, minmax_x.y, 
            minmax_y.x, minmax_y.y);

    float transform_x = width / (minmax_x.y - minmax_x.x);
    float transform_y = (height - plus_y) / (minmax_y.y - minmax_y.x);

    out_points[0] = (GPoint) {0, 0};
    // map each point to a new GPoint
    for (int i=0; i<line->length; i++) {
        out_points[i+1] = (GPoint) {
            (float)line->points[i].x * transform_x,
            ((float)line->points[i].y * transform_y) + plus_y
        };
    }
    out_points[line->length] = (GPoint) {width, 0};

    return out;
}

void log_gpathinfo(GPathInfo *path_info) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "GPathInfo[%d]", (int)path_info->num_points);
    for (unsigned int i=0; i<path_info->num_points; i++)
        APP_LOG(APP_LOG_LEVEL_DEBUG, "GPathInfo[%d] = {%d, %d}", i,
                path_info->points[i].x, path_info->points[i].y);
}



/****************************************************************************
 * EVENT HANDLERS
 */

static void song_layer_update_proc(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    gpath_draw_filled(ctx, song_path);
}

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
    TextLayer *layer;

    // make some polygons
    song_path_info = line_to_gpathinfo(&song, bounds.size.w, 50, 10);
    log_gpathinfo(song_path_info);
    //song_path = gpath_create(&herpaderp);
    //gpath_move_to(song_path, GPoint(10, bounds.size.h - 30));

    /*song_layer = layer_create(bounds);*/
    /*layer_set_update_proc(song_layer, song_layer_update_proc);*/
    /*layer_add_child(window_layer, song_layer);*/

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
    free(song_path_info->points);
    // free(song_path_info);

    gpath_destroy(song_path);
    layer_destroy(song_layer);
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
    line_convert_ms_to_ticks(&song);


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

#include <limits.h>
#include "line.h"
#define min(X, Y) (((X) < (Y)) ? (X) : (Y))
#define max(X, Y) (((X) > (Y)) ? (X) : (Y))

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
 *
 * Allocates memory! You must free(out->points) and free(out);
 */
GPathInfo* line_to_gpathinfo(const Line *line, int width, int height, int plus_y) {
    GPathInfo *out;
    GPoint    *out_points;

    // allocations. be vewwy vewwy quiet, I'm huwnting memowy
    out = malloc(sizeof(GPathInfo));
    if (out == NULL) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "line_to_gpathinfo: Could not allocate out");
        return NULL;
    }

    out_points = malloc(sizeof(GPoint) * (line->length+2));
    if (out_points == NULL) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "line_to_gpathinfo: Could not allocate out_points");
        free(out);
        return NULL;
    }

    out->num_points = line->length+2;
    out->points = out_points;

    Point minmax_x = line_minmax_x(line);
    Point minmax_y = line_minmax_y(line);

    float transform_x = (float)width / (float)(minmax_x.y - minmax_x.x);
    float transform_y = (float)(height - plus_y) / (float)(minmax_y.y - minmax_y.x);

    APP_LOG(APP_LOG_LEVEL_DEBUG, "xmin %d, xmax %d, ymin %d, ymax %d, tx %f, ty %f", minmax_x.x, minmax_x.y, 
            minmax_y.x, minmax_y.y, transform_x, transform_y);

    out_points[0] = (GPoint) {0, height};
    // map each point to a new GPoint
    // remember that the Pebble screen system is addressed from the top-left
    for (int i=0; i < line->length; i++) {
        out_points[i+1] = (GPoint) {
            ((float)line->points[i].x) * transform_x,
            height - (((float)line->points[i].y) * transform_y)
        };
    }
    out_points[line->length+1] = (GPoint) {width, height};

    return out;
}

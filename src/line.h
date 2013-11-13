#include <pebble.h>
/**
 * line.h
 * Very similar to a GPoint[], but with int values  Possibly not even needed.
 * pre-definition of line.c
 */


typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    int length;
    Point points[];
} Line;


int line_eval(const Line *line, int x);

Point line_minmax_y(const Line *line);
Point line_minmax_x(const Line *line);

GPathInfo* line_to_gpathinfo(const Line *line, int width, int height, int plus_y);

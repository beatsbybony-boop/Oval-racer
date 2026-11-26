#ifndef OR_TRACK_H
#define OR_TRACK_H

#include "vec.h"
#include "vehicle.h"

typedef enum { SURF_ASPHALT = 0, SURF_DIRT } SurfaceType;
typedef enum { SHAPE_OVAL = 0, SHAPE_FIG8 } TrackShape;

/*
 * Two track shapes, both centred on the origin.
 *
 * SHAPE_OVAL: stadium shape, two straights of length `straight` joined by
 * half-circles of radius `radius`. Cars run counter-clockwise.
 *
 * SHAPE_FIG8: two loops of radius `radius` centred at x = +/-`straight`
 * (here `straight` is the centre offset, and must exceed `radius`), joined
 * by two straights that cross at the origin. Cars run counter-clockwise
 * round the right loop and clockwise round the left one, so the cars cross
 * the middle at a steep angle.
 *
 * Lateral offset is measured from the centreline, positive to the RIGHT of
 * the direction of travel. On the oval that is toward the outside wall.
 */
typedef struct {
    const char *name;
    SurfaceType surface;
    float straight;   /* oval: straight length (m). fig8: loop centre offset (m) */
    float radius;     /* m, centreline */
    float width;      /* m, racing surface */
    float bank;       /* rad, applied in the turns (keep 0 for figure 8) */
    float dirt_tack;  /* 0 slick .. 1 tacky, dirt only */
    float groove;     /* preferred line, -1 inside .. +1 outside, dirt only */
    TrackShape shape;
} Track;

/* Where a point sits relative to the track. */
typedef struct {
    float s;         /* distance along the centreline, 0 .. length */
    float lateral;   /* m, + to the right of travel */
    Vec2  centre;    /* nearest centreline point */
    Vec2  tangent;   /* direction of travel there */
    Vec2  outward;   /* unit vector to the right of travel */
    int   in_turn;   /* 1 if in a turn, 0 on a straight */
    int   wall_sign; /* +1 wall on the right, -1 wall on the left, 0 no wall here */
} TrackPos;

#define TRACK_COUNT 6
const Track *track_preset(int index);

float track_length(const Track *t);
/* Global nearest-point search. */
void  track_locate(const Track *t, Vec2 p, TrackPos *out);
/*
 * Same, but for a car whose last known position along the track is hint_s.
 * On a figure 8 this is what tells the two crossing straights apart, and it
 * is also much cheaper. Pass a negative hint to search everywhere.
 */
void  track_locate_hint(const Track *t, Vec2 p, float hint_s, TrackPos *out);
/* Point at distance s along the centreline, offset sideways (+ right). */
Vec2  track_point(const Track *t, float s, float lateral, Vec2 *tangent_out);
/* Surface grip at a lateral offset. */
Grip  track_grip(const Track *t, float lateral);
/* Distance from centreline to the wall. */
float track_wall(const Track *t);
/* Which side the wall is on at distance s (+1 right, -1 left, 0 none). */
int   track_wall_sign(const Track *t, float s);
/* Turn radius for a car in lane `lane` at distance s; <= 0 on a straight. */
float track_corner_radius(const Track *t, float s, float lane);
/* Distance along the centreline of the start/finish line. */
float track_start_s(const Track *t);
/* Hard rectangular boundary for shapes with open ground around them. 0 if none. */
int   track_bounds(const Track *t, float *half_w, float *half_h);
/* Banking effect for a car at tp moving at `speed`: normal load scale and gravity pull. */
void  track_bank_effect(const Track *t, const TrackPos *tp, float speed, float *load_scale, Vec2 *accel);

#endif

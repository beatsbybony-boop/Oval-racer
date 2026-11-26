#ifndef OR_VEC_H
#define OR_VEC_H

#include <math.h>

#define OR_PI 3.14159265358979f
#define OR_G  9.81f
#define OR_RHO 1.2f

typedef struct { float x, y; } Vec2;

static inline Vec2 v2(float x, float y) { Vec2 v = { x, y }; return v; }
static inline Vec2 v2_add(Vec2 a, Vec2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline Vec2 v2_sub(Vec2 a, Vec2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline Vec2 v2_scale(Vec2 a, float s) { return v2(a.x * s, a.y * s); }
static inline float v2_dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
static inline float v2_cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
static inline float v2_len(Vec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static inline Vec2 v2_norm(Vec2 a)
{
    float l = v2_len(a);
    return l > 1e-6f ? v2_scale(a, 1.0f / l) : v2(1.0f, 0.0f);
}
/* Rotate a vector by angle (radians, counter-clockwise). */
static inline Vec2 v2_rot(Vec2 a, float ang)
{
    float c = cosf(ang), s = sinf(ang);
    return v2(a.x * c - a.y * s, a.x * s + a.y * c);
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float signf(float v) { return v >= 0.0f ? 1.0f : -1.0f; }
static inline float wrap_pi(float a)
{
    while (a > OR_PI) a -= 2.0f * OR_PI;
    while (a < -OR_PI) a += 2.0f * OR_PI;
    return a;
}

#endif

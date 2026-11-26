#include <stddef.h>
#include "track.h"

/* name, surface, straight/offset, radius, width, bank, tack, groove, shape */
static const Track presets[TRACK_COUNT] = {
    { "Pine Ridge 1/4 Asphalt",     SURF_ASPHALT, 59.5f, 45.f, 13.f, 0.21f, 0.f,  0.f,  SHAPE_OVAL },
    { "Thunder Hill 1/3 Asphalt",   SURF_ASPHALT, 111.f, 50.f, 14.f, 0.26f, 0.f,  0.f,  SHAPE_OVAL },
    { "Red Clay 3/8 Dirt",          SURF_DIRT,    129.5f,55.f, 16.f, 0.20f, 0.6f, 0.2f, SHAPE_OVAL },
    { "Dusty Bullring 1/5 Dirt",    SURF_DIRT,    51.f,  35.f, 12.f, 0.15f, 0.5f, -0.1f,SHAPE_OVAL },
    { "Cross Creek Figure 8 Dirt",  SURF_DIRT,    62.f,  38.f, 14.f, 0.0f,  0.5f, 0.0f, SHAPE_FIG8 },
    { "Speedway Figure 8 Asphalt",  SURF_ASPHALT, 72.f,  44.f, 15.f, 0.0f,  0.f,  0.0f, SHAPE_FIG8 },
};

/* ------------------------------------------------------------------ */
/* Figure 8                                                           */
/* ------------------------------------------------------------------ */

#define F8_MAX_SAMPLES 1400
#define F8_WINDOW 40

/*
 * Path layout, with L = sqrt(d^2 - R^2) and theta = acos(-R/d):
 *   seg 1  line O -> Pe                    length L
 *   seg 2  right loop, CCW, -theta..+theta length 2*theta*R
 *   seg 3  line Px -> -Px (through O)      length 2L
 *   seg 4  left loop, CW                   length 2*theta*R
 *   seg 5  line -Pe -> O                   length L
 */
typedef struct {
    int   built;
    int   n;
    float len, L, theta, ds;
    Vec2  p[F8_MAX_SAMPLES];
    Vec2  t[F8_MAX_SAMPLES];
    signed char ws[F8_MAX_SAMPLES];
} Fig8;

static Fig8 f8cache[TRACK_COUNT];

static void f8_eval(const Track *t, float L, float theta, float s,
                    Vec2 *pos, Vec2 *tan, int *ws)
{
    const float d = t->straight, R = t->radius;
    const float arc = 2.0f * theta * R;
    const float total = 4.0f * L + 2.0f * arc;
    Vec2 pe = v2(d - R * R / d, -R * sinf(theta));       /* tangent point, lower left of right loop */
    Vec2 px = v2(pe.x, -pe.y);
    Vec2 ua = v2_scale(pe, 1.0f / L);
    Vec2 ub = v2_scale(px, -1.0f / L);
    float phi;

    s = fmodf(s, total);
    if (s < 0.0f) s += total;

    if (s < L) {
        *pos = v2_scale(ua, s); *tan = ua; *ws = 0;
    } else if (s < L + arc) {
        phi = -theta + (s - L) / R;
        *pos = v2(d + R * cosf(phi), R * sinf(phi));
        *tan = v2(-sinf(phi), cosf(phi));
        *ws = 1;
    } else if (s < 3.0f * L + arc) {
        *pos = v2_add(px, v2_scale(ub, s - (L + arc)));
        *tan = ub; *ws = 0;
    } else if (s < 3.0f * L + 2.0f * arc) {
        phi = OR_PI + theta - (s - (3.0f * L + arc)) / R;
        *pos = v2(-d + R * cosf(phi), R * sinf(phi));
        *tan = v2(sinf(phi), -cosf(phi));
        *ws = -1;
    } else {
        *pos = v2_add(v2_scale(pe, -1.0f), v2_scale(ua, s - (3.0f * L + 2.0f * arc)));
        *tan = ua; *ws = 0;
    }
}

static Fig8 *f8_get(const Track *t)
{
    int idx = (int)(t - presets);
    Fig8 *f;
    int i;

    if (idx < 0 || idx >= TRACK_COUNT) idx = 0;
    f = &f8cache[idx];
    if (f->built) return f;

    f->L = sqrtf(t->straight * t->straight - t->radius * t->radius);
    f->theta = acosf(-t->radius / t->straight);
    f->len = 4.0f * f->L + 4.0f * f->theta * t->radius;
    f->n = (int)(f->len);                         /* about one sample per metre */
    if (f->n > F8_MAX_SAMPLES) f->n = F8_MAX_SAMPLES;
    f->ds = f->len / (float)f->n;
    for (i = 0; i < f->n; i++) {
        int ws;
        f8_eval(t, f->L, f->theta, (float)i * f->ds, &f->p[i], &f->t[i], &ws);
        f->ws[i] = (signed char)ws;
    }
    f->built = 1;
    return f;
}

static void f8_locate(const Track *t, Vec2 p, float hint_s, TrackPos *out)
{
    Fig8 *f = f8_get(t);
    int best = 0, i, k, lo, hi;
    float bd = 1e30f;
    Vec2 d, rn;

    if (hint_s >= 0.0f) {
        int i0 = (int)(hint_s / f->ds);
        for (k = -F8_WINDOW; k <= F8_WINDOW; k++) {
            float dd;
            i = ((i0 + k) % f->n + f->n) % f->n;
            d = v2_sub(p, f->p[i]);
            dd = d.x * d.x + d.y * d.y;
            if (dd < bd) { bd = dd; best = i; }
        }
        /* Thrown a long way off the line: fall back to a full search. */
        if (bd < (t->width + 12.0f) * (t->width + 12.0f)) { lo = 1; hi = 0; } else { lo = 0; hi = f->n; }
    } else { lo = 0; hi = f->n; }
    if (lo == 0) {
        bd = 1e30f;
        for (i = 0; i < hi; i++) {
            float dd;
            d = v2_sub(p, f->p[i]);
            dd = d.x * d.x + d.y * d.y;
            if (dd < bd) { bd = dd; best = i; }
        }
    }

    d = v2_sub(p, f->p[best]);
    rn = v2(f->t[best].y, -f->t[best].x);
    out->s = (float)best * f->ds + v2_dot(d, f->t[best]);
    if (out->s < 0.0f) out->s += f->len;
    if (out->s >= f->len) out->s -= f->len;
    out->lateral = v2_dot(d, rn);
    out->centre = f->p[best];
    out->tangent = f->t[best];
    out->outward = rn;
    out->wall_sign = f->ws[best];
    out->in_turn = f->ws[best] != 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

const Track *track_preset(int index)
{
    if (index < 0 || index >= TRACK_COUNT) index = 0;
    return &presets[index];
}

float track_length(const Track *t)
{
    if (t->shape == SHAPE_FIG8) return f8_get(t)->len;
    return 2.0f * t->straight + 2.0f * OR_PI * t->radius;
}

float track_wall(const Track *t)
{
    return t->width * 0.5f + 0.5f;
}

float track_start_s(const Track *t)
{
    if (t->shape == SHAPE_FIG8) return f8_get(t)->L;      /* just before the right loop */
    return t->straight * 0.5f;
}

int track_bounds(const Track *t, float *half_w, float *half_h)
{
    if (t->shape != SHAPE_FIG8) return 0;
    *half_w = t->straight + t->radius + t->width * 0.5f + 4.0f;
    *half_h = t->radius + t->width * 0.5f + 4.0f;
    return 1;
}

int track_wall_sign(const Track *t, float s)
{
    if (t->shape == SHAPE_FIG8) {
        Fig8 *f = f8_get(t);
        int i = ((int)(s / f->ds) % f->n + f->n) % f->n;
        return f->ws[i];
    }
    return 1;
}

float track_corner_radius(const Track *t, float s, float lane)
{
    if (t->shape == SHAPE_FIG8) {
        int ws = track_wall_sign(t, s);
        return ws == 0 ? 0.0f : t->radius + (float)ws * lane;
    }
    {
        float len = track_length(t);
        float L = t->straight, R = t->radius;
        s = fmodf(s, len);
        if (s < 0.0f) s += len;
        if ((s >= L && s < L + OR_PI * R) || s >= 2.0f * L + OR_PI * R) return R + lane;
        return 0.0f;
    }
}

void track_locate_hint(const Track *t, Vec2 p, float hint_s, TrackPos *out)
{
    const float L = t->straight, R = t->radius;
    const float half = L * 0.5f;

    if (t->shape == SHAPE_FIG8) { f8_locate(t, p, hint_s, out); return; }

    out->wall_sign = 1;
    if (p.x > half || p.x < -half) {
        /* In one of the two turns. */
        int right = p.x > half;
        Vec2 c = v2(right ? half : -half, 0.0f);
        Vec2 d = v2_sub(p, c);
        float r = v2_len(d);
        float th = atan2f(d.y, d.x);
        if (!right && th < 0.0f) th += 2.0f * OR_PI;
        if (right)
            out->s = L + R * (th + OR_PI * 0.5f);
        else
            out->s = 2.0f * L + OR_PI * R + R * (th - OR_PI * 0.5f);
        out->lateral = r - R;
        out->outward = r > 1e-4f ? v2_scale(d, 1.0f / r) : v2(1.0f, 0.0f);
        out->centre = v2_add(c, v2_scale(out->outward, R));
        out->tangent = v2(-out->outward.y, out->outward.x);
        out->in_turn = 1;
    } else if (p.y < 0.0f) {
        /* Bottom straight, heading +x. */
        out->s = p.x + half;
        out->lateral = -p.y - R;
        out->outward = v2(0.0f, -1.0f);
        out->centre = v2(p.x, -R);
        out->tangent = v2(1.0f, 0.0f);
        out->in_turn = 0;
    } else {
        /* Top straight, heading -x. */
        out->s = L + OR_PI * R + (half - p.x);
        out->lateral = p.y - R;
        out->outward = v2(0.0f, 1.0f);
        out->centre = v2(p.x, R);
        out->tangent = v2(-1.0f, 0.0f);
        out->in_turn = 0;
    }
}

void track_locate(const Track *t, Vec2 p, TrackPos *out)
{
    track_locate_hint(t, p, -1.0f, out);
}

Vec2 track_point(const Track *t, float s, float lateral, Vec2 *tangent_out)
{
    const float L = t->straight, R = t->radius;
    const float len = track_length(t);
    Vec2 centre, tangent, outward;

    s = fmodf(s, len);
    if (s < 0.0f) s += len;

    if (t->shape == SHAPE_FIG8) {
        Fig8 *f = f8_get(t);
        int ws;
        f8_eval(t, f->L, f->theta, s, &centre, &tangent, &ws);
        outward = v2(tangent.y, -tangent.x);          /* right of travel */
        if (tangent_out) *tangent_out = tangent;
        return v2_add(centre, v2_scale(outward, lateral));
    }

    if (s < L) {
        centre = v2(-L * 0.5f + s, -R);
        tangent = v2(1.0f, 0.0f);
        outward = v2(0.0f, -1.0f);
    } else if (s < L + OR_PI * R) {
        float th = -OR_PI * 0.5f + (s - L) / R;
        outward = v2(cosf(th), sinf(th));
        centre = v2_add(v2(L * 0.5f, 0.0f), v2_scale(outward, R));
        tangent = v2(-outward.y, outward.x);
    } else if (s < 2.0f * L + OR_PI * R) {
        float u = s - (L + OR_PI * R);
        centre = v2(L * 0.5f - u, R);
        tangent = v2(-1.0f, 0.0f);
        outward = v2(0.0f, 1.0f);
    } else {
        float th = OR_PI * 0.5f + (s - (2.0f * L + OR_PI * R)) / R;
        outward = v2(cosf(th), sinf(th));
        centre = v2_add(v2(-L * 0.5f, 0.0f), v2_scale(outward, R));
        tangent = v2(-outward.y, outward.x);
    }
    if (tangent_out) *tangent_out = tangent;
    return v2_add(centre, v2_scale(outward, lateral));
}

Grip track_grip(const Track *t, float lateral)
{
    Grip g;
    float half = t->width * 0.5f;

    g.stiff = 14.0f;
    if (lateral < -half) { g.mu = 0.45f; g.rolling = 0.08f; g.stiff = 7.0f; return g; }   /* infield grass */
    if (lateral > half)  { g.mu = 0.50f; g.rolling = 0.06f; g.stiff = 7.0f; return g; }   /* outside apron */

    if (t->surface == SURF_ASPHALT) {
        g.mu = 1.0f;
        g.rolling = 0.015f;
    } else {
        float base = 0.50f + 0.20f * t->dirt_tack;
        float d = (lateral - t->groove * half) / (0.3f * t->width);
        g.mu = base + 0.14f * expf(-d * d);
        g.rolling = 0.045f;
        g.stiff = 8.0f;
    }
    return g;
}

void track_bank_effect(const Track *t, const TrackPos *tp, float speed, float *load_scale, Vec2 *accel)
{
    *load_scale = 1.0f;
    *accel = v2(0.0f, 0.0f);
    if (!tp->in_turn || t->bank <= 0.0f || t->shape != SHAPE_OVAL) return;

    {
        float reff = fmaxf(t->radius + tp->lateral, 10.0f);
        float sb = sinf(t->bank);
        /* Normal load: weight component plus centripetal component. */
        *load_scale = cosf(t->bank) + (speed * speed / reff) / OR_G * sb;
        /* Gravity pulls the car down the slope, toward the infield. */
        *accel = v2_scale(tp->outward, -OR_G * sb);
    }
}

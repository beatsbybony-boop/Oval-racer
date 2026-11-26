#include "game.h"

/*
 * Each car is a row of circles along its length. Contacts use impulses with
 * restitution that drops as the body gets crushed, plus Coulomb friction along
 * the contact (so cars scrub, spin each other and drag along walls). The energy
 * a car loses is turned into zoned damage at the contact point (see
 * car_apply_damage), and big side hits can roll tall vehicles.
 */

typedef struct { Vec2 p; float r; } Circle;

static int hull_circles(const Car *c, Circle *out)
{
    const VehicleSpec *s = c->spec;
    float r = s->width * 0.5f;
    int n = (int)ceilf(s->length / s->width);
    float centre_off = (s->cg_front - (s->wheelbase - s->cg_front)) * 0.5f;
    float span = s->length * 0.5f - r;
    Vec2 fwd = car_forward(c);
    int i;

    if (n < 2) n = 2;
    if (n > MAX_HULL_CIRCLES) n = MAX_HULL_CIRCLES;
    for (i = 0; i < n; i++) {
        float t = (float)i / (float)(n - 1);            /* 0..1 rear to front */
        float along = centre_off + (-span + 2.0f * span * t);
        out[i].p = v2_add(c->pos, v2_scale(fwd, along));
        out[i].r = r;
    }
    return n;
}

static Vec2 point_velocity(const Car *c, Vec2 rv)
{
    Vec2 v = car_velocity(c);
    return v2_add(v, v2_scale(v2(-rv.y, rv.x), c->yaw_rate));
}

/* Apply a world-frame impulse at offset rv from the CG. */
static void apply_impulse(Car *c, Vec2 imp, Vec2 rv)
{
    const VehicleSpec *s = c->spec;
    Vec2 v = v2_add(car_velocity(c), v2_scale(imp, 1.0f / s->mass));
    c->vx = v2_dot(v, car_forward(c));
    c->vy = v2_dot(v, car_left(c));
    c->yaw_rate += v2_cross(rv, imp) / s->yaw_inertia;
}

static Vec2 to_local(const Car *c, Vec2 world)
{
    Vec2 rel = v2_sub(world, c->pos);
    return v2(v2_dot(rel, car_forward(c)), v2_dot(rel, car_left(c)));
}

static void add_damage(Game *g, Racer *rc, Vec2 contact, float energy)
{
    car_apply_damage(&rc->car, to_local(&rc->car, contact), energy, g->damage_scale);
    if (energy > 1500.0f) rc->hit_timer = 0.0f;
}

/*
 * A hard hit from the side can tip a car. dv is the speed change it got along
 * the hit direction; tall vehicles go over at a much smaller shove.
 */
static void maybe_roll(Car *c, Vec2 hit_dir, float dv)
{
    float side = fabsf(v2_dot(hit_dir, car_left(c)));
    float thresh = c->spec->cg_h > 1.0f ? 5.0f : 11.0f;
    if (side > 0.7f && dv * side > thresh) car_start_roll(c);
}

static void car_vs_car(Game *g, Racer *A, Racer *B)
{
    Circle ca[MAX_HULL_CIRCLES], cb[MAX_HULL_CIRCLES];
    int na, nb, i, j;
    float reach = (A->car.spec->length + B->car.spec->length) * 0.5f;

    if (v2_len(v2_sub(A->car.pos, B->car.pos)) > reach + 1.0f) return;
    na = hull_circles(&A->car, ca);
    nb = hull_circles(&B->car, cb);

    for (i = 0; i < na; i++) {
        for (j = 0; j < nb; j++) {
            Vec2 d = v2_sub(cb[j].p, ca[i].p);
            float dist = v2_len(d);
            float pen = ca[i].r + cb[j].r - dist;
            if (pen <= 0.0f) continue;

            {
                Vec2 n = dist > 1e-4f ? v2_scale(d, 1.0f / dist) : v2(1.0f, 0.0f);
                Vec2 contact = v2_add(ca[i].p, v2_scale(n, ca[i].r - pen * 0.5f));
                Vec2 ra = v2_sub(contact, A->car.pos);
                Vec2 rb = v2_sub(contact, B->car.pos);
                float invA = 1.0f / A->car.spec->mass, invB = 1.0f / B->car.spec->mass;
                float IA = A->car.spec->yaw_inertia, IB = B->car.spec->yaw_inertia;
                float tot = invA + invB;
                Vec2 rel;
                float vn;

                /* Push the cars apart in proportion to how light they are. */
                A->car.pos = v2_sub(A->car.pos, v2_scale(n, pen * 0.8f * invA / tot));
                B->car.pos = v2_add(B->car.pos, v2_scale(n, pen * 0.8f * invB / tot));
                na = hull_circles(&A->car, ca);
                nb = hull_circles(&B->car, cb);

                rel = v2_sub(point_velocity(&B->car, rb), point_velocity(&A->car, ra));
                vn = v2_dot(rel, n);
                if (vn >= 0.0f) continue;

                {
                    /* Crushed metal doesn't bounce, and neither does a gentle nudge. */
                    float crush = 0.5f * (car_crush_at(&A->car, to_local(&A->car, contact)) +
                                          car_crush_at(&B->car, to_local(&B->car, contact)));
                    float e = (-vn < 1.0f) ? 0.0f : 0.22f * (1.0f - 0.7f * crush);
                    float cA = v2_cross(ra, n), cB = v2_cross(rb, n);
                    float denom = tot + cA * cA / IA + cB * cB / IB;
                    float jn = -(1.0f + e) * vn / denom;
                    Vec2 imp = v2_scale(n, jn);
                    float energy = 0.5f * jn * -vn;

                    apply_impulse(&A->car, v2_scale(imp, -1.0f), ra);
                    apply_impulse(&B->car, imp, rb);

                    /* Paint-trading: friction along the contact turns glancing hits into spins. */
                    rel = v2_sub(point_velocity(&B->car, rb), point_velocity(&A->car, ra));
                    {
                        Vec2 tv = v2_sub(rel, v2_scale(n, v2_dot(rel, n)));
                        float tl = v2_len(tv);
                        if (tl > 0.05f) {
                            Vec2 t = v2_scale(tv, 1.0f / tl);
                            float tA = v2_cross(ra, t), tB = v2_cross(rb, t);
                            float dt_ = tot + tA * tA / IA + tB * tB / IB;
                            float jt = fminf(tl / dt_, 0.4f * jn);
                            apply_impulse(&A->car, v2_scale(t, jt), ra);
                            apply_impulse(&B->car, v2_scale(t, -jt), rb);
                        }
                    }

                    /* The lighter car takes the bigger share of the damage. */
                    add_damage(g, A, contact, energy * invA / tot);
                    add_damage(g, B, contact, energy * invB / tot);
                    maybe_roll(&A->car, n, jn * invA);
                    maybe_roll(&B->car, n, jn * invB);
                }
            }
        }
    }
}

/* Hit a wall whose outward normal is `out`, penetrating by `pen`, at circle c. */
static void car_vs_wall(Game *g, Racer *R, Circle c, Vec2 out, float pen)
{
    Car *car = &R->car;
    Vec2 contact = v2_add(c.p, v2_scale(out, c.r));
    Vec2 rv = v2_sub(contact, car->pos);
    Vec2 vp;
    float vn;

    car->pos = v2_sub(car->pos, v2_scale(out, pen));       /* shove back inside */
    vp = point_velocity(car, rv);
    vn = v2_dot(vp, out);
    if (vn <= 0.0f) return;

    {
        float crush = car_crush_at(car, to_local(car, contact));
        float e = (vn < 1.0f) ? 0.0f : 0.2f * (1.0f - 0.7f * crush);
        float m = car->spec->mass, I = car->spec->yaw_inertia;
        float cr = v2_cross(rv, out);
        float denom = 1.0f / m + cr * cr / I;
        float jn = (1.0f + e) * vn / denom;
        Vec2 tang = v2_sub(vp, v2_scale(out, vn));
        float tl = v2_len(tang);
        float energy = 0.5f * jn * vn;

        apply_impulse(car, v2_scale(out, -jn), rv);
        if (tl > 0.01f) {
            /* Scraping along the wall bleeds speed. */
            float jt = fminf(0.4f * jn, tl * 0.5f / (1.0f / m));
            apply_impulse(car, v2_scale(tang, -jt / tl), rv);
        }
        add_damage(g, R, contact, energy);
        maybe_roll(car, out, jn / m);
    }
}

static void walls(Game *g, Racer *R)
{
    Circle cs[MAX_HULL_CIRCLES];
    int n = hull_circles(&R->car, cs), i;
    float bw = 0.0f, bh = 0.0f;
    int bounded = g->track ? track_bounds(g->track, &bw, &bh) : 0;

    for (i = 0; i < n; i++) {
        if (g->track) {
            TrackPos tp;
            float wall = track_wall(g->track);
            track_locate_hint(g->track, cs[i].p, R->prev_s, &tp);
            if (tp.wall_sign != 0) {
                float out_lat = (float)tp.wall_sign * tp.lateral;
                if (out_lat + cs[i].r > wall)
                    car_vs_wall(g, R, cs[i], v2_scale(tp.outward, (float)tp.wall_sign),
                                out_lat + cs[i].r - wall);
            }
            if (bounded) {
                float ex;
                ex = cs[i].p.x + cs[i].r - bw;
                if (ex > 0.0f) car_vs_wall(g, R, cs[i], v2(1.0f, 0.0f), ex);
                ex = -cs[i].p.x + cs[i].r - bw;
                if (ex > 0.0f) car_vs_wall(g, R, cs[i], v2(-1.0f, 0.0f), ex);
                ex = cs[i].p.y + cs[i].r - bh;
                if (ex > 0.0f) car_vs_wall(g, R, cs[i], v2(0.0f, 1.0f), ex);
                ex = -cs[i].p.y + cs[i].r - bh;
                if (ex > 0.0f) car_vs_wall(g, R, cs[i], v2(0.0f, -1.0f), ex);
            }
        } else {
            float ex;
            ex = cs[i].p.x + cs[i].r - g->arena_hw;
            if (ex > 0.0f) car_vs_wall(g, R, cs[i], v2(1.0f, 0.0f), ex);
            ex = -cs[i].p.x + cs[i].r - g->arena_hw;
            if (ex > 0.0f) car_vs_wall(g, R, cs[i], v2(-1.0f, 0.0f), ex);
            ex = cs[i].p.y + cs[i].r - g->arena_hh;
            if (ex > 0.0f) car_vs_wall(g, R, cs[i], v2(0.0f, 1.0f), ex);
            ex = -cs[i].p.y + cs[i].r - g->arena_hh;
            if (ex > 0.0f) car_vs_wall(g, R, cs[i], v2(0.0f, -1.0f), ex);
        }
        /* The hull may have moved; refresh the remaining circles. */
        n = hull_circles(&R->car, cs);
    }
}

void collide_all(Game *g)
{
    int i, j;
    /* Eliminated cars stay on the track as wrecks, so everyone collides. */
    for (i = 0; i < g->count; i++) {
        for (j = i + 1; j < g->count; j++)
            car_vs_car(g, &g->racers[i], &g->racers[j]);
        walls(g, &g->racers[i]);
    }
}

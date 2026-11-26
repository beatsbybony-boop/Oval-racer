#include <stddef.h>
#include "game.h"

#define AI_GRIP_USE 0.80f

/* The AI makes decisions at 30 Hz; the game loop reuses the last input in between. */
#define AI_DT (1.0f / 30.0f)

/* ------------------------------------------------------------------ */
/* Shared steering                                                    */
/* ------------------------------------------------------------------ */

/* Pure pursuit toward `aim`, returned as a -1..1 steering input. */
static float steer_toward(const Car *c, Vec2 aim, float lookahead)
{
    Vec2 local = v2_rot(v2_sub(aim, c->pos), -c->heading);
    float alpha = atan2f(local.y, local.x);
    float angle = atan2f(2.0f * c->spec->wheelbase * sinf(alpha), lookahead);
    float lock = car_steer_lock(c);
    return clampf(angle / lock, -1.0f, 1.0f);
}

/* ------------------------------------------------------------------ */
/* Race driving                                                       */
/* ------------------------------------------------------------------ */

/* Fastest speed the car can hold at distance s along the lane `lane`. */
static float corner_speed(const Track *t, const VehicleSpec *sp, float s, float lane)
{
    float mu, b, rr, v2c, v;
    int pass;

    rr = track_corner_radius(t, s, lane);
    if (rr <= 0.0f) return sp->top_speed;
    /*
     * Usable grip is well below the tire's peak: load sensitivity and lateral
     * weight transfer cost a fifth or so, and a tall vehicle runs out of
     * track width (it tips) before its tires let go.
     */
    mu = sp->mu_tire * track_grip(t, lane).mu * AI_GRIP_USE;
    mu = fminf(mu, 0.8f * vehicle_roll_g(sp));
    b = t->bank;
    v = 20.0f;
    for (pass = 0; pass < 2; pass++) {
        /* Banked-turn limit, with downforce raising effective grip at speed. */
        float m_eff = mu * (1.0f + 0.5f * OR_RHO * sp->cl_a * v * v / (sp->mass * OR_G));
        float num = sinf(b) + m_eff * cosf(b);
        float den = cosf(b) - m_eff * sinf(b);
        if (den < 0.08f) return sp->top_speed;
        v2c = rr * OR_G * num / den;
        v = sqrtf(v2c);
    }
    return fminf(v, sp->top_speed);
}

static void drive_race(Game *g, int idx)
{
    Racer *R = &g->racers[idx];
    Car *c = &R->car;
    const Track *t = g->track;
    const VehicleSpec *sp = c->spec;
    TrackPos tp;
    Grip grip = game_grip_at(g, c->pos, R->prev_s, &tp);
    float speed = car_speed(c);
    float half = t->width * 0.5f - 1.8f;
    float lane = clampf(R->lane, -half, half);
    float vt, mu_eff, decel;
    int i;
    CarInput in;

    in.steer = 0.0f; in.throttle = 0.0f; in.brake = 0.0f; in.reverse = 0;

    /* Pass or follow: look for a car just ahead in roughly our lane. */
    {
        float best = 30.0f;
        for (i = 0; i < g->count; i++) {
            TrackPos op;
            float ahead;
            if (i == idx) continue;
            track_locate_hint(t, g->racers[i].car.pos, g->racers[i].prev_s, &op);
            ahead = op.s - tp.s;
            if (ahead < -track_length(t) * 0.5f) ahead += track_length(t);
            if (ahead > track_length(t) * 0.5f) ahead -= track_length(t);
            if (ahead > 2.0f && ahead < best && fabsf(op.lateral - tp.lateral) < sp->width * 1.4f) {
                best = ahead;
                if (g->mode == MODE_DERBY && R->skill > 0.95f && g->racers[i].active) {
                    /* Derby bruiser: line up the car in front instead of passing it. */
                    lane = clampf(op.lateral, -half, half);
                } else {
                    lane = clampf(op.lateral + (tp.lateral < op.lateral ? -1.0f : 1.0f) * sp->width * 1.6f,
                                  -half, half);
                }
            }
        }
    }

    /* Target speed: slowest of the corners coming up, allowing for braking distance. */
    mu_eff = sp->mu_tire * grip.mu;
    decel = fminf(sp->brake_g, mu_eff) * OR_G * 0.5f;
    vt = sp->top_speed;
    {
        static const float looks[5] = { 0.0f, 15.0f, 30.0f, 50.0f, 75.0f };
        for (i = 0; i < 5; i++) {
            /* 0.9 keeps the AI a little below the true limit, where the tires stop biting. */
            float vc = corner_speed(t, sp, tp.s + looks[i], lane) * R->skill * 0.95f;
            float allowed = sqrtf(vc * vc + 2.0f * decel * looks[i]);
            if (allowed < vt) vt = allowed;
        }
    }
    if (R->finished) vt = fminf(vt, sp->top_speed * 0.5f);   /* cool-down lap */

    /* Steer at a point on our lane a little way ahead. */
    {
        float ld = clampf(0.5f * speed + 6.0f, 8.0f, 40.0f);
        Vec2 aim = track_point(t, tp.s + ld, lane, NULL);
        in.steer = steer_toward(c, aim, ld);
        /* Counter-steer into a slide: aim the wheels where the car is actually going. */
        in.steer = clampf(in.steer + clampf(2.0f * c->vy / fmaxf(fabsf(c->vx), 6.0f), -0.5f, 0.5f),
                          -1.0f, 1.0f);
    }

    {
        float err = vt - speed;
        if (err > 0.0f) {
            in.throttle = clampf(0.25f + err * 0.5f, 0.0f, 1.0f);
        } else if (err < -1.0f) {
            in.brake = clampf(-err * 0.15f, 0.0f, 1.0f);
        }
        /* Sliding sideways: ease off the power. */
        if (fabsf(c->vy) > 0.2f * fmaxf(fabsf(c->vx), 5.0f)) in.throttle *= 0.4f;
    }

    /* Facing the wrong way, or stuck: turn around / back out. */
    {
        float along = v2_dot(car_forward(c), tp.tangent);
        if (speed < 1.5f) R->stuck_time += AI_DT;
        else if (R->stuck_time < 2.0f) R->stuck_time = 0.0f;
        if (R->stuck_time >= 2.0f) {
            in.reverse = 1; in.throttle = 1.0f; in.brake = 0.0f;
            in.steer = -signf(v2_cross(car_forward(c), tp.tangent));
            R->stuck_time += AI_DT;
            if (R->stuck_time > 3.5f) R->stuck_time = 0.0f;
        } else if (along < -0.2f) {
            /* Pointing against traffic: swing the nose round. */
            in.steer = signf(v2_cross(car_forward(c), tp.tangent));
            in.throttle = 0.5f; in.brake = 0.0f;
        }
    }
    R->input = in;
}

/* ------------------------------------------------------------------ */
/* Demolition derby                                                   */
/* ------------------------------------------------------------------ */

static int pick_victim(Game *g, int idx)
{
    int i, best = -1;
    float bd = 1e9f;
    for (i = 0; i < g->count; i++) {
        float d;
        if (i == idx || !g->racers[i].active) continue;
        d = v2_len(v2_sub(g->racers[i].car.pos, g->racers[idx].car.pos));
        /* Mix in a little randomness so the whole field doesn't gang up on one car. */
        d *= 0.8f + 0.4f * game_rand(g);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void drive_derby(Game *g, int idx)
{
    Racer *R = &g->racers[idx];
    Car *c = &R->car;
    CarInput in;
    Vec2 aim, centre = v2(0.0f, 0.0f);
    float speed = car_speed(c);
    float dist_wall, dist = 0.0f;
    Vec2 fwd = car_forward(c);

    in.steer = 0.0f; in.throttle = 0.0f; in.brake = 0.0f; in.reverse = 0;

    R->retarget -= AI_DT;
    if (R->target < 0 || !g->racers[R->target].active || R->retarget <= 0.0f) {
        R->target = pick_victim(g, idx);
        R->retarget = 2.0f + 2.0f * game_rand(g);
    }
    if (R->target < 0) { in.brake = 0.5f; R->input = in; return; }

    {
        const Car *tc = &g->racers[R->target].car;
        Vec2 to = v2_sub(tc->pos, c->pos);
        float lead;
        dist = v2_len(to);
        lead = clampf(dist / fmaxf(speed, 5.0f), 0.0f, 1.0f);
        aim = v2_add(tc->pos, v2_scale(car_velocity(tc), lead));
    }

    /* Keep out of the walls: if one is close and we're driving into it, head for the middle. */
    dist_wall = fminf(g->arena_hw - fabsf(c->pos.x), g->arena_hh - fabsf(c->pos.y));
    if (dist_wall < 8.0f && v2_dot(fwd, v2_sub(centre, c->pos)) < 0.0f && speed > 4.0f)
        aim = centre;

    {
        Vec2 local = v2_rot(v2_sub(aim, c->pos), -c->heading);
        float alpha = atan2f(local.y, local.x);
        float ld = fmaxf(dist * 0.5f, 4.0f);
        if (R->stuck_time >= 1.5f) {
            /* Wedged or stalled: back away. */
            in.reverse = 1; in.throttle = 1.0f;
            in.steer = -steer_toward(c, aim, ld);
            R->stuck_time += AI_DT;
            if (R->stuck_time > 3.0f) R->stuck_time = 0.0f;
        } else if (fabsf(alpha) > 2.4f && dist < 20.0f && R->skill > 0.9f) {
            /* Target behind us: back into it, trunk first, like a real derby driver. */
            float back = wrap_pi(alpha - OR_PI);
            in.reverse = 1; in.throttle = 1.0f;
            in.steer = clampf(-atan2f(2.0f * c->spec->wheelbase * sinf(back), ld) / car_steer_lock(c),
                              -1.0f, 1.0f);
        } else {
            in.steer = steer_toward(c, aim, ld);
            in.throttle = 1.0f;
        }
        if (speed < 1.5f) R->stuck_time += AI_DT;
        else if (R->stuck_time < 1.5f) R->stuck_time = 0.0f;
    }
    R->input = in;
}

void ai_drive(Game *g, int idx)
{
    if (g->track) drive_race(g, idx);
    else drive_derby(g, idx);
}

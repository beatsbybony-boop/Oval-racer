#include <string.h>
#include "game.h"

#define DERBY_HIT_LIMIT 40.0f    /* seconds without a real hit before a car is out */
#define DERBY_TIME_LIMIT 480.0f
#define RACE_DAMAGE_SCALE 0.20f

float game_rand(Game *g)
{
    g->rng = g->rng * 1664525u + 1013904223u;
    return (float)((g->rng >> 8) & 0xFFFFFFu) / 16777216.0f;
}

float game_start_line(const Game *g)
{
    return g->track ? track_start_s(g->track) : 0.0f;
}

Grip game_grip_at(const Game *g, Vec2 p, float hint_s, TrackPos *tp_out)
{
    if (g->track) {
        TrackPos tp;
        track_locate_hint(g->track, p, hint_s, &tp);
        if (tp_out) *tp_out = tp;
        return track_grip(g->track, tp.lateral);
    } else {
        Grip gr;
        if (tp_out) memset(tp_out, 0, sizeof(*tp_out));
        gr.mu = 0.68f;
        gr.rolling = 0.05f;
        gr.stiff = 8.0f;
        return gr;
    }
}

/* ------------------------------------------------------------------ */
/* Setup                                                              */
/* ------------------------------------------------------------------ */

static void init_common(Game *g, GameMode mode, int count, int with_player, unsigned seed)
{
    memset(g, 0, sizeof(*g));
    g->mode = mode;
    g->phase = PHASE_COUNTDOWN;
    g->countdown = 3.0f;
    g->rng = seed ? seed : 1u;
    g->count = count < 2 ? 2 : (count > MAX_RACERS ? MAX_RACERS : count);
    g->player = with_player ? 0 : -1;
    g->winner = -1;
    vehicle_init_specs();
}

static void init_racer(Game *g, int i, const VehicleSpec *spec, Vec2 pos, float heading)
{
    Racer *R = &g->racers[i];
    memset(R, 0, sizeof(*R));
    car_reset(&R->car, spec, pos, heading);
    R->is_player = (i == g->player);
    R->ai_control = !R->is_player;
    R->active = 1;
    R->target = -1;
    R->skill = 0.88f + 0.10f * game_rand(g);
}

/* Starting grid: two abreast behind the start line, player mid-pack. */
static void place_on_grid(Game *g, int i, const VehicleSpec *spec)
{
    const Track *track = g->track;
    float len = track_length(track);
    float line = track_start_s(track);
    float spacing = track->shape == SHAPE_FIG8 ? 7.0f : 9.0f;
    int slot = i;
    float s, lat;
    Vec2 tangent, pos;
    Racer *R;

    if (i == 0) slot = g->count / 2;
    else if (i <= g->count / 2) slot = i - 1;

    s = line - 8.0f - (float)(slot / 2) * spacing;
    lat = (slot % 2 == 0 ? -1.0f : 1.0f) * track->width * 0.2f;
    pos = track_point(track, s, lat, &tangent);
    init_racer(g, i, spec, pos, atan2f(tangent.y, tangent.x));

    R = &g->racers[i];
    R->dist = s - line;
    R->prev_s = s < 0.0f ? s + len : s;
    {
        float half = track->width * 0.5f - 2.0f;
        if (track->surface == SURF_DIRT)
            R->lane = track->groove * half + (game_rand(g) - 0.5f) * 4.0f;
        else
            R->lane = (game_rand(g) - 0.5f) * 2.0f * half * 0.6f;
    }
}

void game_init_race(Game *g, const Track *track, VehicleClass cls,
                    int count, int laps, int with_player, unsigned seed)
{
    int i;

    init_common(g, MODE_RACE, count, with_player, seed);
    g->track = track;
    g->laps = laps < 1 ? 1 : laps;
    g->damage_scale = RACE_DAMAGE_SCALE;
    for (i = 0; i < g->count; i++) place_on_grid(g, i, vehicle_spec(cls));
}

void game_init_derby(Game *g, VehicleClass player_cls, int count, int with_player,
                     unsigned seed, const Track *track, int laps)
{
    static const VehicleClass pool[] = {
        VC_CROWN_VIC, VC_CROWN_VIC, VC_CROWN_VIC, VC_STREET_STOCK, VC_STREET_STOCK,
        VC_TRUCK, VC_MODIFIED, VC_LATE_MODEL, VC_CAMPER, VC_BUS, VC_SPRINT
    };
    const int npool = (int)(sizeof(pool) / sizeof(pool[0]));
    int i;

    init_common(g, MODE_DERBY, count, with_player, seed);
    g->damage_scale = 1.0f;
    g->track = track;
    g->laps = laps < 1 ? 1 : laps;
    g->arena_hw = 55.0f;
    g->arena_hh = 38.0f;

    for (i = 0; i < g->count; i++) {
        VehicleClass cls = (i == 0 && with_player)
            ? player_cls
            : pool[(int)(game_rand(g) * (float)npool) % npool];
        if (track) {
            place_on_grid(g, i, vehicle_spec(cls));
            /* Derby drivers: pick how aggressive each one is. */
            g->racers[i].skill = 0.90f + 0.10f * game_rand(g);
        } else {
            float a = 2.0f * OR_PI * (float)i / (float)g->count;
            Vec2 pos = v2(cosf(a) * g->arena_hw * 0.7f, sinf(a) * g->arena_hh * 0.7f);
            init_racer(g, i, vehicle_spec(cls), pos, atan2f(-pos.y, -pos.x));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Simulation                                                         */
/* ------------------------------------------------------------------ */

static void eliminate(Game *g, int i)
{
    Racer *R = &g->racers[i];
    if (!R->active) return;
    R->active = 0;
    R->elim_time = g->time;
    R->ai_control = 1;
    memset(&R->input, 0, sizeof(R->input));
    R->input.brake = 0.4f;
}

static void update_race_progress(Game *g, int i)
{
    Racer *R = &g->racers[i];
    TrackPos tp;
    float len = track_length(g->track);
    float ds;
    int laps_now;

    track_locate_hint(g->track, R->car.pos, R->prev_s, &tp);
    ds = tp.s - R->prev_s;
    if (ds < -len * 0.5f) ds += len;
    if (ds > len * 0.5f) ds -= len;
    R->dist += ds;
    R->prev_s = tp.s;

    laps_now = R->dist >= 0.0f ? (int)(R->dist / len) : 0;
    if (laps_now > R->laps_done) {
        R->laps_done = laps_now;
        R->last_lap = g->time - R->lap_start;
        R->lap_start = g->time;
        if (R->best_lap <= 0.0f || R->last_lap < R->best_lap) R->best_lap = R->last_lap;
    }

    if (!R->finished && R->dist >= (float)g->laps * len) {
        R->finished = 1;
        R->finish_time = g->time;
        R->ai_control = 1;          /* take the cool-down lap on autopilot */
        g->finished_count++;
        if (g->winner < 0) g->winner = i;
        if (i == g->player || g->player < 0) g->phase = PHASE_FINISHED;
    }
}

static void update_derby_rules(Game *g, int i)
{
    Racer *R = &g->racers[i];
    if (!R->active) return;
    R->hit_timer += FIXED_DT;
    if (car_is_dead(&R->car) || (!g->track && R->hit_timer > DERBY_HIT_LIMIT)) eliminate(g, i);
}

static void step(Game *g, const CarInput *player_input)
{
    int i;

    if (g->phase == PHASE_COUNTDOWN) {
        g->countdown -= FIXED_DT;
        if (g->countdown <= 0.0f) { g->phase = PHASE_RUNNING; g->time = 0.0f; }
        return;
    }
    if (g->phase == PHASE_RUNNING) g->time += FIXED_DT;
    g->tick++;

    for (i = 0; i < g->count; i++) {
        Racer *R = &g->racers[i];
        TrackPos tp;
        Grip grip;
        float load = 1.0f;
        Vec2 ext = v2(0.0f, 0.0f);

        if (R->is_player && !R->ai_control && R->active && player_input) {
            CarInput in = *player_input;
            /* Hold the brake from a standstill to back up. */
            if (in.brake > 0.05f && in.throttle < 0.01f && R->car.vx < 0.8f) {
                in.reverse = 1; in.throttle = in.brake; in.brake = 0.0f;
            }
            R->input = in;
        } else if (R->active && (g->tick % 4u) == ((unsigned)i % 4u)) {
            ai_drive(g, i);
        }

        grip = game_grip_at(g, R->car.pos, R->prev_s, &tp);
        if (g->track)
            track_bank_effect(g->track, &tp, car_speed(&R->car), &load, &ext);
        car_step(&R->car, &R->input, &grip, load, ext, FIXED_DT);
    }

    collide_all(g);

    for (i = 0; i < g->count; i++) {
        if (g->track) update_race_progress(g, i);
        if (g->mode == MODE_DERBY) update_derby_rules(g, i);
    }

    if (g->mode == MODE_DERBY && g->phase == PHASE_RUNNING) {
        int alive = game_active_count(g);
        int all_done = g->track != NULL;
        for (i = 0; i < g->count; i++)
            if (g->racers[i].active && !g->racers[i].finished) all_done = 0;
        if (alive <= 1 || all_done || g->time > DERBY_TIME_LIMIT) {
            if (g->winner < 0) {
                int best = -1;
                float bh = -1.0f;
                for (i = 0; i < g->count; i++)
                    if (g->racers[i].active && g->racers[i].car.health > bh) { bh = g->racers[i].car.health; best = i; }
                g->winner = best;
            }
            g->phase = PHASE_FINISHED;
        }
    }
}

void game_update(Game *g, float dt, const CarInput *player_input)
{
    g->accum += dt;
    if (g->accum > 0.25f) g->accum = 0.25f;      /* don't spiral after a long frame */
    while (g->accum >= FIXED_DT) {
        step(g, player_input);
        g->accum -= FIXED_DT;
    }
}

/* ------------------------------------------------------------------ */
/* Queries                                                            */
/* ------------------------------------------------------------------ */

int game_active_count(const Game *g)
{
    int i, n = 0;
    for (i = 0; i < g->count; i++) if (g->racers[i].active) n++;
    return n;
}

int game_position(const Game *g, int idx)
{
    const Racer *a = &g->racers[idx];
    int j, pos = 1;
    for (j = 0; j < g->count; j++) {
        const Racer *b = &g->racers[j];
        int ahead;
        if (j == idx) continue;
        if (g->track) {
            if (b->finished != a->finished) ahead = b->finished;
            else if (b->finished) ahead = b->finish_time < a->finish_time;
            else if (b->active != a->active) ahead = b->active;
            else ahead = b->dist > a->dist;
        } else {
            if (b->active != a->active) ahead = b->active;
            else if (b->active) ahead = b->car.health > a->car.health;
            else ahead = b->elim_time > a->elim_time;
        }
        if (ahead) pos++;
    }
    return pos;
}

int game_current_lap(const Game *g, int idx)
{
    int lap = g->racers[idx].laps_done + 1;
    return lap > g->laps ? g->laps : lap;
}

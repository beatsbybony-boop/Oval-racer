/*
 * Headless checks for the portable core: no graphics, no PS2.
 * Build with: make test   (see the Makefile for the compiler flags)
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../core/game.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_geometry(void)
{
    int ti, k;
    printf("[geometry] track_point <-> track_locate round trip\n");
    for (ti = 0; ti < TRACK_COUNT; ti++) {
        const Track *t = track_preset(ti);
        float len = track_length(t);
        for (k = 0; k < 40; k++) {
            float s = len * (float)k / 40.0f + 0.37f;
            float lat = (k % 5 - 2) * 2.0f;
            TrackPos tp;
            Vec2 p = track_point(t, s, lat, NULL);
            track_locate_hint(t, p, s, &tp);
            CHECK(fabsf(tp.s - s) < 0.12f && fabsf(tp.lateral - lat) < 0.05f,
                  "%s s=%.2f lat=%.1f -> s=%.2f lat=%.2f", t->name, s, lat, tp.s, tp.lateral);
        }
        printf("  %-26s length %.0f m (%.2f mi)\n", t->name, len, len / 1609.34f);
    }
}

static void test_straight_line(void)
{
    int vc;
    printf("[vehicles] 0-60 mph, top speed after 25 s, 60-0 mph on flat asphalt\n");
    for (vc = 0; vc < VC_COUNT; vc++) {
        Car c;
        CarInput in = { 0.0f, 1.0f, 0.0f, 0 };
        Grip gr = { 1.0f, 0.015f, 14.0f };
        float t = 0.0f, t60 = -1.0f, d_brake = 0.0f;
        const float mph60 = 26.82f;
        car_reset(&c, vehicle_spec((VehicleClass)vc), v2(0, 0), 0.0f);
        while (t < 25.0f) {
            car_step(&c, &in, &gr, 1.0f, v2(0, 0), FIXED_DT);
            t += FIXED_DT;
            if (t60 < 0.0f && c.vx >= mph60) t60 = t;
        }
        {
            float top = c.vx;
            Vec2 start = c.pos;
            in.throttle = 0.0f; in.brake = 1.0f;
            if (c.vx > mph60) {
                while (c.vx > mph60 && t < 60.0f) { car_step(&c, &in, &gr, 1.0f, v2(0, 0), FIXED_DT); t += FIXED_DT; }
                start = c.pos;
            }
            while (c.vx > 0.3f && t < 90.0f) { car_step(&c, &in, &gr, 1.0f, v2(0, 0), FIXED_DT); t += FIXED_DT; }
            d_brake = v2_len(v2_sub(c.pos, start));
            printf("  %-13s 0-60: %5.2f s   top: %5.1f mph   60-0: %5.1f m\n",
                   c.spec->name, t60, top * 2.237f, d_brake);
            CHECK(t60 > 0.5f || t60 < 0.0f, "%s accelerates implausibly fast", c.spec->name);
            CHECK(fabsf(c.vy) < 0.5f, "%s drifted sideways under straight-line braking (vy=%.2f)", c.spec->name, c.vy);
        }
    }
}

static void run_race(int ti, VehicleClass cls, int count, int laps)
{
    Game g;
    const Track *t = track_preset(ti);
    int i, steps = 0, spun = 0;
    float worst = 1.0f, avg_h = 0.0f;
    float len = track_length(t);

    game_init_race(&g, t, cls, count, laps, 0, 1234u + (unsigned)ti * 17u + (unsigned)cls);
    while (g.phase != PHASE_FINISHED && g.time < 600.0f && steps < 120 * 700) {
        game_update(&g, FIXED_DT, NULL);
        steps++;
        for (i = 0; i < g.count; i++) {
            float al = v2_dot(car_forward(&g.racers[i].car), v2(1, 0));
            (void)al;
        }
    }
    for (i = 0; i < g.count; i++) {
        Racer *R = &g.racers[i];
        if (R->car.health < worst) worst = R->car.health;
        avg_h += R->car.health;
        if (!R->finished && g.phase == PHASE_FINISHED) spun++;
    }
    avg_h /= (float)g.count;
    {
        float best = 1e9f;
        for (i = 0; i < g.count; i++)
            if (g.racers[i].best_lap > 0.0f && g.racers[i].best_lap < best) best = g.racers[i].best_lap;
        printf("  %-26s %-12s race %5.1fs  best lap %5.2fs (%.0f mph avg)  worst health %3.0f%%  avg %3.0f%%  finished %d/%d\n",
               t->name, vehicle_spec(cls)->name, g.time, best < 1e8f ? best : 0.0f,
               best < 1e8f ? len / best * 2.237f : 0.0f, worst * 100.0f, avg_h * 100.0f, g.finished_count, g.count);
    }
    CHECK(g.phase == PHASE_FINISHED, "%s / %s did not finish within the time limit", t->name, vehicle_spec(cls)->name);
    CHECK(g.finished_count >= 1, "%s / %s: nobody finished", t->name, vehicle_spec(cls)->name);
    (void)spun;
}

static void test_races(void)
{
    int ti, vc;
    printf("[race] 8-car AI races, 4 laps\n");
    for (ti = 0; ti < TRACK_COUNT; ti++)
        for (vc = 0; vc < VC_COUNT; vc++)
            run_race(ti, (VehicleClass)vc, 8, 4);
}

static void test_derby(void)
{
    unsigned seed;
    printf("[derby] 12-car mixed-class derbies\n");
    for (seed = 1; seed <= 3; seed++) {
        Game g;
        int i, out = 0;
        game_init_derby(&g, VC_CROWN_VIC, 12, 0, seed * 77u, NULL, 1);
        while (g.phase != PHASE_FINISHED && g.time < 600.0f)
            game_update(&g, FIXED_DT, NULL);
        for (i = 0; i < g.count; i++) if (!g.racers[i].active) out++;
        printf("  seed %u: ended %5.1fs, %d of %d out, winner #%d (%s, health %.0f%%)\n",
               seed, g.time, out, g.count, g.winner,
               g.winner >= 0 ? g.racers[g.winner].car.spec->name : "none",
               g.winner >= 0 ? g.racers[g.winner].car.health * 100.0f : 0.0f);
        CHECK(g.phase == PHASE_FINISHED, "derby seed %u never ended", seed);
        CHECK(out >= 6, "derby seed %u: only %d cars eliminated (too little contact?)", seed, out);
    }
}

static void test_fig8_derby(void)
{
    int ti;
    unsigned seed;
    printf("[figure-8 derby] 12 mixed cars, 4 laps, hits at the crossover\n");
    for (ti = 0; ti < TRACK_COUNT; ti++) {
        const Track *t = track_preset(ti);
        if (t->shape != SHAPE_FIG8) continue;
        for (seed = 1; seed <= 3; seed++) {
            Game g;
            int i, out = 0, wrecked_at_cross = 0;
            float avg = 0.0f;
            game_init_derby(&g, VC_CROWN_VIC, 12, 0, seed * 313u, t, 4);
            while (g.phase != PHASE_FINISHED && g.time < 600.0f)
                game_update(&g, FIXED_DT, NULL);
            for (i = 0; i < g.count; i++) {
                const Racer *R = &g.racers[i];
                avg += R->car.health;
                if (!R->active) {
                    out++;
                    if (v2_len(R->car.pos) < t->width * 1.5f) wrecked_at_cross++;
                }
            }
            avg /= (float)g.count;
            printf("  %-26s seed %u: %5.1fs, %2d out (%d at the crossover), avg health %3.0f%%, winner #%d %s\n",
                   t->name, seed, g.time, out, wrecked_at_cross, avg * 100.0f, g.winner,
                   g.winner >= 0 ? g.racers[g.winner].car.spec->name : "none");
            CHECK(g.phase == PHASE_FINISHED, "%s derby seed %u never ended", t->name, seed);
            CHECK(avg < 0.97f, "%s derby seed %u: almost no contact (avg health %.2f)", t->name, seed, avg);
        }
    }
}


/* ---- physics feel: weight transfer, rollover, dirt slide, damage effects, impacts ---- */

static Grip asphalt_grip(void) { Grip g = { 1.0f, 0.015f, 14.0f }; return g; }
static Grip dirt_grip(void)    { Grip g = { 0.62f, 0.045f, 8.0f }; return g; }

/* Hold a steady turn at `speed` for a few seconds. Returns max |vy/vx| and counts rolls. */
static float steady_turn(VehicleClass vc, Grip gr, float speed, float steer, int *rolls)
{
    Car c;
    CarInput in = { steer, 0.35f, 0.0f, 0 };
    float t, worst = 0.0f;
    car_reset(&c, vehicle_spec(vc), v2(0, 0), 0.0f);
    c.vx = speed;
    for (t = 0.0f; t < 4.0f; t += FIXED_DT) {
        car_step(&c, &in, &gr, 1.0f, v2(0, 0), FIXED_DT);
        if (c.vx > 3.0f && fabsf(c.vy / c.vx) > worst) worst = fabsf(c.vy / c.vx);
    }
    *rolls = c.rolls;
    return worst;
}

static void test_physics(void)
{
    int rolls, i;
    Grip a = asphalt_grip(), d = dirt_grip();
    printf("[physics] weight transfer, rollover, dirt, damage, impacts\n");

    /* Tall vehicles tip in a hard turn on asphalt; low race cars don't. */
    {
        int r_bus, r_camper, r_late;
        steady_turn(VC_BUS, a, 22.0f, 0.5f, &r_bus);
        steady_turn(VC_CAMPER, a, 24.0f, 0.5f, &r_camper);
        steady_turn(VC_LATE_MODEL, a, 28.0f, 0.5f, &r_late);
        printf("  hard turn on asphalt: bus rolls %d, camper rolls %d, late model rolls %d\n", r_bus, r_camper, r_late);
        CHECK(r_bus + r_camper > 0, "neither the bus nor the camper rolled in a hard turn");
        CHECK(r_late == 0, "the late model rolled in a hard turn (%d)", r_late);
    }

    /* Dirt is looser: same input, more sideways angle. */
    {
        float sa = steady_turn(VC_LATE_MODEL, a, 20.0f, 0.22f, &rolls);
        float sd = steady_turn(VC_LATE_MODEL, d, 20.0f, 0.22f, &rolls);
        printf("  late model, same steering at 20 m/s: slip ratio asphalt %.2f, dirt %.2f\n", sa, sd);
        CHECK(sd > sa, "dirt was not slidier than asphalt (%.2f vs %.2f)", sd, sa);
    }

    /* Weight transfer: braking should load the front axle and unload the rear. */
    {
        Car c;
        CarInput in = { 0.0f, 0.0f, 1.0f, 0 };
        float t;
        car_reset(&c, vehicle_spec(VC_LATE_MODEL), v2(0, 0), 0.0f);
        c.vx = 40.0f;
        for (t = 0.0f; t < 0.6f; t += FIXED_DT) car_step(&c, &in, &a, 1.0f, v2(0, 0), FIXED_DT);
        printf("  braking from 40 m/s: filtered decel %.1f m/s^2\n", -c.ax_f);
        CHECK(c.ax_f < -8.0f, "braking did not build a longitudinal load transfer signal (ax_f=%.2f)", c.ax_f);
    }

    /* Damage has consequences. */
    {
        Car fresh, hurt, drag;
        CarInput go = { 0.0f, 1.0f, 0.0f, 0 };
        float t;
        car_reset(&fresh, vehicle_spec(VC_LATE_MODEL), v2(0, 0), 0.0f);
        car_reset(&hurt, vehicle_spec(VC_LATE_MODEL), v2(0, 0), 0.0f);
        car_reset(&drag, vehicle_spec(VC_LATE_MODEL), v2(0, 0), 0.0f);
        hurt.zone[ZONE_FRONT] = 0.8f; car_update_health(&hurt);
        drag.wheel[WHEEL_FL] = 0.0f;  car_update_health(&drag);
        for (t = 0.0f; t < 6.0f; t += FIXED_DT) {
            car_step(&fresh, &go, &a, 1.0f, v2(0, 0), FIXED_DT);
            car_step(&hurt, &go, &a, 1.0f, v2(0, 0), FIXED_DT);
            car_step(&drag, &go, &a, 1.0f, v2(0, 0), FIXED_DT);
        }
        printf("  after 6 s flat out: fresh %.1f m/s, crushed nose %.1f m/s, torn-off front-left wheel pulled %.1f m sideways (heading %.1f deg)\n",
               fresh.vx, hurt.vx, drag.pos.y, drag.heading * 57.3f);
        CHECK(hurt.vx < fresh.vx - 1.0f, "front damage did not cost speed");
        CHECK(drag.pos.y > 0.5f, "a torn-off front-left wheel did not pull the car left (y=%.2f)", drag.pos.y);
    }

    /* Impacts: a car into a wall dies faster than a bus, and a glancing hit spins a car. */
    {
        Game g;
        float hp_vic, hp_bus;
        for (i = 0; i < 2; i++) {
            VehicleClass vc = i == 0 ? VC_CROWN_VIC : VC_BUS;
            float t;
            game_init_derby(&g, vc, 2, 1, 5u, NULL, 1);
            g.racers[0].car.pos = v2(g.arena_hw - 14.0f, 0.0f);
            g.racers[0].car.heading = 0.0f;
            g.racers[0].car.vx = 9.0f;
            g.racers[1].car.pos = v2(-30.0f, -30.0f);
            g.countdown = 0.0f; g.phase = PHASE_RUNNING;
            for (t = 0.0f; t < 1.5f; t += FIXED_DT) game_update(&g, FIXED_DT, NULL);
            if (i == 0) hp_vic = g.racers[0].car.health; else hp_bus = g.racers[0].car.health;
        }
        printf("  head-on into the wall at 9 m/s: Crown Vic health %.0f%%, school bus %.0f%%\n", hp_vic * 100.0f, hp_bus * 100.0f);
        CHECK(hp_vic < hp_bus, "the Crown Vic should be hurt more than the bus by the same wall hit");
        CHECK(hp_vic < 0.95f, "a 20 mph wall hit barely scratched the Crown Vic");
    }
    {
        Game g;
        float t, peak = 0.0f;
        game_init_derby(&g, VC_CROWN_VIC, 2, 1, 9u, NULL, 1);
        g.racers[0].car.pos = v2(0.0f, 0.0f);   g.racers[0].car.heading = 0.0f; g.racers[0].car.vx = 12.0f;
        g.racers[1].car.pos = v2(1.0f, 1.55f);  g.racers[1].car.heading = 0.0f; g.racers[1].car.vx = 0.0f;
        g.countdown = 0.0f; g.phase = PHASE_RUNNING;
        for (t = 0.0f; t < 1.0f; t += FIXED_DT) {
            game_update(&g, FIXED_DT, NULL);
            if (fabsf(g.racers[1].car.yaw_rate) > peak) peak = fabsf(g.racers[1].car.yaw_rate);
        }
        printf("  glancing side-swipe at 12 m/s: struck car's peak spin %.2f rad/s\n", peak);
        CHECK(peak > 0.3f, "a side-swipe did not spin the struck car (peak %.2f rad/s)", peak);
    }
}

int main(void)
{
    test_geometry();
    test_straight_line();
    test_races();
    test_derby();
    test_fig8_derby();
    test_physics();
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

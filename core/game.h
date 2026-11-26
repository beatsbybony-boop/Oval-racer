#ifndef OR_GAME_H
#define OR_GAME_H

#include "vehicle.h"
#include "track.h"

#define MAX_RACERS 20
#define MAX_HULL_CIRCLES 6
#define FIXED_DT (1.0f / 120.0f)

typedef enum { MODE_RACE = 0, MODE_DERBY } GameMode;
typedef enum { PHASE_COUNTDOWN = 0, PHASE_RUNNING, PHASE_FINISHED } GamePhase;

typedef struct {
    Car      car;
    CarInput input;       /* what the driver (human or AI) asked for this step */
    int      is_player;
    int      ai_control;  /* 1 = AI drives (also set once a racer finishes) */
    int      active;      /* 0 once destroyed or eliminated in a derby */

    /* AI personality */
    float    lane;        /* preferred lateral offset, m */
    float    skill;       /* 0.7 .. 1.0 pace multiplier */
    int      target;      /* derby victim index, -1 for none */
    float    retarget;    /* seconds until the derby AI re-picks a victim */
    float    stuck_time;  /* seconds nearly stationary */

    /* Race progress */
    float    dist;        /* unwrapped distance along the track, negative on the grid */
    float    prev_s;
    int      laps_done;
    float    lap_start, last_lap, best_lap;
    int      finished;
    float    finish_time;

    /* Derby */
    float    hit_timer;   /* seconds since this car was last in a real hit */
    float    elim_time;   /* time of elimination, 0 if still running */
} Racer;

typedef struct {
    GameMode      mode;
    GamePhase     phase;
    const Track  *track;       /* race, or a derby run round a track; NULL in the arena */
    float         arena_hw, arena_hh;  /* derby arena half extents, m */
    float         damage_scale;
    Racer         racers[MAX_RACERS];
    int           count;
    int           player;      /* index of the human racer, -1 for an AI-only run */
    int           laps;
    float         time;        /* seconds since the green flag */
    float         countdown;
    float         accum;
    int           finished_count;
    int           winner;      /* index once decided, else -1 */
    unsigned      rng;
    unsigned      tick;        /* fixed steps run so far, used to stagger the AI */
} Game;

/* Everyone in the field drives the same class. Racer 0 is the player. */
void game_init_race(Game *g, const Track *track, VehicleClass cls,
                    int count, int laps, int with_player, unsigned seed);
/*
 * Mixed-class demolition derby. Racer 0 is the player.
 *   track == NULL  walled dirt arena, last car running wins, cars are out at
 *                  0% health or after too long without a real hit.
 *   track != NULL  derby race round that track (meant for the figure 8s):
 *                  hits do full damage and wrecks stay on the track. First
 *                  across the line after `laps` wins, or the last car running.
 */
void game_init_derby(Game *g, VehicleClass player_cls, int count,
                     int with_player, unsigned seed, const Track *track, int laps);

void game_update(Game *g, float dt, const CarInput *player_input);

/* 1-based running order. */
int  game_position(const Game *g, int idx);
int  game_current_lap(const Game *g, int idx);   /* 1-based */
int  game_active_count(const Game *g);
/* Distance along the centreline of the start/finish line (race mode). */
float game_start_line(const Game *g);

/* Shared helpers used by the sub-modules. */
float game_rand(Game *g);                         /* 0..1 */
/* hint_s: the racer's last known distance along the track (see track_locate_hint). */
Grip  game_grip_at(const Game *g, Vec2 p, float hint_s, TrackPos *tp_out);

/* collide.c */
void collide_all(Game *g);

/* ai.c */
void ai_drive(Game *g, int idx);

#endif

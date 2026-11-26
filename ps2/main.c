/*
 * PS2 front end for the oval racer: gsKit 2D top-down view, DualShock input,
 * a simple menu. All game rules and physics live in ../core and are plain C.
 *
 * NOTE: written against the ps2dev toolchain but not compiled in the
 * environment it was authored in. If the build complains, the likely spots are
 * the gsKit font calls and the libpad setup in pad_init().
 */
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <stdio.h>
#include <string.h>

#include "../core/game.h"

#define RGB(r, g, b) GS_SETREG_RGBAQ((r), (g), (b), 0x80, 0x00)

/* ------------------------------------------------------------------ */
/* Pad                                                                */
/* ------------------------------------------------------------------ */

static char padbuf[256] __attribute__((aligned(64)));
static int pad_ok = 0;

typedef struct {
    u32 held, pressed;
    float steer;     /* -1 right .. +1 left */
} PadState;

static void pad_init(void)
{
    int ret;
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    if (!padPortOpen(0, 0, padbuf)) return;
    while ((ret = padGetState(0, 0)) != PAD_STATE_STABLE && ret != PAD_STATE_FINDCTP1)
        ;
    padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
    while (padGetReqState(0, 0) == PAD_RSTAT_BUSY)
        ;
    pad_ok = 1;
}

static void pad_poll(PadState *p)
{
    struct padButtonStatus pb;
    u32 now = 0;
    float stick = 0.0f;

    if (pad_ok && padGetState(0, 0) == PAD_STATE_STABLE && padRead(0, 0, &pb)) {
        now = 0xffff ^ pb.btns;                       /* buttons arrive active-low */
        if ((pb.mode >> 4) == 0x07) {                 /* analog stick present */
            float x = ((float)pb.ljoy_h - 128.0f) / 128.0f;
            if (x > -0.12f && x < 0.12f) x = 0.0f;    /* dead zone */
            stick = -x;
        }
    }
    p->pressed = now & ~p->held;
    p->held = now;
    p->steer = stick;
    if (now & PAD_LEFT) p->steer = 1.0f;
    if (now & PAD_RIGHT) p->steer = -1.0f;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static GSGLOBAL *gs;
static GSFONTM *font;
static float cam_x, cam_y, cam_scale = 6.0f;

static float sx(float x) { return gs->Width * 0.5f + (x - cam_x) * cam_scale; }
static float sy(float y) { return gs->Height * 0.5f - (y - cam_y) * cam_scale; }

/* Rectangle given by its four corners in perimeter order. */
static void quad_world(Vec2 a, Vec2 b, Vec2 c, Vec2 d, int z, u64 col)
{
    /* The GS draws quads as a strip, so swap the last two corners. */
    gsKit_prim_quad(gs, sx(a.x), sy(a.y), sx(b.x), sy(b.y),
                        sx(d.x), sy(d.y), sx(c.x), sy(c.y), z, col);
}

static void text(float x, float y, float scale, u64 col, const char *s)
{
    gsKit_fontm_print_scaled(gs, font, x, y, 3, scale, col, s);
}

static void draw_track(const Track *t, float start_s)
{
    float len = track_length(t), half = t->width * 0.5f;
    int N = (int)(len / 6.0f);
    u64 surf = t->surface == SURF_DIRT ? RGB(124, 86, 52) : RGB(74, 74, 80);
    u64 wall = RGB(215, 215, 215);
    int k;

    if (N < 96) N = 96;
    if (N > 220) N = 220;
    for (k = 0; k < N; k++) {
        float s0 = len * (float)k / N, s1 = len * (float)(k + 1) / N;
        int side = track_wall_sign(t, 0.5f * (s0 + s1));
        Vec2 i0 = track_point(t, s0, -half, NULL), o0 = track_point(t, s0, half, NULL);
        Vec2 i1 = track_point(t, s1, -half, NULL), o1 = track_point(t, s1, half, NULL);
        quad_world(i0, o0, o1, i1, 1, surf);
        if (side != 0) {
            /* Right of travel is +lateral. Wall sits just beyond the racing surface. */
            float e0 = side > 0 ? half : -half, e1 = side > 0 ? half + 0.9f : -half - 0.9f;
            Vec2 a0 = track_point(t, s0, e0, NULL), b0 = track_point(t, s0, e1, NULL);
            Vec2 a1 = track_point(t, s1, e0, NULL), b1 = track_point(t, s1, e1, NULL);
            quad_world(a0, b0, b1, a1, 2, wall);
        }
    }
    {
        Vec2 a, b, c, d;
        a = track_point(t, start_s - 0.6f, -half, NULL);
        b = track_point(t, start_s - 0.6f, half, NULL);
        c = track_point(t, start_s + 0.6f, half, NULL);
        d = track_point(t, start_s + 0.6f, -half, NULL);
        quad_world(a, b, c, d, 2, RGB(245, 245, 245));
    }
}

static void draw_arena(const Game *g)
{
    Vec2 a = v2(-g->arena_hw, -g->arena_hh), b = v2(g->arena_hw, -g->arena_hh);
    Vec2 c = v2(g->arena_hw, g->arena_hh), d = v2(-g->arena_hw, g->arena_hh);
    quad_world(a, b, c, d, 1, RGB(124, 86, 52));
    gsKit_prim_line(gs, sx(a.x), sy(a.y), sx(b.x), sy(b.y), 2, RGB(230, 230, 230));
    gsKit_prim_line(gs, sx(b.x), sy(b.y), sx(c.x), sy(c.y), 2, RGB(230, 230, 230));
    gsKit_prim_line(gs, sx(c.x), sy(c.y), sx(d.x), sy(d.y), 2, RGB(230, 230, 230));
    gsKit_prim_line(gs, sx(d.x), sy(d.y), sx(a.x), sy(a.y), 2, RGB(230, 230, 230));
}

static void draw_car(const Game *g, int idx)
{
    static const u8 pal[8][3] = {
        { 220, 50, 50 }, { 60, 100, 230 }, { 70, 190, 90 }, { 200, 90, 210 },
        { 245, 140, 40 }, { 70, 205, 205 }, { 235, 235, 235 }, { 140, 90, 60 }
    };
    const Racer *R = &g->racers[idx];
    const Car *c = &R->car;
    const VehicleSpec *sp = c->spec;
    float centre = (sp->cg_front - (sp->wheelbase - sp->cg_front)) * 0.5f;
    Vec2 fwd = car_forward(c), left = car_left(c);
    Vec2 mid = v2_add(c->pos, v2_scale(fwd, centre));
    float hl = sp->length * 0.5f, hw = sp->width * 0.5f;
    float shade[2];       /* rear half, front half */
    int r, gr, b, half_i, w;
    Vec2 p[4], ws[4];

    /* A tumbling car narrows and widens as it rolls over. */
    if (c->roll_timer > 0.0f) hw *= 0.35f + 0.65f * fabsf(cosf(c->roll_timer * 9.0f));

    if (idx == g->player) { r = 250; gr = 220; b = 30; }
    else { r = pal[idx % 8][0]; gr = pal[idx % 8][1]; b = pal[idx % 8][2]; }
    shade[0] = 1.0f - 0.65f * c->zone[ZONE_REAR];
    shade[1] = 1.0f - 0.65f * c->zone[ZONE_FRONT];
    if (!R->active && g->mode == MODE_DERBY) { r = gr = b = 70; shade[0] = shade[1] = 1.0f; }

    /* Body in two halves so crushed noses and tails show up in the colour. */
    for (half_i = 0; half_i < 2; half_i++) {
        float x0 = half_i == 0 ? -hl : 0.0f, x1 = half_i == 0 ? 0.0f : hl;
        float sh = shade[half_i];
        p[0] = v2_add(v2_add(mid, v2_scale(fwd, x1)), v2_scale(left, hw));
        p[1] = v2_add(v2_add(mid, v2_scale(fwd, x1)), v2_scale(left, -hw));
        p[2] = v2_add(v2_add(mid, v2_scale(fwd, x0)), v2_scale(left, -hw));
        p[3] = v2_add(v2_add(mid, v2_scale(fwd, x0)), v2_scale(left, hw));
        quad_world(p[0], p[1], p[2], p[3], 4, RGB((int)(r * sh), (int)(gr * sh), (int)(b * sh)));
    }

    /* Windshield toward the front so the heading is obvious. */
    ws[0] = v2_add(v2_add(mid, v2_scale(fwd, hl * 0.55f)), v2_scale(left, hw * 0.7f));
    ws[1] = v2_add(v2_add(mid, v2_scale(fwd, hl * 0.55f)), v2_scale(left, -hw * 0.7f));
    ws[2] = v2_add(v2_add(mid, v2_scale(fwd, hl * 0.15f)), v2_scale(left, -hw * 0.7f));
    ws[3] = v2_add(v2_add(mid, v2_scale(fwd, hl * 0.15f)), v2_scale(left, hw * 0.7f));
    quad_world(ws[0], ws[1], ws[2], ws[3], 5, RGB(20, 24, 32));

    /* Torn-off wheels show as dark blocks sticking out at that corner. */
    for (w = 0; w < WHEEL_COUNT; w++) {
        if (c->wheel[w] > 0.05f) continue;
        {
            float x = w < 2 ? sp->cg_front : -(sp->wheelbase - sp->cg_front);
            float y = (w & 1) == 0 ? sp->track_w * 0.5f : -sp->track_w * 0.5f;
            Vec2 wc = v2_add(v2_add(c->pos, v2_scale(fwd, x)), v2_scale(left, y));
            float sz = 0.35f;
            Vec2 q0 = v2_add(wc, v2(-sz, -sz)), q1 = v2_add(wc, v2(sz, -sz));
            Vec2 q2 = v2_add(wc, v2(sz, sz)), q3 = v2_add(wc, v2(-sz, sz));
            quad_world(q0, q1, q2, q3, 6, RGB(10, 10, 10));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Menu and game flow                                                 */
/* ------------------------------------------------------------------ */

typedef enum { ST_MENU, ST_PLAY, ST_PAUSE } AppState;

typedef struct { int mode, track, venue, vehicle, laps, cars, cursor; } Settings;

/* Derby venues: the dirt arena (-1) and every figure-8 track. */
static int venue_list[TRACK_COUNT + 1];
static int venue_count = 0;

static void build_venues(void)
{
    int i;
    venue_list[venue_count++] = -1;
    for (i = 0; i < TRACK_COUNT; i++)
        if (track_preset(i)->shape == SHAPE_FIG8) venue_list[venue_count++] = i;
}

static const char *mode_names[2] = { "Race", "Demo Derby" };

static void menu_input(Settings *s, const PadState *p, int *start)
{
    int d = 0;
    if (p->pressed & PAD_UP) s->cursor = (s->cursor + 5) % 6;
    if (p->pressed & PAD_DOWN) s->cursor = (s->cursor + 1) % 6;
    if (p->pressed & PAD_LEFT) d = -1;
    if (p->pressed & PAD_RIGHT) d = 1;

    switch (s->cursor) {
    case 0: if (d) s->mode = 1 - s->mode; break;
    case 1:
        if (d && s->mode == MODE_RACE) s->track = (s->track + TRACK_COUNT + d) % TRACK_COUNT;
        else if (d) s->venue = (s->venue + venue_count + d) % venue_count;
        break;
    case 2: if (d) s->vehicle = (s->vehicle + VC_COUNT + d) % VC_COUNT; break;
    case 3: s->laps += d * 2; if (s->laps < 2) s->laps = 2; if (s->laps > 50) s->laps = 50; break;
    case 4: s->cars += d; if (s->cars < 2) s->cars = 2; if (s->cars > 16) s->cars = 16; break;
    default: break;
    }
    if ((p->pressed & PAD_CROSS) && s->cursor == 5) *start = 1;
    if (p->pressed & PAD_START) *start = 1;
}

static void draw_menu(const Settings *s)
{
    char buf[96];
    const u64 white = RGB(240, 240, 240), hi = RGB(250, 210, 40);
    int i;
    const char *labels[5] = { "Mode", "Track", "Vehicle", "Laps", "Cars" };
    float y = 120.0f;

    text(60.0f, 50.0f, 1.0f, hi, "SHORT TRACK RACER");
    for (i = 0; i < 6; i++) {
        u64 col = (i == s->cursor) ? hi : white;
        if (i == 0) snprintf(buf, sizeof(buf), "%-8s  < %s >", labels[0], mode_names[s->mode]);
        else if (i == 1) snprintf(buf, sizeof(buf), "%-8s  < %s >", labels[1],
                                  s->mode == MODE_RACE ? track_preset(s->track)->name
                                  : (venue_list[s->venue] < 0 ? "Dirt Arena" : track_preset(venue_list[s->venue])->name));
        else if (i == 2) snprintf(buf, sizeof(buf), "%-8s  < %s >", labels[2], vehicle_spec((VehicleClass)s->vehicle)->name);
        else if (i == 3) snprintf(buf, sizeof(buf), "%-8s  < %d >", labels[3], s->laps);
        else if (i == 4) snprintf(buf, sizeof(buf), "%-8s  < %d >", labels[4], s->cars);
        else snprintf(buf, sizeof(buf), "START");
        text(80.0f, y, 0.75f, col, buf);
        y += 36.0f;
    }
    text(60.0f, 380.0f, 0.55f, white, "D-pad: choose   X: start   R2/X gas   L2/Square brake-reverse   Start: pause");
}

static void start_game(Game *g, const Settings *s)
{
    unsigned seed = 7919u * (unsigned)(s->track + 3) + (unsigned)s->vehicle * 131u + 17u;
    if (s->mode == MODE_RACE)
        game_init_race(g, track_preset(s->track), (VehicleClass)s->vehicle, s->cars, s->laps, 1, seed);
    else
        game_init_derby(g, (VehicleClass)s->vehicle, s->cars, 1, seed,
                        venue_list[s->venue] < 0 ? NULL : track_preset(venue_list[s->venue]), s->laps);
    cam_x = g->racers[0].car.pos.x;
    cam_y = g->racers[0].car.pos.y;
}

static void update_camera(const Game *g)
{
    const Car *c = &g->racers[0].car;
    if (!g->track) {
        cam_x = 0.0f; cam_y = 0.0f;
        cam_scale = (float)gs->Width / (g->arena_hw * 2.0f + 8.0f);
    } else {
        float target = 7.0f - car_speed(c) * 0.07f;
        cam_x += (c->pos.x - cam_x) * 0.15f;
        cam_y += (c->pos.y - cam_y) * 0.15f;
        cam_scale += (target - cam_scale) * 0.05f;
    }
}

static void draw_hud(const Game *g)
{
    char buf[96];
    const Racer *me = &g->racers[0];
    const u64 white = RGB(240, 240, 240), hi = RGB(250, 210, 40), red = RGB(240, 70, 60);
    int bar_w = (int)(120.0f * me->car.health);

    snprintf(buf, sizeof(buf), "%3.0f mph", car_speed(&me->car) * 2.237f);
    text(30.0f, 24.0f, 0.8f, white, buf);

    if (g->mode == MODE_RACE) {
        snprintf(buf, sizeof(buf), "P%d/%d   LAP %d/%d", game_position(g, 0), g->count, game_current_lap(g, 0), g->laps);
        text(30.0f, 56.0f, 0.65f, hi, buf);
        if (me->last_lap > 0.0f) {
            snprintf(buf, sizeof(buf), "last %.2f  best %.2f", me->last_lap, me->best_lap);
            text(30.0f, 82.0f, 0.5f, white, buf);
        }
    } else if (g->track) {
        snprintf(buf, sizeof(buf), "P%d  LAP %d/%d  %d LEFT", game_position(g, 0), game_current_lap(g, 0), g->laps,
                 game_active_count(g));
        text(30.0f, 56.0f, 0.65f, hi, buf);
        if (!me->active) text(30.0f, 82.0f, 0.5f, red, "YOU'RE OUT - watch the carnage");
    } else {
        snprintf(buf, sizeof(buf), "%d CARS LEFT", game_active_count(g));
        text(30.0f, 56.0f, 0.65f, hi, buf);
        if (me->active) {
            snprintf(buf, sizeof(buf), "make a hit in %2.0fs", 40.0f - me->hit_timer);
            text(30.0f, 82.0f, 0.5f, me->hit_timer > 30.0f ? red : white, buf);
        }
    }
    /* Damage bar. */
    gsKit_prim_sprite(gs, 30.0f, gs->Height - 40.0f, 154.0f, gs->Height - 28.0f, 6, RGB(40, 40, 40));
    gsKit_prim_sprite(gs, 32.0f, gs->Height - 38.0f, 32.0f + (float)bar_w, gs->Height - 30.0f, 7,
                      me->car.health > 0.35f ? RGB(80, 200, 90) : red);

    if (g->phase == PHASE_COUNTDOWN) {
        snprintf(buf, sizeof(buf), "%d", (int)g->countdown + 1);
        text(gs->Width * 0.5f - 12.0f, 150.0f, 2.0f, hi, buf);
    }
    if (g->phase == PHASE_FINISHED) {
        if (g->mode == MODE_RACE) {
            snprintf(buf, sizeof(buf), "RACE OVER - YOU FINISHED P%d", game_position(g, 0));
        } else if (g->winner == 0) {
            snprintf(buf, sizeof(buf), "YOU WIN THE DERBY!");
        } else if (g->winner >= 0) {
            snprintf(buf, sizeof(buf), "%s wins   (you: P%d)", g->racers[g->winner].car.spec->name, game_position(g, 0));
        } else {
            snprintf(buf, sizeof(buf), "NO WINNER");
        }
        text(gs->Width * 0.5f - 200.0f, 150.0f, 0.8f, hi, buf);
        text(gs->Width * 0.5f - 120.0f, 190.0f, 0.55f, white, "Select: back to menu");
    }
}

/* ------------------------------------------------------------------ */

int main(void)
{
    static Game game;
    Settings set = { MODE_RACE, 0, 0, VC_LATE_MODEL, 10, 8, 0 };
    PadState pad;
    AppState state = ST_MENU;
    float frame_dt;

    memset(&pad, 0, sizeof(pad));
    build_venues();
    pad_init();

    gs = gsKit_init_global();
    gs->Mode = gsKit_check_rom();
    gs->Height = (gs->Mode == GS_MODE_PAL) ? 512 : 448;
    gs->PSM = GS_PSM_CT16;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    font = gsKit_init_fontm();
    gsKit_fontm_upload(gs, font);
    font->Spacing = 0.9f;

    frame_dt = (gs->Mode == GS_MODE_PAL) ? (1.0f / 50.0f) : (1.0f / 60.0f);

    for (;;) {
        int start = 0;
        pad_poll(&pad);

        if (state == ST_MENU) {
            gsKit_clear(gs, RGB(24, 40, 24));
            menu_input(&set, &pad, &start);
            draw_menu(&set);
            if (start) { start_game(&game, &set); state = ST_PLAY; }
        } else {
            if (pad.pressed & PAD_SELECT) { state = ST_MENU; }
            else if (pad.pressed & PAD_START) state = (state == ST_PLAY) ? ST_PAUSE : ST_PLAY;

            if (state == ST_PLAY) {
                CarInput in;
                in.steer = pad.steer;
                in.throttle = (pad.held & (PAD_R2 | PAD_CROSS)) ? 1.0f : 0.0f;
                in.brake = (pad.held & (PAD_L2 | PAD_SQUARE)) ? 1.0f : 0.0f;
                in.reverse = 0;
                game_update(&game, frame_dt, &in);
            }
            update_camera(&game);

            gsKit_clear(gs, game.track ? RGB(34, 96, 40) : RGB(20, 28, 20));
            if (game.track) draw_track(game.track, game_start_line(&game));
            else draw_arena(&game);
            {
                int i;
                for (i = game.count - 1; i >= 0; i--) draw_car(&game, i);
            }
            draw_hud(&game);
            if (state == ST_PAUSE) text(gs->Width * 0.5f - 60.0f, 200.0f, 1.0f, RGB(250, 210, 40), "PAUSED");
        }

        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}

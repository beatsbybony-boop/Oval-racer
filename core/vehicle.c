#include <string.h>
#include "vehicle.h"

/*
 * Vehicle table. Numbers are ballpark real-world values, tuned so each class
 * feels different rather than matched to a data sheet:
 *   sprint car   - huge power-to-weight, wing downforce, almost no brakes
 *   modified     - light, quick, grippy
 *   late model   - heavier, balanced, good aero
 *   street stock - low power, narrow tires, easy to slide
 *   truck        - heavy and fast, long wheelbase
 *   Crown Vic    - stock sedan, soft and slow, great in a derby
 *   camper       - tall, heavy, poor tires
 *   bus          - enormous inertia, slow to turn, hard to kill
 */
static VehicleSpec specs[VC_COUNT] = {
    /* name            mass   len    wid    wb     cgF    power    top  cd_a   cl_a   mu     steer  brk   durability  cg_h  trk   rollF */
    { "Sprint Car",    635.f, 3.4f,  1.8f,  2.2f,  1.25f, 640e3f,  62.f, 1.40f, 2.20f, 1.35f, 0.45f, 0.55f,  30e3f, 0.42f, 1.55f, 0.40f, 0.f },
    { "Modified",     1090.f, 4.1f,  1.9f,  2.5f,  1.30f, 560e3f,  60.f, 0.85f, 0.25f, 1.40f, 0.40f, 1.20f,  70e3f, 0.45f, 1.65f, 0.50f, 0.f },
    { "Late Model",   1150.f, 4.4f,  1.9f,  2.7f,  1.40f, 560e3f,  62.f, 0.95f, 0.80f, 1.45f, 0.40f, 1.20f,  80e3f, 0.48f, 1.65f, 0.52f, 0.f },
    { "Street Stock", 1360.f, 4.6f,  1.85f, 2.7f,  1.40f, 260e3f,  52.f, 0.95f, 0.10f, 1.15f, 0.45f, 1.10f, 110e3f, 0.55f, 1.55f, 0.55f, 0.f },
    { "Stock Truck",  1400.f, 5.2f,  1.95f, 2.85f, 1.45f, 450e3f,  62.f, 1.10f, 0.50f, 1.40f, 0.40f, 1.15f, 130e3f, 0.62f, 1.65f, 0.55f, 0.f },
    { "Crown Vic",    1750.f, 5.4f,  1.95f, 2.9f,  1.40f, 175e3f,  45.f, 0.80f, 0.00f, 1.00f, 0.55f, 0.95f, 180e3f, 0.60f, 1.60f, 0.58f, 0.f },
    { "Camper",       4500.f, 7.5f,  2.4f,  4.4f,  2.00f, 150e3f,  35.f, 3.90f, 0.00f, 0.85f, 0.50f, 0.70f, 300e3f, 1.45f, 1.90f, 0.50f, 0.f },
    { "School Bus",   9000.f, 10.9f, 2.45f, 5.6f,  3.00f, 170e3f,  28.f, 5.30f, 0.00f, 0.85f, 0.55f, 0.60f, 1300e3f, 1.55f, 2.05f, 0.50f, 0.f },
};

static int specs_ready = 0;

void vehicle_init_specs(void)
{
    int i;
    if (specs_ready) return;
    for (i = 0; i < VC_COUNT; i++) {
        VehicleSpec *s = &specs[i];
        s->yaw_inertia = s->mass * (s->length * s->length + s->width * s->width) / 12.0f * 0.9f;
    }
    specs_ready = 1;
}

const VehicleSpec *vehicle_spec(VehicleClass vc)
{
    vehicle_init_specs();
    return &specs[vc];
}

float vehicle_roll_g(const VehicleSpec *spec)
{
    return spec->track_w / (2.0f * spec->cg_h);
}

void car_reset(Car *c, const VehicleSpec *spec, Vec2 pos, float heading)
{
    int i;
    memset(c, 0, sizeof(*c));
    c->spec = spec;
    c->pos = pos;
    c->heading = heading;
    c->health = 1.0f;
    for (i = 0; i < WHEEL_COUNT; i++) c->wheel[i] = 1.0f;
}

Vec2 car_velocity(const Car *c)
{
    return v2_add(v2_scale(car_forward(c), c->vx), v2_scale(car_left(c), c->vy));
}

float car_speed(const Car *c)
{
    return sqrtf(c->vx * c->vx + c->vy * c->vy);
}

float car_steer_lock(const Car *c)
{
    float k = car_speed(c) / 30.0f;
    return c->spec->max_steer / (1.0f + k * k) * (1.0f - 0.4f * c->zone[ZONE_FRONT]);
}

/* ------------------------------------------------------------------ */
/* Damage                                                             */
/* ------------------------------------------------------------------ */

/* How strongly a contact point at `local` belongs to each body zone. */
static void zone_weights(const VehicleSpec *s, Vec2 local, float w[ZONE_COUNT])
{
    float u = clampf(local.x / (s->length * 0.5f), -1.0f, 1.0f);
    float v = clampf(local.y / (s->width * 0.5f), -1.0f, 1.0f);
    float sum;
    int i;

    w[ZONE_FRONT] = u > 0.0f ? powf(u, 1.5f) : 0.0f;
    w[ZONE_REAR]  = u < 0.0f ? powf(-u, 1.5f) : 0.0f;
    w[ZONE_LEFT]  = v > 0.0f ? powf(v, 1.5f) : 0.0f;
    w[ZONE_RIGHT] = v < 0.0f ? powf(-v, 1.5f) : 0.0f;
    sum = w[0] + w[1] + w[2] + w[3];
    if (sum < 1e-3f) { w[0] = w[1] = w[2] = w[3] = 0.25f; return; }
    for (i = 0; i < ZONE_COUNT; i++) w[i] /= sum;
}

static Vec2 wheel_pos(const VehicleSpec *s, int i)
{
    float x = i < 2 ? s->cg_front : -(s->wheelbase - s->cg_front);
    float y = (i & 1) == 0 ? s->track_w * 0.5f : -s->track_w * 0.5f;
    return v2(x, y);
}

float car_crush_at(const Car *c, Vec2 local)
{
    float w[ZONE_COUNT], d = 0.0f;
    int i;
    zone_weights(c->spec, local, w);
    for (i = 0; i < ZONE_COUNT; i++) d += w[i] * c->zone[i];
    return d;
}

void car_update_health(Car *c)
{
    float mx = 0.0f, sum = 0.0f, wsum = 0.0f;
    int i;
    for (i = 0; i < ZONE_COUNT; i++) { if (c->zone[i] > mx) mx = c->zone[i]; sum += c->zone[i]; }
    for (i = 0; i < WHEEL_COUNT; i++) wsum += c->wheel[i];
    c->health = clampf(1.0f - 0.5f * mx - 0.5f * (sum * 0.25f) - 0.1f * (4.0f - wsum) * 0.25f, 0.0f, 1.0f);
}

int car_is_dead(const Car *c)
{
    return c->zone[ZONE_FRONT] >= 1.0f || c->zone[ZONE_REAR] >= 1.0f || c->health <= 0.2f;
}

void car_apply_damage(Car *c, Vec2 local, float energy, float scale)
{
    const VehicleSpec *s = c->spec;
    float w[ZONE_COUNT];
    float unit = scale * energy / (s->durability * 0.5f);
    int i;

    zone_weights(s, local, w);
    for (i = 0; i < ZONE_COUNT; i++)
        c->zone[i] = clampf(c->zone[i] + unit * w[i], 0.0f, 1.0f);

    /* Wheels near the impact take a share on top of the body damage. */
    for (i = 0; i < WHEEL_COUNT; i++) {
        float d = v2_len(v2_sub(local, wheel_pos(s, i)));
        if (d < 1.4f)
            c->wheel[i] = clampf(c->wheel[i] - unit * 1.4f * (1.0f - d / 1.4f), 0.0f, 1.0f);
    }
    car_update_health(c);
}

void car_start_roll(Car *c)
{
    if (c->roll_timer > 0.0f) return;
    c->roll_timer = 2.4f;
    c->rolls++;
    c->roll_charge = 0.0f;
}

/* ------------------------------------------------------------------ */
/* Tires and the step                                                 */
/* ------------------------------------------------------------------ */

/*
 * Saturating tire curve. Asphalt (stiff ~14) peaks near 8 degrees and holds
 * ~80% when sliding; loose dirt (stiff ~8) peaks later, around 15 degrees,
 * and falls further, which is what makes a car on dirt slide and need
 * catching.
 */
static float tire_curve(float slip, float stiff)
{
    float fall = stiff < 10.0f ? 1.5f : 1.4f;
    return sinf(fall * atanf(stiff * slip));
}

void car_step(Car *c, const CarInput *in_user, const Grip *grip,
              float load_scale, Vec2 ext_accel, float dt)
{
    const VehicleSpec *s = c->spec;
    const float L = s->wheelbase, a = s->cg_front, b = L - a;
    const float m = s->mass, h = s->cg_h, tw = s->track_w;
    const float front_dmg = c->zone[ZONE_FRONT], rear_dmg = c->zone[ZONE_REAR];
    const float stiff = grip->stiff > 0.0f ? grip->stiff : 14.0f;
    const int dirt = stiff < 10.0f;
    CarInput in = *in_user;
    int rolling = c->roll_timer > 0.0f;

    float power = s->power * (1.0f - 0.6f * front_dmg * sqrtf(front_dmg)) * (1.0f - 0.35f * rear_dmg);
    float vx = c->vx, vy = c->vy, r = c->yaw_rate;
    float speed = sqrtf(vx * vx + vy * vy);
    float Fz[WHEEL_COUNT], raw[WHEEL_COUNT];
    float lat[WHEEL_COUNT], cap[WHEEL_COUNT], avail[WHEEL_COUNT], fx[WHEEL_COUNT];
    float cs[WHEEL_COUNT], sn[WHEEL_COUNT];
    float Fx = 0.0f, Fy = 0.0f, Mz = 0.0f;
    float mu = s->mu_tire * grip->mu * (rolling ? 0.3f : 1.0f);
    int i;

    if (rolling) {
        in.throttle = 0.0f; in.steer = 0.0f; in.brake = 0.25f;
        c->roll_timer -= dt;
        for (i = 0; i < ZONE_COUNT; i++) c->zone[i] = clampf(c->zone[i] + 0.05f * dt, 0.0f, 1.0f);
    }

    /* Steering: less lock at speed, rate limited; a dragging front wheel adds toe. */
    {
        float lock = car_steer_lock(c);
        float target = in.steer * lock + 0.012f * (c->wheel[WHEEL_FR] - c->wheel[WHEEL_FL]);
        float rate = 3.0f * dt;
        c->steer += clampf(target - c->steer, -rate, rate);
    }

    /* Normal loads: static split, aero, then longitudinal and lateral weight transfer. */
    {
        float q = 0.5f * OR_RHO * speed * speed;
        float W = m * OR_G * load_scale + q * s->cl_a;
        float Wf = W * b / L, Wr = W * a / L;
        float dfx = clampf(m * c->ax_f * h / L, -0.4f * W, 0.4f * W);
        float dfy = clampf(m * c->ay_f * h / tw, -0.62f * W, 0.62f * W);
        float rf = s->roll_front;
        float drag = q * s->cd_a;
        float resist = drag + grip->rolling * m * OR_G;
        float bdir, Fb, thrust = 0.0f;
        float lift_l, lift_r;

        Wf -= dfx; Wr += dfx;
        raw[0] = Wf * 0.5f - rf * dfy;           raw[1] = Wf * 0.5f + rf * dfy;
        raw[2] = Wr * 0.5f - (1.0f - rf) * dfy;  raw[3] = Wr * 0.5f + (1.0f - rf) * dfy;
        for (i = 0; i < WHEEL_COUNT; i++) Fz[i] = raw[i] > 0.0f ? raw[i] : 0.0f;

        /* One whole side of the car unloaded for long enough: over she goes. */
        lift_l = (raw[0] + raw[2] < 0.04f * W) ? 1.0f : 0.0f;
        lift_r = (raw[1] + raw[3] < 0.04f * W) ? 1.0f : 0.0f;
        if (!rolling && speed > 4.0f && (lift_l > 0.0f || lift_r > 0.0f)) {
            c->roll_charge += dt;
            if (c->roll_charge > 0.15f) car_start_roll(c);
        } else {
            c->roll_charge = fmaxf(0.0f, c->roll_charge - 2.0f * dt);
        }

        /* Drive: rear wheels, engine thrust fades toward top speed. */
        if (in.throttle > 0.0f) {
            float vtop = in.reverse ? s->top_speed * 0.25f : s->top_speed;
            float ratio = fabsf(vx) / vtop;
            float fade = 1.0f - ratio * ratio;
            int going_with = (in.reverse ? -vx : vx) > 0.0f;
            if (!going_with) fade = 1.0f;
            if (fade < 0.0f) fade = 0.0f;
            thrust = in.throttle * power * fade / fmaxf(fabsf(vx), 7.0f);
            if (in.reverse) thrust = -thrust * 0.6f;
        }
        bdir = -tanhf(vx / 0.8f);
        Fb = in.brake * s->brake_g * W;

        /* Per wheel: lateral force from slip angle first. */
        for (i = 0; i < WHEEL_COUNT; i++) {
            Vec2 wp = wheel_pos(s, i);
            float delta = i < 2 ? c->steer : 0.0f;
            float vxw = vx - r * wp.y, vyw = vy + r * wp.x;
            float vl, vt, slip, fz0, ratio, sens, wh, gs;

            cs[i] = cosf(delta); sn[i] = sinf(delta);
            vl = vxw * cs[i] + vyw * sn[i];
            vt = -vxw * sn[i] + vyw * cs[i];
            slip = atan2f(vt, fmaxf(fabsf(vl), 1.5f));

            /* Tires lose grip per unit of load as the load goes up. */
            fz0 = m * OR_G * (i < 2 ? b : a) / L * 0.5f;
            ratio = fz0 > 1.0f ? Fz[i] / fz0 : 1.0f;
            sens = clampf(1.0f - 0.18f * (ratio - 1.0f), 0.55f, 1.3f);
            wh = c->wheel[i];
            gs = wh > 0.02f ? 0.35f + 0.65f * wh : 0.12f;

            cap[i] = mu * gs * Fz[i] * sens;
            lat[i] = -cap[i] * tire_curve(slip, stiff);
            avail[i] = fmaxf(sqrtf(fmaxf(0.0f, cap[i] * cap[i] - lat[i] * lat[i])), 0.15f * cap[i]);
        }

        /* Longitudinal: brakes 60/40 front/rear, then drive through a locked rear axle. */
        for (i = 0; i < WHEEL_COUNT; i++) {
            float want = (i < 2 ? 0.3f : 0.2f) * Fb * bdir;
            fx[i] = clampf(want, -avail[i], avail[i]);
        }
        if (thrust != 0.0f) {
            float sg = thrust > 0.0f ? 1.0f : -1.0f, mag = fabsf(thrust);
            float d2 = fminf(mag * 0.5f, avail[2]), d3 = fminf(mag * 0.5f, avail[3]);
            float left = mag - d2 - d3, add;
            if (left > 0.0f) { add = fminf(left, avail[2] - d2); d2 += add; left -= add; }
            if (left > 0.0f) { add = fminf(left, avail[3] - d3); d3 += add; }
            fx[2] = clampf(fx[2] + sg * d2, -avail[2], avail[2]);
            fx[3] = clampf(fx[3] + sg * d3, -avail[3], avail[3]);
        }

        /* Sum the wheels into body forces and yaw moment. */
        for (i = 0; i < WHEEL_COUNT; i++) {
            Vec2 wp = wheel_pos(s, i);
            float vxw = vx - r * wp.y;
            float u = cap[i] > 1.0f ? fx[i] / cap[i] : 0.0f;
            float fl, fxb, fyb;
            float k = i < 2 ? 0.15f : (dirt ? 0.45f : 0.30f);   /* power loosens the rear, more on dirt */
            float wd = 1.0f - c->wheel[i];

            fl = lat[i] * (1.0f - k * u * u);
            /* A crushed or torn-off wheel drags, which pulls the car toward that side. */
            fx[i] -= tanhf(vxw / 0.5f) * wd * (c->wheel[i] > 0.02f ? 0.15f : 0.30f) * Fz[i];

            fxb = fx[i] * cs[i] - fl * sn[i];
            fyb = fl * cs[i] + fx[i] * sn[i];
            Fx += fxb; Fy += fyb;
            Mz += wp.x * fyb - wp.y * fxb;
        }
        Fx -= tanhf(vx / 0.5f) * resist;
        Mz -= r * s->yaw_inertia * 0.1f;
    }

    {
        float axe = v2_dot(ext_accel, car_forward(c));
        float aye = v2_dot(ext_accel, car_left(c));
        float k = dt / (0.09f + dt);

        vx += (Fx / m + r * vy + axe) * dt;
        vy += (Fy / m - r * vx + aye) * dt;
        r  += (Mz / s->yaw_inertia) * dt;

        c->ax_f += (Fx / m - c->ax_f) * k;
        c->ay_f += (Fy / m - c->ay_f) * k;
    }

    /* Parked cars stay parked. */
    if (in.throttle < 0.01f && sqrtf(vx * vx + vy * vy) < 0.4f) {
        vx *= 0.9f; vy *= 0.9f; r *= 0.9f;
    }
    if (!isfinite(vx) || !isfinite(vy) || !isfinite(r)) {
        vx = vy = r = 0.0f; c->ax_f = c->ay_f = 0.0f;
    }

    c->vx = vx; c->vy = vy; c->yaw_rate = r;
    c->pos = v2_add(c->pos, v2_scale(car_velocity(c), dt));
    c->heading = wrap_pi(c->heading + r * dt);
    car_update_health(c);
}

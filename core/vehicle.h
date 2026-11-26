#ifndef OR_VEHICLE_H
#define OR_VEHICLE_H

#include "vec.h"

typedef enum {
    VC_SPRINT = 0,
    VC_MODIFIED,
    VC_LATE_MODEL,
    VC_STREET_STOCK,
    VC_TRUCK,
    VC_CROWN_VIC,
    VC_CAMPER,
    VC_BUS,
    VC_COUNT
} VehicleClass;

/* Static description of a vehicle type. SI units throughout. */
typedef struct {
    const char *name;
    float mass;        /* kg, with driver */
    float length;      /* m */
    float width;       /* m */
    float wheelbase;   /* m */
    float cg_front;    /* m, centre of gravity to front axle */
    float power;       /* W at the wheels, roughly */
    float top_speed;   /* m/s, thrust fades to zero here (gearing) */
    float cd_a;        /* drag area, m^2 */
    float cl_a;        /* downforce area, m^2 (wings / body) */
    float mu_tire;     /* peak tire grip on clean asphalt */
    float max_steer;   /* rad at low speed */
    float brake_g;     /* peak braking force as a multiple of weight */
    float durability;  /* joules of crash energy before the car is dead */
    float cg_h;        /* m, centre of gravity height: weight transfer and rollover */
    float track_w;     /* m, wheel track (centre to centre of left/right tires) */
    float roll_front;  /* 0..1, share of lateral weight transfer taken by the front axle */
    float yaw_inertia; /* kg m^2, filled in by vehicle_init_specs() */
} VehicleSpec;

/* Driver input, all normalised. */
typedef struct {
    float steer;    /* -1 (right) .. +1 (left) */
    float throttle; /* 0..1 */
    float brake;    /* 0..1 */
    int   reverse;  /* non-zero: throttle drives backwards */
} CarInput;

/* Ground under the car, supplied by the track code. */
typedef struct {
    float mu;      /* multiplier on tire grip */
    float rolling; /* rolling resistance coefficient */
    float stiff;   /* tire slip stiffness: ~14 asphalt, ~8 loose dirt. 0 means 14 */
} Grip;

/* Damage zones and wheels. */
enum { ZONE_FRONT = 0, ZONE_REAR, ZONE_LEFT, ZONE_RIGHT, ZONE_COUNT };
enum { WHEEL_FL = 0, WHEEL_FR, WHEEL_RL, WHEEL_RR, WHEEL_COUNT };

typedef struct {
    const VehicleSpec *spec;
    Vec2  pos;       /* world position of the CG */
    float heading;   /* rad, 0 = +x, counter-clockwise positive */
    float vx, vy;    /* velocity in the body frame (x forward, y left) */
    float yaw_rate;  /* rad/s */
    float steer;     /* current front wheel angle, rad */
    float health;    /* 1.0 new .. 0.0 destroyed, derived from the zones and wheels */

    /* Wreckfest-style damage: how crushed each part of the body is (0 new .. 1 gone)
     * and how healthy each tire/wheel is (1 new .. 0 torn off). Front damage costs
     * power and steering, rear damage costs drive, a dragging wheel pulls the car. */
    float zone[ZONE_COUNT];
    float wheel[WHEEL_COUNT];

    /* Suspension state: low-passed body accelerations that drive weight transfer. */
    float ax_f, ay_f;
    float roll_charge;  /* time the inside wheels have been off the ground */
    float roll_timer;   /* > 0 while the car is tumbling */
    int   rolls;        /* how many times it has rolled */
} Car;

void vehicle_init_specs(void);
const VehicleSpec *vehicle_spec(VehicleClass vc);

void car_reset(Car *c, const VehicleSpec *spec, Vec2 pos, float heading);

/*
 * Advance one car by dt seconds.
 *   grip          surface under the car
 *   load_scale    extra normal load from banking, 1.0 = flat
 *   ext_accel     extra world-frame acceleration (gravity component of banking)
 */
void car_step(Car *c, const CarInput *in, const Grip *grip,
              float load_scale, Vec2 ext_accel, float dt);

/* Lateral g at which the car starts to tip over (track / (2 * cg height)). */
float vehicle_roll_g(const VehicleSpec *spec);

/*
 * Damage. `local` is the contact point in the car's own frame (x forward,
 * y left, origin at the CG). `energy` is joules absorbed by this car, scaled
 * by `scale` (race damage is lighter than derby damage).
 */
void  car_apply_damage(Car *c, Vec2 local, float energy, float scale);
float car_crush_at(const Car *c, Vec2 local);   /* 0 pristine .. 1 flattened */
void  car_update_health(Car *c);
int   car_is_dead(const Car *c);                /* engine/rear gone, or nothing left */
void  car_start_roll(Car *c);

/* Maximum front wheel angle at the car's current speed (rad). */
float car_steer_lock(const Car *c);

static inline Vec2 car_forward(const Car *c) { return v2(cosf(c->heading), sinf(c->heading)); }
static inline Vec2 car_left(const Car *c) { return v2(-sinf(c->heading), cosf(c->heading)); }
Vec2 car_velocity(const Car *c);          /* world frame */
float car_speed(const Car *c);

#endif

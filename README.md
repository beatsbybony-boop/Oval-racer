# Short Track Racer (PS2 homebrew)

Dirt and asphalt short-track ovals, figure-8 tracks, eight vehicle classes, a normal race mode and a demolition
derby mode. Top-down view for now. All physics, AI and rules are portable C in `core/`; `ps2/main.c` is a thin
gsKit/libpad front end.

## What's in it

| | |
|---|---|
| Vehicles | Sprint Car, Modified, Late Model, Street Stock, Stock Truck, Crown Vic, Camper, School Bus |
| Tracks | Pine Ridge 1/4 asphalt, Thunder Hill 1/3 asphalt, Red Clay 3/8 dirt, Dusty Bullring 1/5 dirt, **Cross Creek Figure 8 (dirt)**, **Speedway Figure 8 (asphalt)**, plus a walled dirt arena |
| Race | 2-20 cars, selectable laps, banked ovals, AI that picks lanes, passes and brakes for corners, lap timing. Works on every track, including the figure 8s (where the crossover is a hazard) |
| Demo Derby | Mixed-class field (Crown Vics, buses, campers, trucks...). Pick the **dirt arena** (last car running wins) or a **figure 8** (derby race: full damage, wrecks stay on the track, first across the line after N laps or last car running) |

## Physics (Wreckfest / SRX-style, within PS2 limits)

This is not a soft-body engine (that would not run on a PS2), but it has the parts that make those games feel the way they do:

- **Four-wheel tire model with weight transfer.** Per-wheel loads shift under braking, acceleration and cornering, so
  cars dive, squat and lean. Tires lose grip per unit of load as the load rises (load sensitivity), which gives
  real understeer and oversteer instead of a fixed balance.
- **Drivetrain.** Rear-wheel drive through a locked rear axle. Power loosens the rear, more so on dirt, so you can
  steer with the throttle.
- **Surfaces.** Asphalt has a sharp tire curve. Loose dirt has a lazier, slidier one that peaks later and falls off harder,
  and a tacky groove that the AI hunts for. Grass and aprons are slow and slippery.
- **Zoned crush damage.** The body is four zones (front, rear, left, right) plus four wheels. A hit is credited to the zone
  and wheel nearest the contact point. Front damage costs power and steering, rear damage costs drive, a crushed or
  torn-off wheel drags and pulls the car toward that corner. Crushed metal stops bouncing. A car is out when its engine or
  rear is destroyed or it is mostly wrecked.
- **Collisions.** Impulse-based with Coulomb friction along the contact, so glancing hits spin cars and walls scrub speed.
  The lighter car takes the bigger share of a hit. Buses and campers are heavy but not equally tough per kilo.
- **Rollovers.** When the whole inside of the car unloads, it tips. Tall campers and buses roll in hard turns on asphalt;
  low race cars don't. A hard side hit can also roll a tall vehicle. A rolled car tumbles for a couple of seconds,
  takes damage and loses control.

## Controls (DualShock 2, port 1)

Left stick or D-pad: steer. R2 or X: gas. L2 or Square: brake, and reverse once stopped. Start: pause. Select: back to menu.

## Build

### Check the game logic on any PC (no PS2 tools needed)

    make test

Runs: track geometry round-trips; 0-60, top speed and braking per vehicle; 8-car AI races for every track x vehicle;
12-car derbies on the arena and on both figure 8s; and a physics block (rollover, dirt vs asphalt slide, weight transfer,
damage effects, wall and side-swipe impacts).

### Build the PS2 ELF

Option A, nothing to install (GitHub does the compiling):

1. Make a free GitHub account if you don't have one, and create a new repository.
2. Upload the contents of this folder to it (the `.github` folder must come along; "Add file > Upload files" in the
   web page works, or use git).
3. Open the repository's **Actions** tab. The "Build" workflow starts by itself; it takes a few minutes.
4. Open the finished run and download **oval-racer-elf** from the Artifacts section at the bottom. Unzip it to get
   `oval-racer.elf`. If the run fails, open the "Compile" step and copy the red error lines back to me.

Option B, on your own PC with Docker installed:

    docker run -it --rm -v "$PWD":/src -w /src ps2dev/ps2dev:latest make

Either way you get `oval-racer.elf`. Drag it onto PCSX2 (File > Run ELF), or launch it from uLaunchELF / OPL on real hardware.

## Honest status

- `core/` is compiled and tested on Linux with gcc, and every test in `make test` passes.
- `ps2/main.c` has **not** been compiled against the real toolchain: the environment it was written in had no ps2dev
  and no network for Docker. It does pass a syntax/type check against stub headers, so it is internally consistent, but
  the real gsKit and libpad signatures may differ. Expect to fix a few compile errors on first build and send them back.
- Nothing has been felt with a real controller or profiled on hardware. The physics runs at 120 Hz with up to 20 cars;
  my estimate says that fits the PS2's CPU, but that is an estimate. If it is too heavy, drop to 60 Hz (`FIXED_DT`)
  and/or cap the field.
- Tuning is first-pass. In headless AI races asphalt laps are plausible (Pine Ridge ~15 s for a late model) and average
  health after a 4-lap, 8-car race is ~88%, but the "feel" of each class still needs hands-on tuning.
- The view is top-down 2D. Rollovers and damage are shown by colour, narrowing and dark wheel blocks, not 3D deformation.

## Roadmap

1. Compile and run the top-down build, tune the feel per vehicle class and surface.
2. Chase camera with 3D cars and track (gsKit supports textured triangles; simple box models first).
3. Visual deformation: bend or swap body meshes by zone damage, flying wheels and debris, smoke and fire.
4. Engine/tire audio via audsrv, crowd, collision sounds.
5. Tire wear and fuel, pit stops, caution flags and restarts, heat races into a feature, dirt track rubbering-in.
6. Setup screen (stagger, springs, gearing, brake bias) and a career or points season.

## Layout

    core/vec.h        small vector math
    core/vehicle.*    vehicle table, four-wheel physics step, zone damage, rollover
    core/track.*      oval and figure-8 geometry, surfaces, banking
    core/collide.c    car-car and car-wall impulses, friction, damage
    core/ai.c         race driver and derby driver
    core/game.*       modes, grid setup, lap counting, derby rules
    ps2/main.c        gsKit rendering, pad, menu
    tests/headless.c  host-side checks

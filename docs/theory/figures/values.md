# Measured values

Written by `make theory-figs` (tests/theory_figs.c) from the program's own functions. Do not edit by hand.
Units: the pond is the unit square (lengths in domain widths), times in seconds, density 1. The last column is the relative error; where the theory column is 0 it is the absolute value.

| id | what is measured | measured | theory | error |
|---|---|---|---|---|
| `wall-normal` | largest sum u(ghost) + u(first cell), in size, on the left and right walls after 3 s of a jet (theory 0: no flow through the wall) | 0 | 0 | 0.00e+00 |
| `wall-tangent` | largest difference v(ghost) - v(first cell), in size, on the right wall (theory 0: no shear, a free-slip wall) | 0 | 0 | 0.00e+00 |
| `wall-flux` | volume flux through the right wall, sum of the face velocities x h (theory 0) | 0 | 0 | 0.00e+00 |
| `wall-slip` | tangential speed in the first cell at the wall where the jet strikes it: not zero, the wall does not hold the water | 0.476944 | 0 | 4.77e-01 |
| `diffusion-decay` | amplitude of a weak cell after 10 s, nu = 0.005, relative to the start (theory exp(-2 nu pi^2 t)) | 0.37252 | 0.372708 | 5.04e-04 |
| `diffusion-discrete` | the same against the backward-Euler factor 1 / (1 + nu dt lambda_h) to the 500th power | 0.37252 | 0.373202 | 1.83e-03 |
| `diffusion-big-step` | largest energy ratio over 20 steps of 500 x the explicit limit h^2 / (4 nu) (stable means it never exceeds 1) | 0.825557 | 0 | 8.26e-01 |
| `advection-max-principle` | largest vorticity after 10 s over the largest at the start, n = 48, no viscosity (bilinear interpolation cannot overshoot: at most 1) | 0.584171 | 1 | 4.16e-01 |
| `advection-loss-24` | energy lost in 10 s by a cell of speed 0.31 with zero viscosity, n = 24 (ideal 0; numerical diffusion) | 0.59986 | 0 | 6.00e-01 |
| `advection-loss-48` | the same on the game's grid, n = 48 | 0.503839 | 0 | 5.04e-01 |
| `advection-loss-96` | the same, n = 96 | 0.663694 | 0 | 6.64e-01 |
| `residual-24` | divergence left over the largest vorticity at the end of that run, n = 24 (0 for an exact pressure solve) | 0.0128976 | 0 | 1.29e-02 |
| `residual-48` | the same, n = 48 | 0.0894845 | 0 | 8.95e-02 |
| `residual-96` | the same, n = 96: 24 sweeps reach less far on a finer grid | 0.425473 | 0 | 4.25e-01 |
| `advection-nu-48` | viscosity that would cause the n = 48 loss, -ln(1 - loss) / (4 pi^2 t) (compare the game's 0.0004) | 0.00177529 | 0 | 1.78e-03 |
| `advection-no-growth` | largest relative energy gain in any single step of those runs (theory: none) | 0 | 0 | 0.00e+00 |
| `advection-big-step` | largest energy ratio over 20 steps of dt = 1 s, where the water crosses 15 cells per step (stable: never above 1) | 0.862065 | 0 | 8.62e-01 |
| `projection-before` | largest divergence, in size, right after the first force blob, before the step [1/s] | 0.468453 | 0 | 4.68e-01 |
| `projection-after` | the same after the step: 24 Gauss-Seidel sweeps, twice (an exact solve gives 0) | 0.0221283 | 0 | 2.21e-02 |
| `projection-ratio` | after over before for that first step (the unit test requires below 0.1) | 0.0472368 | 0 | 4.72e-02 |
| `projection-vs-vorticity` | largest divergence over largest vorticity after any step of 5 s of forcing (0 for an exact projection) | 0.0473655 | 0 | 4.74e-02 |
| `force-impulse` | momentum added by one blob of force 0.006 for 0.02 s, summed over the grid (theory F dt) | 0.00012 | 0.00012 | 1.45e-07 |
| `force-peak` | velocity added at the centre cell (theory F dt / (2 pi sigma^2), half a cell off the centre) | 0.011154 | 0.011154 | 1.38e-07 |
| `box-momentum-1` | net x momentum of the water one step later, over the momentum put in (an exact projection in a closed box leaves 0) | 0.997573 | 0 | 9.98e-01 |
| `box-momentum-100` | the same after 100 steps (2 s) | 7.30528e-05 | 0 | 7.31e-05 |
| `box-momentum-1000` | the same after 1000 steps (20 s) | 2.90742e-07 | 0 | 2.91e-07 |
| `energy-cell` | fluid_kinetic_energy of the cell A = 0.1 (theory A^2 pi^2 / 4) | 0.024674 | 0.024674 | 1.59e-08 |
| `vorticity-centre` | fluid_vorticity_at next to the centre (theory 2 A pi^2 sin sin) | 1.9704 | 1.97181 | 7.14e-04 |
| `vorticity-max` | fluid_max_abs_vorticity (theory 2 A pi^2 at the centre) | 1.9704 | 1.97392 | 1.78e-03 |
| `circulation-cell` | fluid_total_circulation, the mean vorticity (theory A x 8: the line integral of the velocity round the walls) | 0.799714 | 0.8 | 3.57e-04 |
| `divergence-cell` | fluid_max_divergence of the analytic cell (theory 0; central differences of a sine) | 7.15256e-07 | 0 | 7.15e-07 |
| `energy-after-stroke` | water energy 30 s after a 1 s stroke over its peak (it only decays once nothing pushes) | 0.0388319 | 0 | 3.88e-02 |
| `energy-no-growth` | largest relative energy gain in a step from 8 s on, the disc at rest (theory: none) | 0 | 0 | 0.00e+00 |
| `mass-disc` | mass of the disc, area x unit density (theory pi r^2) | 0.00282743 | 0.00282743 | 5.43e-09 |
| `first-step-speed` | speed after the first 0.02 s stroke from rest (theory F dt / (2 m): added mass doubles the inertia) | 0.0212207 | 0.0212207 | 3.57e-08 |
| `terminal-relative` | speed through the local water when the disc is fastest (theory: thrust = (0.002 + 0.03 w) w, w = 0.4152) | 0.413729 | 0.415121 | 3.35e-03 |
| `terminal-speed` | speed over the ground at that moment (the disc drags water along with it, so this is not the same number) | 0.458963 | 0.415121 | 1.06e-01 |
| `drag-at-top` | drag force the model reports there (theory: equal to the thrust 0.006 when the speed has settled) | 0.00596262 | 0.006 | 6.23e-03 |
| `reynolds-top` | Reynolds number at that speed, diameter 0.06, nu = 0.0004 (theory U D / nu) | 68.8445 | 68.8445 | 2.86e-08 |
| `drag-at-0.5` | drag at a relative speed of 0.5 (theory (0.002 + 0.03 x 0.5) x 0.5) | 0.0085 | 0.0085 | 5.52e-08 |
| `third-law` | momentum given to the water in one step over minus the momentum the disc gained (2 m dv) (theory 1) | 1 | 1 | 3.37e-07 |
| `turn-rate` | heading after 1 s of full turn (theory 2.5 rad) | 2.5 | 2.5 | 4.77e-07 |
| `wall-clamp-x` | x of a disc thrown at the right wall at speed 3 (theory 1 - radius) | 0.97 | 0.97 | 2.95e-08 |
| `wall-clamp-v` | its speed into the wall afterwards (theory 0: the wall takes it all, no bounce) | 0 | 0 | 0.00e+00 |
| `reynolds-default` | reynolds_number(0.5, 0.06, 0.0004) (theory 75) | 75 | 75 | 0.00e+00 |
| `sample-linear` | largest error of fluid_sample on a linear field at 500 points (theory 0: bilinear interpolation is exact for it) | 1.13249e-07 | 0 | 1.13e-07 |
| `sample-sine` | largest error sampling the cell's u along y = 0.25 (second order: about (pi h)^2 / 8 of the amplitude 0.22) | 0.000237813 | 0 | 2.38e-04 |
| `sample-clamped` | u sampled far outside the square: clamped to the corner cell, finite (1 = finite) | 1 | 1 | 0.00e+00 |
| `view-time` | VIEW_TIME after 50 calls of view_step (theory 50 x 0.02 s) | 1 | 1 | 0.00e+00 |
| `view-speed` | VIEW_SPEED after 1 s of strokes, against the same 50 steps made directly (the adapter starts the disc where it starts it; speed only) | 0.435478 | 0.435371 | 2.44e-04 |
| `view-radius` | VIEW_RADIUS (theory 0.03, the disc that collides and drags) | 0.03 | 0.03 | 2.24e-08 |
| `jet-game-start` | current against the swimmer where reach 1 starts, from the game layer itself (against the stand-alone spin-up at strength 0.45) | 0.029433 | 0.029433 | 0.00e+00 |
| `jet-flow-reach1` | sluice strength of reach 1 (theory BASE_FLOW[0]) | 0.45 | 0.45 | 2.65e-08 |
| `jet-peak-0.45` | fastest water on the centre line at strength 0.45 [domain/s] (the disc's terminal speed through water is 0.415) | 0.241975 | 0 | 2.42e-01 |
| `jet-peak-0.62` | the same at strength 0.62, the last reach | 0.284454 | 0 | 2.84e-01 |
| `jet-linear` | peak at 0.62 over peak at 0.30, against the ratio of the strengths (a jet is not linear in its forcing) | 1.42096 | 2.06667 | 3.12e-01 |
| `stamina-empty-pace` | seconds of strokes until the swimmer is winded, with Silt setting the pace (theory 1 / (0.50 x 0.60)) | 3.34 | 3.33333 | 2.00e-03 |
| `stamina-empty-alone` | the same with the pace effect off (theory 1 / 0.50) | 2.02 | 2 | 1.00e-02 |
| `stamina-back` | seconds winded before strokes work again (theory 0.35 / 0.25) | 1.42 | 1.4 | 1.43e-02 |
| `ease-time` | seconds a coasting swimmer waits for the jet to weaken the first time (theory EASE_SECONDS 10) | 10 | 10 | 0.00e+00 |
| `ease-factor` | jet strength after 35 s of coasting over the start (theory 0.85^3) | 0.614125 | 0.614125 | 1.35e-07 |
| `coast-drift-x` | x of the disc after coasting for 35 s from 0.12: the eddies beside the jet carry it | 0.97 | 0 | 9.70e-01 |
| `ease-no-sweep` | sweeps counted in those 35 s (the coasting disc is carried round by the return flow and never loses 0.20 of ground) | 0 | 0 | 0.00e+00 |
| `sweep-count` | sweeps added when the swimmer is found 0.26 behind its best x (theory 1; the rule is a loss above 0.20) | 1 | 1 | 0.00e+00 |
| `sweep-on-teleport` | sweeps counted before that, when the test moved the swimmer forward by hand (theory 0) | 0 | 0 | 0.00e+00 |
| `sweep-ease` | jet strength after the sweep over before (theory EASE_FLOW 0.85) | 0.85 | 0.85 | 7.79e-09 |
| `sweep-banner` | banner raised (theory JB_SWEPT = 2) | 2 | 2 | 0.00e+00 |
| `gate-clears` | reach index after a step taken 0.028 from the gate (theory 1: reach 1 cleared) | 1 | 1 | 0.00e+00 |
| `gate-scene` | scene opened (theory JS_CULVERT = 1) | 1 | 1 | 0.00e+00 |
| `ditch-marks` | marks on the ditch route (theory 3) | 3 | 3 | 0.00e+00 |
| `ditch-flow` | jet strength in the ditch (theory 0.60 x 0.60) | 0.36 | 0.36 | 3.97e-08 |
| `weir-hold-need` | journey_hold_need at the weir (theory WEIR_HOLD 18 s) | 18 | 18 | 0.00e+00 |
| `hold-radius` | journey_hold_radius (theory 0.14) | 0.14 | 0.14 | 4.26e-09 |
| `weir-hold-time` | seconds held on the weir mark before the next mark opens (theory 18) | 18 | 18 | 0.00e+00 |
| `scenes-shown` | scenes opened in one whole journey (theory 8: outlet, culvert, eddy, warm, taste, reeds, surge, spring) | 8 | 8 | 0.00e+00 |
| `ending-stay` | journey_ending after staying with Silt (theory 0) | 0 | 0 | 0.00e+00 |
| `ending-leave` | journey_ending after leaving at once (theory 1) | 1 | 1 | 0.00e+00 |
| `journey-time-stay` | swimming seconds of that journey, staying (one run of the bench policy; no theory) | 100.035 | 0 | 1.00e+02 |
| `journey-time-leave` | the same policy leaving at once | 100.595 | 0 | 1.01e+02 |
| `heavy-scenes` | scenes that journey_heavy marks (theory 2: reeds, surge) | 2 | 2 | 0.00e+00 |
| `choice-scenes` | scenes with two options (theory 2: culvert, reeds) | 2 | 2 | 0.00e+00 |
| `scene-first` | scene open right after journey_init (theory JS_OUTLET = 0) | 0 | 0 | 0.00e+00 |
| `ending-before` | journey_ending before the spring (theory -1) | -1 | -1 | 0.00e+00 |

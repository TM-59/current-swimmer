#ifndef WAKE_H
#define WAKE_H

/* M2: flow past a fixed circular cylinder in a channel (a separate solver; the pool of fluid.c is unchanged).
 *
 * Units: the cylinder diameter D = 1, the inflow speed U = 1, density 1, so the Reynolds number is 1 / visc
 * and times are in D / U. The channel is WAKE_LX x WAKE_LY diameters with the cylinder centre at
 * (WAKE_CX, WAKE_LY / 2). Inflow at the left (u = U, v = 0), zero-gradient outflow at the right with the
 * pressure fixed to 0, free-slip walls at the top and bottom (blockage D / LY).
 *
 * The step is the same split as Stable Fluids (diffusion, semi-Lagrangian advection, pressure projection),
 * but on a staggered (MAC) grid, with second-order MacCormack advection, an RK2 back-trace, and a pressure
 * solve iterated to a residual tolerance. The cylinder is a volume-penalised obstacle: on every velocity
 * face its solid fraction chi in [0, 1] is removed each step (u <- (1 - chi) u, the limit eta -> 0 of the
 * Brinkman term -chi u / eta), and the projection knows about the solid: it solves
 * div((1 - chi) grad phi) = div u and corrects u -= (1 - chi) grad phi, so the pressure never pushes water
 * back through a solid face. The force on the cylinder is the momentum removed by the penalty plus the
 * pressure impulse the solid faces withheld, per unit time. */

enum { WAKE_LX = 16, WAKE_LY = 8, WAKE_CX = 4 };

typedef struct {
    int n;              /* cells per diameter */
    int nx, ny;         /* cells along and across the channel */
    float h;            /* cell size [D] */
    float visc;         /* kinematic viscosity [D U] = 1 / Re */
    float cx, cy, r;    /* cylinder centre and radius [D] */
    float *u, *v;       /* face velocities: u (nx + 1) x (ny + 2), v (nx + 2) x (ny + 1) */
    float *u0, *v0, *u1, *v1, *u2, *v2;
    float *chiu, *chiv; /* solid fraction on each face */
    float *p, *div;     /* pressure (times dt) and divergence on (nx + 2) x (ny + 2) cells */
    double t;           /* time [D / U] */
    double fx, fy;      /* force on the cylinder per unit span from the last step [rho U^2 D] */
    int sweeps;         /* pressure sweeps used by the last projection */
    int first_order;    /* 1: plain semi-Lagrangian advection as in fluid.c, for comparison (default 0) */
    int split_penalty;  /* 1: the M2 scheme, a plain projection after the penalty (leaks through the solid and
                           reads the drag high); kept for the comparison of docs/theory/05-wake.md (default 0) */
    int length;         /* channel length [D] */
    float dt;           /* step of the last wake_step [D / U] */
} Wake;

/* Everything that can be varied in the studies. wake_init uses 16 x 8 diameters, the centre 4 D from the
 * inflow, and the wall at the radius. */
typedef struct {
    int cells;          /* cells per diameter, >= 2 */
    float reynolds;
    int length, height; /* channel size [D] */
    int upstream;       /* inflow to cylinder centre [D] */
    float wall_shift;   /* moves the middle of the one-cell penalised wall outwards by this many cells */
} WakeSetup;

int wake_init(Wake *w, int cells_per_diameter, float reynolds);
/* The same with a channel `height` diameters high (blockage 1 / height); wake_init uses WAKE_LY. */
int wake_init_channel(Wake *w, int cells_per_diameter, float reynolds, int height);
int wake_init_setup(Wake *w, const WakeSetup *setup);
void wake_free(Wake *w);
/* Largest stable step for this grid: half a cell per step at the inflow speed. */
float wake_dt(const Wake *w);
void wake_step(Wake *w, float dt);

/* C_D = 2 F_x / (rho U^2 D) and C_L = 2 F_y / (rho U^2 D) of the last step. */
float wake_drag_coefficient(const Wake *w);
float wake_lift_coefficient(const Wake *w);
/* Drag coefficient from the momentum balance of the square box of half-width `half` diameters around the
 * cylinder, taken on the field after the last step: momentum flux in minus out, pressure (phi / dt) on the
 * left and right faces, viscous stress on all four. Averaged over whole lift periods it should equal the
 * mean of wake_drag_coefficient; the difference measures what the advection scheme does not conserve. */
float wake_control_volume_drag(const Wake *w, float half);
/* Vorticity at the cell (i, j) centre, 0 <= i < nx, 0 <= j < ny [U / D]. */
float wake_vorticity_at(const Wake *w, int i, int j);
/* Largest |div u| over the fluid cells after the last projection [U / D]. */
float wake_max_divergence(const Wake *w);
/* Bilinear velocity at (x, y) in diameters. */
void wake_sample(const Wake *w, float x, float y, float *u, float *v);
/* Length of the closed recirculation behind the cylinder (from its rear point to where u on the centre
 * line turns positive again) [D]; 0 when no reversed flow is found. */
float wake_recirculation_length(const Wake *w);

/* St = f D / U. */
float strouhal_number(float frequency, float diameter, float speed);

/* Statistics of a lift and drag record sampled every dt, using only samples at index >= from:
 * frequency from the mean spacing of upward zero crossings of C_L - mean(C_L) (linearly interpolated),
 * C_D averaged over whole lift periods, amplitudes as half the peak-to-peak. periods = 0 means no
 * oscillation was found (fewer than 3 crossings, or a C_L amplitude below 0.01, the round-off level of a
 * steady wake): frequency 0 and C_D averaged over the whole window. */
typedef struct {
    float frequency, strouhal, cd_mean, cd_amp, cl_amp;
    int periods;
} WakeStats;
WakeStats wake_stats(const float *cl, const float *cd, int count, int from, float dt);

/* Runs the wake for t_end (in D / U) from rest-plus-a-nudge with step dt (wake_dt when dt <= 0) and
 * measures over the last half of the run. When cl or cd are not NULL they receive one value per step
 * (cap entries at most). lr receives the recirculation length at the end. Returns -1 on allocation failure. */
int wake_run(int cells_per_diameter, float reynolds, float t_end, float dt, WakeStats *stats, float *lr,
             float *cl, float *cd, int cap, int *count);

#endif

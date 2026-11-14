/* Regenerates every plotted figure of docs/theory and the table of measured values
 * (docs/theory/figures/values.md). Run with `make theory-figs`. Each figure is produced by the
 * same fluid solver (src/fluid.c), disc model (src/swimmer.c), browser adapter (src/view.c) and
 * story layer (src/journey.c) the program uses; nothing here changes them. */
#define _POSIX_C_SOURCE 200809L
#include "../src/fluid.h"
#include "../src/journey.h"
#include "../src/svg.h"
#include "../src/swimmer.h"
#include "../src/view.h"
#include "../src/render.h"
#include "../src/wake.h"
#include "story_run.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PI 3.14159265358979323846
#define MAXN 4096
#define AT(f, i, j) ((i) + ((f)->n + 2) * (j))

static char dir[512];
static FILE *values;
static float xs[MAXN], ya[MAXN], yb[MAXN], yc[MAXN];

static const char *path(const char *name)
{
    static char buf[640];
    snprintf(buf, sizeof buf, "%s/%s", dir, name);
    return buf;
}

static void plot(const char *name, const char *title, const char *xl, const char *yl, int count, const Series *s, int ns)
{
    if (svg_plot(path(name), title, xl, yl, xs, count, s, ns) != 0) {
        fprintf(stderr, "cannot write %s\n", name);
        exit(1);
    }
}

/* One row of the measured-values table: id | what | measured | theory | error (relative; absolute when theory is 0). */
static void value(const char *id, const char *what, double measured, double theory)
{
    double err = theory != 0.0 ? fabs(measured / theory - 1.0) : fabs(measured - theory);
    fprintf(values, "| `%s` | %s | %.6g | %.6g | %.2e |\n", id, what, measured, theory, err);
}

static void must(int failed)
{
    if (failed) { fprintf(stderr, "out of memory\n"); exit(1); }
}

/* The cell of streamfunction psi = A sin(pi x) sin(pi y): u = dpsi/dy, v = -dpsi/dx. It satisfies the walls
 * exactly (no flow through them, no shear on them) and is an exact decaying solution of the Navier-Stokes equations. */
static void set_cell(Fluid *f, double amp)
{
    int n = f->n;
    double h = 1.0 / n;
    for (int j = 0; j <= n + 1; j++)
        for (int i = 0; i <= n + 1; i++) {
            double x = (i - 0.5) * h, y = (j - 0.5) * h;
            f->u[AT(f, i, j)] = (float)(amp * PI * sin(PI * x) * cos(PI * y));
            f->v[AT(f, i, j)] = (float)(-amp * PI * cos(PI * x) * sin(PI * y));
        }
}

static double momentum_x(const Fluid *f)
{
    double sum = 0.0;
    for (int j = 1; j <= f->n; j++)
        for (int i = 1; i <= f->n; i++) sum += f->u[AT(f, i, j)];
    return sum / ((double)f->n * f->n);
}

/* S03: the walls. A jet aimed at the right wall: the normal velocity is mirrored with its sign flipped in the
 * ghost cell (zero on the wall), the tangential velocity is copied (no shear on the wall). */
static void fig_walls(void)
{
    Fluid f;
    must(fluid_init(&f, 48, 0.0004f));
    for (int k = 0; k < 150; k++) {
        fluid_add_force(&f, 0.75f, 0.42f, 1.2f, 0.5f, 0.07f, 0.02f);
        fluid_step(&f, 0.02f);
    }
    int n = f.n, row = 24, count = 0;
    for (int i = 30; i <= n + 1; i++, count++) {
        xs[count] = (float)((i - 0.5) / n);
        ya[count] = f.u[AT(&f, i, row)];
        yb[count] = f.v[AT(&f, i, row)];
    }
    double worst_normal = 0.0, worst_tangent = 0.0, flux = 0.0;
    for (int j = 1; j <= n; j++) {
        worst_normal = fmax(worst_normal, fabs(f.u[AT(&f, n + 1, j)] + f.u[AT(&f, n, j)]));
        worst_normal = fmax(worst_normal, fabs(f.u[AT(&f, 0, j)] + f.u[AT(&f, 1, j)]));
        worst_tangent = fmax(worst_tangent, fabs(f.v[AT(&f, n + 1, j)] - f.v[AT(&f, n, j)]));
        flux += 0.5 * (f.u[AT(&f, n + 1, j)] + f.u[AT(&f, n, j)]) / n;
    }
    value("wall-normal", "largest sum u(ghost) + u(first cell), in size, on the left and right walls after 3 s of a jet (theory 0: no flow through the wall)", worst_normal, 0.0);
    value("wall-tangent", "largest difference v(ghost) - v(first cell), in size, on the right wall (theory 0: no shear, a free-slip wall)", worst_tangent, 0.0);
    value("wall-flux", "volume flux through the right wall, sum of the face velocities x h (theory 0)", flux, 0.0);
    value("wall-slip", "tangential speed in the first cell at the wall where the jet strikes it: not zero, the wall does not hold the water", fabs(f.v[AT(&f, n, row + 6)]), 0.0);
    Series s[] = {{"u (normal)", ya, "#1f77b4"}, {"v (tangential)", yb, "#d62728"}};
    plot("walls.svg", "A jet meeting the right wall (last point is the ghost cell)", "x [domain]", "velocity [domain/s]", count, s, 2);
    fluid_free(&f);
}

/* S04: implicit diffusion. A weak cell (so advection is negligible) decays as exp(-2 nu pi^2 t). */
static void fig_diffusion(void)
{
    Fluid f;
    float visc = 0.005f, dt = 0.02f;
    must(fluid_init(&f, 48, visc));
    set_cell(&f, 1e-3);
    int n = f.n, c = n / 4, count = 0;
    double h = 1.0 / n, u0 = f.u[AT(&f, n / 2, c)];
    double lambda = 2.0 * (2.0 - 2.0 * cos(PI * h)) / (h * h);   /* eigenvalue of the 5-point Laplacian for this mode */
    double per_step = 1.0 / (1.0 + visc * dt * lambda), meas = 1.0;
    for (int k = 0; k <= 1000; k++) {
        if (k % 10 == 0) {
            xs[count] = (float)(k * dt);
            ya[count] = (float)(f.u[AT(&f, n / 2, c)] / u0);
            yb[count] = (float)exp(-2.0 * visc * PI * PI * k * dt);
            yc[count] = (float)pow(per_step, k);
            count++;
        }
        if (k == 500) meas = f.u[AT(&f, n / 2, c)] / u0;
        if (k < 1000) fluid_step(&f, dt);
    }
    value("diffusion-decay", "amplitude of a weak cell after 10 s, nu = 0.005, relative to the start (theory exp(-2 nu pi^2 t))", meas, exp(-2.0 * 0.005 * PI * PI * 10.0));
    value("diffusion-discrete", "the same against the backward-Euler factor 1 / (1 + nu dt lambda_h) to the 500th power", meas, pow(per_step, 500));
    /* Unconditional stability: a step 500 times the explicit limit h^2 / (4 nu). */
    Fluid g;
    must(fluid_init(&g, 48, 0.05f));
    set_cell(&g, 1e-3);
    double e0 = fluid_kinetic_energy(&g), worst = 0.0;
    float big = (float)(500.0 * h * h / (4.0 * 0.05));
    for (int k = 0; k < 20; k++) {
        fluid_step(&g, big);
        worst = fmax(worst, fluid_kinetic_energy(&g) / e0);
    }
    value("diffusion-big-step", "largest energy ratio over 20 steps of 500 x the explicit limit h^2 / (4 nu) (stable means it never exceeds 1)", worst, 0.0);
    Series s[] = {{"solver", ya, "#1f77b4"}, {"exp(-2 nu pi^2 t)", yb, "#d62728"}, {"backward Euler", yc, "#2ca02c"}};
    plot("diffusion.svg", "Viscous decay of a weak cell, nu = 0.005", "t [s]", "amplitude / start", count, s, 3);
    fluid_free(&f);
    fluid_free(&g);
}

/* S05: semi-Lagrangian advection. With no viscosity the energy should stay; the interpolation removes some. */
static void fig_advection(void)
{
    int sizes[3] = {24, 48, 96};
    float *out[3] = {ya, yb, yc};
    double loss[3], resid[3], grew = 0.0;
    int count = 0;
    for (int m = 0; m < 3; m++) {
        Fluid f;
        must(fluid_init(&f, sizes[m], 0.0f));
        set_cell(&f, 0.1);
        double e0 = fluid_kinetic_energy(&f), prev = e0, peak0 = fluid_max_abs_vorticity(&f);
        count = 0;
        for (int k = 0; k <= 500; k++) {
            double e = fluid_kinetic_energy(&f);
            if (k % 5 == 0) {
                xs[count] = (float)(k * 0.02);
                out[m][count] = (float)(e / e0);
                count++;
            }
            grew = fmax(grew, e / prev - 1.0);
            prev = e;
            if (k < 500) fluid_step(&f, 0.02f);
        }
        loss[m] = 1.0 - fluid_kinetic_energy(&f) / e0;
        resid[m] = fluid_max_divergence(&f) / fluid_max_abs_vorticity(&f);
        if (m == 1) value("advection-max-principle", "largest vorticity after 10 s over the largest at the start, n = 48, no viscosity (bilinear interpolation cannot overshoot: at most 1)", fluid_max_abs_vorticity(&f) / peak0, 1.0);
        fluid_free(&f);
    }
    value("advection-loss-24", "energy lost in 10 s by a cell of speed 0.31 with zero viscosity, n = 24 (ideal 0; numerical diffusion)", loss[0], 0.0);
    value("advection-loss-48", "the same on the game's grid, n = 48", loss[1], 0.0);
    value("advection-loss-96", "the same, n = 96", loss[2], 0.0);
    value("residual-24", "divergence left over the largest vorticity at the end of that run, n = 24 (0 for an exact pressure solve)", resid[0], 0.0);
    value("residual-48", "the same, n = 48", resid[1], 0.0);
    value("residual-96", "the same, n = 96: 24 sweeps reach less far on a finer grid", resid[2], 0.0);
    value("advection-nu-48", "viscosity that would cause the n = 48 loss, -ln(1 - loss) / (4 pi^2 t) (compare the game's 0.0004)", -log(1.0 - loss[1]) / (4.0 * PI * PI * 10.0), 0.0);
    value("advection-no-growth", "largest relative energy gain in any single step of those runs (theory: none)", grew, 0.0);
    /* A step far beyond the CFL limit stays bounded. */
    Fluid g;
    must(fluid_init(&g, 48, 0.0f));
    set_cell(&g, 0.1);
    double e0 = fluid_kinetic_energy(&g), worst = 0.0;
    for (int k = 0; k < 20; k++) { fluid_step(&g, 1.0f); worst = fmax(worst, fluid_kinetic_energy(&g) / e0); }
    value("advection-big-step", "largest energy ratio over 20 steps of dt = 1 s, where the water crosses 15 cells per step (stable: never above 1)", worst, 0.0);
    fluid_free(&g);
    Series s[] = {{"n = 24", ya, "#d62728"}, {"n = 48 (game)", yb, "#1f77b4"}, {"n = 96", yc, "#2ca02c"}};
    plot("advection.svg", "Energy of an inviscid cell: lost to interpolation only", "t [s]", "energy / start", count, s, 3);
}

/* S06: projection. A force blob makes a divergent field; one step later the divergence is small. */
static void fig_projection(void)
{
    Fluid f;
    must(fluid_init(&f, 48, 0.0004f));
    int count = 0;
    double before1 = 0.0, after1 = 0.0, worst_ratio = 0.0;
    for (int k = 0; k < 250; k++) {
        fluid_add_force(&f, 0.3f, 0.5f, 2.0f, 0.6f, 0.05f, 0.02f);
        double before = fluid_max_divergence(&f);
        fluid_step(&f, 0.02f);
        double after = fluid_max_divergence(&f);
        if (k == 0) { before1 = before; after1 = after; }
        worst_ratio = fmax(worst_ratio, after / fluid_max_abs_vorticity(&f));
        xs[count] = (float)((k + 1) * 0.02);
        ya[count] = (float)before;
        yb[count] = (float)after;
        yc[count] = fluid_max_abs_vorticity(&f);
        count++;
    }
    value("projection-before", "largest divergence, in size, right after the first force blob, before the step [1/s]", before1, 0.0);
    value("projection-after", "the same after the step: 24 Gauss-Seidel sweeps, twice (an exact solve gives 0)", after1, 0.0);
    value("projection-ratio", "after over before for that first step (the unit test requires below 0.1)", after1 / before1, 0.0);
    value("projection-vs-vorticity", "largest divergence over largest vorticity after any step of 5 s of forcing (0 for an exact projection)", worst_ratio, 0.0);
    Series s[] = {{"div after the force", ya, "#d62728"}, {"div after the step", yb, "#1f77b4"}, {"max |vorticity|", yc, "#888888"}};
    plot("projection.svg", "Divergence before and after each step under steady forcing", "t [s]", "[1/s]", count, s, 3);
    /* How far 24 sweeps get: the same right-hand side, projected again and again. */
    fluid_free(&f);
    must(fluid_init(&f, 48, 0.0f));
    fluid_add_force(&f, 0.3f, 0.5f, 2.0f, 0.6f, 0.05f, 0.02f);
    double d0 = fluid_max_divergence(&f);
    fluid_step(&f, 1e-6f);
    (void)d0;
    fluid_free(&f);
}

/* S07: the force blob, and S13: what the closed box does with the momentum. */
static void fig_force(void)
{
    Fluid f;
    must(fluid_init(&f, 48, 0.0004f));
    float sigma = 0.04f, fx = 0.006f;
    double area = 2.0 * PI * sigma * sigma;
    fluid_add_force(&f, 0.5f, 0.5f, (float)(fx / area), 0.0f, sigma, 0.02f);
    int n = f.n, count = 0;
    for (int i = 1; i <= n; i++, count++) {
        double x = (i - 0.5) / n, y = (24 - 0.5) / n, r2 = (x - 0.5) * (x - 0.5) + (y - 0.5) * (y - 0.5);
        xs[count] = (float)x;
        ya[count] = f.u[AT(&f, i, 24)];
        yb[count] = (float)(fx / area * 0.02 * exp(-r2 / (2.0 * sigma * sigma)));
    }
    double put = momentum_x(&f);
    value("force-impulse", "momentum added by one blob of force 0.006 for 0.02 s, summed over the grid (theory F dt)", put, 0.006 * 0.02);
    value("force-peak", "velocity added at the centre cell (theory F dt / (2 pi sigma^2), half a cell off the centre)", f.u[AT(&f, 24, 24)], fx / area * 0.02 * exp(-2.0 * (0.5 / n) * (0.5 / n) / (2.0 * sigma * sigma)));
    fluid_step(&f, 0.02f);
    value("box-momentum-1", "net x momentum of the water one step later, over the momentum put in (an exact projection in a closed box leaves 0)", momentum_x(&f) / put, 0.0);
    for (int k = 1; k < 100; k++) fluid_step(&f, 0.02f);
    value("box-momentum-100", "the same after 100 steps (2 s)", momentum_x(&f) / put, 0.0);
    for (int k = 100; k < 1000; k++) fluid_step(&f, 0.02f);
    value("box-momentum-1000", "the same after 1000 steps (20 s)", momentum_x(&f) / put, 0.0);
    Series s[] = {{"u added", ya, "#1f77b4"}, {"Gaussian", yb, "#d62728"}};
    plot("force.svg", "One force blob: velocity added along the row through its centre", "x [domain]", "u [domain/s]", count, s, 2);
    fluid_free(&f);
}

/* S08, S09: energy, vorticity and circulation of the cell, against the closed forms. */
static void fig_vorticity(void)
{
    Fluid f;
    must(fluid_init(&f, 48, 0.0004f));
    set_cell(&f, 0.1);
    int n = f.n, count = 0;
    for (int i = 1; i <= n; i++, count++) {
        double x = (i - 0.5) / n, y = (24 - 0.5) / n;
        xs[count] = (float)x;
        ya[count] = fluid_vorticity_at(&f, i, 24);
        yb[count] = (float)(0.1 * 2.0 * PI * PI * sin(PI * x) * sin(PI * y));
    }
    value("energy-cell", "fluid_kinetic_energy of the cell A = 0.1 (theory A^2 pi^2 / 4)", fluid_kinetic_energy(&f), 0.01 * PI * PI / 4.0);
    value("vorticity-centre", "fluid_vorticity_at next to the centre (theory 2 A pi^2 sin sin)", fluid_vorticity_at(&f, 24, 24), 0.1 * 2.0 * PI * PI * sin(PI * 23.5 / 48) * sin(PI * 23.5 / 48));
    value("vorticity-max", "fluid_max_abs_vorticity (theory 2 A pi^2 at the centre)", fluid_max_abs_vorticity(&f), 0.2 * PI * PI);
    value("circulation-cell", "fluid_total_circulation, the mean vorticity (theory A x 8: the line integral of the velocity round the walls)", fluid_total_circulation(&f), 0.8);
    value("divergence-cell", "fluid_max_divergence of the analytic cell (theory 0; central differences of a sine)", fluid_max_divergence(&f), 0.0);
    Series s[] = {{"fluid_vorticity_at", ya, "#1f77b4"}, {"2 A pi^2 sin sin", yb, "#d62728"}};
    plot("vorticity.svg", "Vorticity of the cell along the middle row", "x [domain]", "vorticity [1/s]", count, s, 2);
    /* Energy of the game's water after one second of strokes, then left alone. */
    Fluid g;
    Swimmer sw;
    must(fluid_init(&g, 48, 0.0004f));
    swimmer_init(&sw, 0.2f, 0.5f, 0.0f);
    count = 0;
    double peak = 0.0, prev = 0.0, grew = 0.0;
    for (int k = 0; k < 1500; k++) {
        Controls c = {0.0f, k < 50};
        swimmer_step(&sw, &g, &c, 0.02f);
        fluid_step(&g, 0.02f);
        double e = fluid_kinetic_energy(&g);
        if (k >= 400 && prev > 0.0) grew = fmax(grew, e / prev - 1.0);   /* the disc has stopped pushing by then */
        prev = e;
        peak = fmax(peak, e);
        if (k % 5 == 0) { xs[count] = (float)((k + 1) * 0.02); ya[count] = (float)(e * 1e6); count++; }
    }
    value("energy-after-stroke", "water energy 30 s after a 1 s stroke over its peak (it only decays once nothing pushes)", prev / peak, 0.0);
    value("energy-no-growth", "largest relative energy gain in a step from 8 s on, the disc at rest (theory: none)", grew, 0.0);
    Series e[] = {{"kinetic energy x 1e6", ya, "#1f77b4"}};
    plot("energy.svg", "Water energy after a one-second stroke", "t [s]", "mean u^2/2 x 1e6 [domain^2/s^2]", count, e, 1);
    fluid_free(&f);
    fluid_free(&g);
}

/* S10..S13: the disc. */
static void fig_swimmer(void)
{
    Fluid f;
    Swimmer s;
    must(fluid_init(&f, 48, 0.0004f));
    swimmer_init(&s, 0.08f, 0.5f, 0.0f);
    double meff = 2.0 * PI * 0.03 * 0.03, a0 = 0.006 / meff;
    double wt = (-0.002 + sqrt(0.002 * 0.002 + 4.0 * 0.03 * 0.006)) / (2.0 * 0.03);
    int count = 0;
    double v1 = 0.0, top = 0.0, rel_top = 0.0, re_top = 0.0, drag_top = 0.0, x_stop = 0.0;
    for (int k = 0; k < 400; k++) {
        Controls c = {0.0f, k < 90};
        float uf, vf;
        fluid_sample(&f, s.x, s.y, &uf, &vf);
        double rel = hypot(s.vx - uf, s.vy - vf);
        swimmer_step(&s, &f, &c, 0.02f);
        fluid_step(&f, 0.02f);
        if (k == 0) v1 = s.vx;
        if (k < 90 && swimmer_speed(&s) > top) { top = swimmer_speed(&s); rel_top = rel; re_top = reynolds_number((float)top, 2.0f * s.radius, f.visc); drag_top = s.drag; }
        xs[count] = (float)((k + 1) * 0.02);
        ya[count] = swimmer_speed(&s);
        yb[count] = (float)fmin(a0 * (k + 1) * 0.02, 0.6);
        yc[count] = (float)wt;
        count++;
        x_stop = s.x;
    }
    value("mass-disc", "mass of the disc, area x unit density (theory pi r^2)", s.mass, PI * 0.0009);
    value("first-step-speed", "speed after the first 0.02 s stroke from rest (theory F dt / (2 m): added mass doubles the inertia)", v1, a0 * 0.02);
    value("terminal-relative", "speed through the local water when the disc is fastest (theory: thrust = (0.002 + 0.03 w) w, w = 0.4152)", rel_top, wt);
    value("terminal-speed", "speed over the ground at that moment (the disc drags water along with it, so this is not the same number)", top, wt);
    value("drag-at-top", "drag force the model reports there (theory: equal to the thrust 0.006 when the speed has settled)", drag_top, 0.006);
    value("reynolds-top", "Reynolds number at that speed, diameter 0.06, nu = 0.0004 (theory U D / nu)", re_top, top * 0.06 / 0.0004);
    (void)x_stop;
    Series p[] = {{"speed", ya, "#1f77b4"}, {"F t / (2 m)", yb, "#2ca02c"}, {"terminal 0.415", yc, "#d62728"}};
    plot("swimmer-speed.svg", "Stroke for 1.8 s, then coast", "t [s]", "speed [domain/s]", count, p, 3);

    /* Drag law: force against relative speed, read back from the model in still water (no time to drag water along). */
    count = 0;
    for (int i = 0; i <= 60; i++) {
        Fluid g;
        Swimmer q;
        must(fluid_init(&g, 16, 0.0004f));
        swimmer_init(&q, 0.5f, 0.5f, 0.0f);
        q.vx = 0.01f * i;
        Controls c = {0.0f, 0};
        swimmer_step(&q, &g, &c, 1e-6f);
        xs[count] = 0.01f * i;
        ya[count] = q.drag * 1000.0f;
        yb[count] = (float)(1000.0 * (0.002 + 0.03 * 0.01 * i) * 0.01 * i);
        yc[count] = 6.0f;
        count++;
        if (i == 50) value("drag-at-0.5", "drag at a relative speed of 0.5 (theory (0.002 + 0.03 x 0.5) x 0.5)", q.drag, 0.0085);
        fluid_free(&g);
    }
    Series d[] = {{"drag x 1000", ya, "#1f77b4"}, {"(0.002 + 0.03 w) w", yb, "#d62728"}, {"thrust x 1000", yc, "#888888"}};
    plot("drag.svg", "Drag on the disc against its speed through the water", "relative speed w [domain/s]", "force x 1000", count, d, 3);

    /* Third law: the momentum the two blobs put into the water in one step is minus what the body gained. */
    Fluid g;
    Swimmer q;
    must(fluid_init(&g, 48, 0.0004f));
    swimmer_init(&q, 0.5f, 0.5f, 0.0f);
    q.vx = 0.2f;
    Controls c = {0.0f, 1};
    double before = meff * q.vx;
    swimmer_step(&q, &g, &c, 0.02f);
    value("third-law", "momentum given to the water in one step over minus the momentum the disc gained (2 m dv) (theory 1)", momentum_x(&g) / -(meff * q.vx - before), 1.0);
    fluid_free(&g);

    /* Heading and walls. */
    Swimmer t;
    must(fluid_init(&g, 16, 0.0004f));
    swimmer_init(&t, 0.5f, 0.5f, 0.0f);
    Controls turn = {1.0f, 0};
    for (int k = 0; k < 50; k++) swimmer_step(&t, &g, &turn, 0.02f);
    value("turn-rate", "heading after 1 s of full turn (theory 2.5 rad)", t.heading, 2.5);
    swimmer_init(&t, 0.9f, 0.5f, 0.0f);
    t.vx = 3.0f;
    Controls none = {0.0f, 0};
    for (int k = 0; k < 10; k++) swimmer_step(&t, &g, &none, 0.02f);
    value("wall-clamp-x", "x of a disc thrown at the right wall at speed 3 (theory 1 - radius)", t.x, 0.97);
    value("wall-clamp-v", "its speed into the wall afterwards (theory 0: the wall takes it all, no bounce)", t.vx, 0.0);
    fluid_free(&g);
    fluid_free(&f);
}

/* S14: Reynolds number. */
static void fig_reynolds(void)
{
    int count = 0;
    for (int i = 0; i <= 60; i++, count++) {
        xs[count] = 0.01f * i;
        ya[count] = reynolds_number(0.01f * i, 0.06f, 0.0004f);
        yb[count] = reynolds_number(0.01f * i, 0.06f, 0.0004f + 0.0018f);
    }
    value("reynolds-default", "reynolds_number(0.5, 0.06, 0.0004) (theory 75)", reynolds_number(0.5f, 0.06f, 0.0004f), 75.0);
    Series s[] = {{"nu = 0.0004 (set)", ya, "#1f77b4"}, {"with numerical 0.0018", yb, "#d62728"}};
    plot("reynolds.svg", "Reynolds number of the disc against its speed", "speed [domain/s]", "Re = U D / nu", count, s, 2);
}

/* S15: bilinear sampling. */
static void fig_sample(void)
{
    Fluid f;
    must(fluid_init(&f, 48, 0.0004f));
    int n = f.n;
    for (int j = 0; j <= n + 1; j++)
        for (int i = 0; i <= n + 1; i++) {
            double x = (i - 0.5) / n, y = (j - 0.5) / n;
            f.u[AT(&f, i, j)] = (float)(0.3 + 0.7 * x - 0.2 * y);
            f.v[AT(&f, i, j)] = (float)(0.1 * x + 0.4 * y);
        }
    double worst = 0.0;
    for (int k = 0; k < 500; k++) {
        float x = 0.02f + 0.96f * (float)((k * 37) % 500) / 500.0f, y = 0.02f + 0.96f * (float)((k * 91) % 500) / 500.0f, u, v;
        fluid_sample(&f, x, y, &u, &v);
        worst = fmax(worst, fabs(u - (0.3 + 0.7 * x - 0.2 * y)));
        worst = fmax(worst, fabs(v - (0.1 * x + 0.4 * y)));
    }
    value("sample-linear", "largest error of fluid_sample on a linear field at 500 points (theory 0: bilinear interpolation is exact for it)", worst, 0.0);
    set_cell(&f, 0.1);
    int count = 0;
    double worst_sine = 0.0;
    for (int k = 0; k <= 480; k++, count++) {
        float x = (float)k / 480.0f, u, v;
        fluid_sample(&f, x, 0.25f, &u, &v);
        double exact = 0.1 * PI * sin(PI * x) * cos(PI * 0.25);
        xs[count] = x;
        ya[count] = u;
        yb[count] = (float)exact;
        if (x > 0.02f && x < 0.98f) worst_sine = fmax(worst_sine, fabs(u - exact));
    }
    value("sample-sine", "largest error sampling the cell's u along y = 0.25 (second order: about (pi h)^2 / 8 of the amplitude 0.22)", worst_sine, 0.0);
    float u, v;
    fluid_sample(&f, -3.0f, 9.0f, &u, &v);
    value("sample-clamped", "u sampled far outside the square: clamped to the corner cell, finite (1 = finite)", isfinite(u) && isfinite(v), 1.0);
    Series s[] = {{"fluid_sample", ya, "#1f77b4"}, {"exact", yb, "#d62728"}};
    plot("sample.svg", "Sampling between cell centres: u of the cell along y = 0.25", "x [domain]", "u [domain/s]", count, s, 2);
    fluid_free(&f);
}

/* S16: the browser adapter reads the same model. */
static void val_view(void)
{
    if (view_init()) { fprintf(stderr, "view_init failed\n"); exit(1); }
    for (int k = 0; k < 50; k++) view_step(0.0f, 1);
    const float *st = view_state();
    Fluid f;
    Swimmer s;
    must(fluid_init(&f, VIEW_GRID, 0.0004f));
    swimmer_init(&s, 0.5f, 0.5f, 0.0f);
    Controls c = {0.0f, 1};
    for (int k = 0; k < 50; k++) { swimmer_step(&s, &f, &c, 0.02f); fluid_step(&f, 0.02f); }
    value("view-time", "VIEW_TIME after 50 calls of view_step (theory 50 x 0.02 s)", st[VIEW_TIME], 1.0);
    value("view-speed", "VIEW_SPEED after 1 s of strokes, against the same 50 steps made directly (the adapter starts the disc where it starts it; speed only)", st[VIEW_SPEED], swimmer_speed(&s));
    value("view-radius", "VIEW_RADIUS (theory 0.03, the disc that collides and drags)", st[VIEW_RADIUS], 0.03);
    fluid_free(&f);
}

/* S17: the sluice jet after its 10 s spin-up, for three strengths. */
static void fig_jet(void)
{
    float flows[3] = {0.30f, 0.45f, 0.62f};
    float *out[3] = {ya, yb, yc};
    int count = 0;
    double at_start[3], peak[3];
    for (int m = 0; m < 3; m++) {
        Fluid f;
        must(fluid_init(&f, 48, 0.0004f));
        for (int k = 0; k < 500; k++) {
            fluid_add_force(&f, 0.96f, 0.5f, -flows[m], 0.0f, 0.07f, 0.02f);
            fluid_step(&f, 0.02f);
        }
        count = 0;
        peak[m] = 0.0;
        for (int k = 0; k <= 96; k++, count++) {
            float x = (float)k / 96.0f, u, v;
            fluid_sample(&f, x, 0.5f, &u, &v);
            xs[count] = x;
            out[m][count] = -u;
            peak[m] = fmax(peak[m], -u);
        }
        float u, v;
        fluid_sample(&f, 0.12f, 0.5f, &u, &v);
        at_start[m] = -u;
        fluid_free(&f);
    }
    Journey j;
    if (journey_init(&j, JE_ALL)) exit(1);
    journey_answer(&j, 0);
    float u, v;
    fluid_sample(&j.water, 0.12f, 0.5f, &u, &v);
    value("jet-game-start", "current against the swimmer where reach 1 starts, from the game layer itself (against the stand-alone spin-up at strength 0.45)", -u, at_start[1]);
    value("jet-flow-reach1", "sluice strength of reach 1 (theory BASE_FLOW[0])", j.flow, 0.45);
    value("jet-peak-0.45", "fastest water on the centre line at strength 0.45 [domain/s] (the disc's terminal speed through water is 0.415)", peak[1], 0.0);
    value("jet-peak-0.62", "the same at strength 0.62, the last reach", peak[2], 0.0);
    value("jet-linear", "peak at 0.62 over peak at 0.30, against the ratio of the strengths (a jet is not linear in its forcing)", peak[2] / peak[0], 0.62 / 0.30);
    journey_free(&j);
    Series s[] = {{"strength 0.30", ya, "#2ca02c"}, {"0.45 (reach 1)", yb, "#1f77b4"}, {"0.62 (reach 5)", yc, "#d62728"}};
    plot("jet.svg", "The sluice jet after 10 s: speed towards the swimmer on the centre line", "x [domain]", "current [domain/s]", count, s, 3);
}

/* S18: stamina. */
static void fig_stamina(void)
{
    Journey a, b;
    if (journey_init(&a, JE_ALL) || journey_init(&b, 0u)) exit(1);
    journey_answer(&a, 0);
    journey_answer(&b, 0);
    a.fish.heading = b.fish.heading = (float)PI;   /* facing the left wall: the tail beats, the swimmer goes nowhere */
    int count = 0;
    double empty_a = -1.0, empty_b = -1.0, back_a = -1.0;
    for (int k = 0; k < 600; k++) {
        int stroke = k < 250 || k >= 420;
        journey_step(&a, 0.0f, stroke);
        journey_step(&b, 0.0f, stroke);
        double t = (k + 1) * 0.02;
        if (empty_a < 0.0 && a.winded) empty_a = t;
        if (empty_b < 0.0 && b.winded) empty_b = t;
        if (empty_a > 0.0 && back_a < 0.0 && !a.winded) back_a = t;
        xs[count] = (float)t;
        ya[count] = a.stamina;
        yb[count] = b.stamina;
        yc[count] = 0.35f;
        count++;
    }
    value("stamina-empty-pace", "seconds of strokes until the swimmer is winded, with Silt setting the pace (theory 1 / (0.50 x 0.60))", empty_a, 1.0 / 0.30);
    value("stamina-empty-alone", "the same with the pace effect off (theory 1 / 0.50)", empty_b, 2.0);
    value("stamina-back", "seconds winded before strokes work again (theory 0.35 / 0.25)", back_a - empty_a, 1.4);
    journey_free(&a);
    journey_free(&b);
    Series s[] = {{"with Silt's pace", ya, "#1f77b4"}, {"alone", yb, "#d62728"}, {"winded until 0.35", yc, "#888888"}};
    plot("stamina.svg", "Stamina: stroke held for 5 s, coast 3.4 s, stroke again", "t [s]", "stamina", count, s, 3);
}

/* S19, S20: marks, holds, sweeps and the easing of the jet. */
static void fig_rules(void)
{
    Journey j;
    if (journey_init(&j, JE_ALL)) exit(1);
    journey_answer(&j, 0);
    int count = 0;
    double first_ease = -1.0, flow0 = j.flow;
    for (int k = 0; k < 1750; k++) {
        journey_step(&j, 0.0f, 0);
        if (first_ease < 0.0 && j.flow < flow0) first_ease = (k + 1) * 0.02;
        if (k % 5 == 0) { xs[count] = (float)((k + 1) * 0.02); ya[count] = j.flow; yb[count] = j.fish.x; count++; }
    }
    value("ease-time", "seconds a coasting swimmer waits for the jet to weaken the first time (theory EASE_SECONDS 10)", first_ease, 10.0);
    value("ease-factor", "jet strength after 35 s of coasting over the start (theory 0.85^3)", j.flow / flow0, 0.85 * 0.85 * 0.85);
    value("coast-drift-x", "x of the disc after coasting for 35 s from 0.12: the eddies beside the jet carry it", j.fish.x, 0.0);
    value("ease-no-sweep", "sweeps counted in those 35 s (the coasting disc is carried round by the return flow and never loses 0.20 of ground)", j.sweeps, 0.0);
    Series s[] = {{"jet strength", ya, "#1f77b4"}, {"swimmer x", yb, "#888888"}};
    plot("easing.svg", "A swimmer who only coasts: the jet eases every 10 s", "t [s]", "strength, x [domain]", count, s, 2);
    /* A sweep, on a fresh reach: put the swimmer at 0.60, then let the bookkeeping see it 0.26 back. */
    journey_free(&j);
    if (journey_init(&j, JE_ALL)) exit(1);
    journey_answer(&j, 0);
    j.fish.x = 0.60f; j.fish.vx = 0.0f;
    journey_step(&j, 0.0f, 0);
    double flow1 = j.flow;
    int sweeps1 = j.sweeps;
    j.fish.x = 0.34f;
    journey_step(&j, 0.0f, 0);
    value("sweep-count", "sweeps added when the swimmer is found 0.26 behind its best x (theory 1; the rule is a loss above 0.20)", j.sweeps - sweeps1, 1.0);
    value("sweep-on-teleport", "sweeps counted before that, when the test moved the swimmer forward by hand (theory 0)", sweeps1, 0.0);
    value("sweep-ease", "jet strength after the sweep over before (theory EASE_FLOW 0.85)", j.flow / flow1, 0.85);
    value("sweep-banner", "banner raised (theory JB_SWEPT = 2)", j.banner, 2.0);
    /* The gate: within 0.07 of (0.82, 0.5) clears reach 1 and opens the next scene. */
    j.fish.x = 0.80f; j.fish.y = 0.52f; j.fish.vx = j.fish.vy = 0.0f;
    journey_step(&j, 0.0f, 0);
    value("gate-clears", "reach index after a step taken 0.028 from the gate (theory 1: reach 1 cleared)", j.reach, 1.0);
    value("gate-scene", "scene opened (theory JS_CULVERT = 1)", j.scene, 1.0);
    /* The ditch: three marks and an 18 s hold at the second. */
    journey_answer(&j, 1);
    value("ditch-marks", "marks on the ditch route (theory 3)", j.marks, 3.0);
    value("ditch-flow", "jet strength in the ditch (theory 0.60 x 0.60)", j.flow, 0.36);
    j.fish.x = j.mark_x[0]; j.fish.y = j.mark_y[0];
    journey_step(&j, 0.0f, 0);
    value("weir-hold-need", "journey_hold_need at the weir (theory WEIR_HOLD 18 s)", journey_hold_need(&j), 18.0);
    value("hold-radius", "journey_hold_radius (theory 0.14)", journey_hold_radius(), 0.14);
    int steps = 0;
    while (j.mark == 1 && steps < 2000) {
        j.fish.x = j.mark_x[1]; j.fish.y = j.mark_y[1]; j.fish.vx = j.fish.vy = 0.0f;
        journey_step(&j, 0.0f, 0);
        steps++;
    }
    value("weir-hold-time", "seconds held on the weir mark before the next mark opens (theory 18)", steps * 0.02, 18.0);
    journey_free(&j);
}

/* S21: scenes and whole journeys, flown by the bench's policy. */
static void val_story(void)
{
    RunResult stay, leave;
    run_journey(sub_seed(20261111ULL, 0, 9), JE_ALL, 0, 0, &stay);
    run_journey(sub_seed(20261111ULL, 0, 9), JE_ALL, 0, 1, &leave);
    value("scenes-shown", "scenes opened in one whole journey (theory 8: outlet, culvert, eddy, warm, taste, reeds, surge, spring)", stay.order_len, 8.0);
    value("ending-stay", "journey_ending after staying with Silt (theory 0)", stay.ending, 0.0);
    value("ending-leave", "journey_ending after leaving at once (theory 1)", leave.ending, 1.0);
    value("journey-time-stay", "swimming seconds of that journey, staying (one run of the bench policy; no theory)", stay.seconds, 0.0);
    value("journey-time-leave", "the same policy leaving at once", leave.seconds, 0.0);
    value("heavy-scenes", "scenes that journey_heavy marks (theory 2: reeds, surge)", journey_heavy(JS_REEDS) + journey_heavy(JS_SURGE) + journey_heavy(JS_OUTLET) + journey_heavy(JS_CULVERT) + journey_heavy(JS_EDDY) + journey_heavy(JS_WARM) + journey_heavy(JS_TASTE) + journey_heavy(JS_SPRING), 2.0);
    value("choice-scenes", "scenes with two options (theory 2: culvert, reeds)", (journey_options(JS_CULVERT) == 2) + (journey_options(JS_REEDS) == 2) + (journey_options(JS_OUTLET) == 2) + (journey_options(JS_SPRING) == 2), 2.0);
    Journey j;
    if (journey_init(&j, JE_ALL)) exit(1);
    value("scene-first", "scene open right after journey_init (theory JS_OUTLET = 0)", j.scene, 0.0);
    value("ending-before", "journey_ending before the spring (theory -1)", journey_ending(&j), -1.0);
    journey_free(&j);
}

/* ------------------------------------------------------------------ M2: the cylinder wake (page 5)
 * Every run starts from the same nudged uniform flow and measures over the second half of the run
 * (wake_stats). The runs are independent, so they go to child processes (at most WAKE_JOBS at a time);
 * each child writes its result into a pipe. Order and values do not depend on the scheduling. */
#define WAKE_JOBS 4
#define TRACE 1024
#define TRACE_EVERY 4
#define CV_HALF 1.5f

typedef struct {
    WakeSetup setup;
    int first_order, split;
    float t_end, dt_scale;
    const char *svg, *title;
} WakeJob;

typedef struct {
    WakeStats s;
    float lr, maxdiv, sweeps, cpu, dt, mass, cd_cv, inside;
    int steps, traced;
    float t[TRACE], cl[TRACE], cd[TRACE];
} WakeOut;

/* The default channel (16 x 8 D, centre 4 D from the inflow) on `cells` per diameter, 100 D/U. */
static WakeJob wjob(int cells, float re)
{
    WakeJob j = {{cells, re, WAKE_LX, WAKE_LY, WAKE_CX, 0.0f, 0}, 0, 0, 100.0f, 1.0f, NULL, NULL};
    return j;
}

static WakeJob wdomain(float re, int length, int upstream, int height)
{
    WakeJob j = wjob(8, re);
    j.setup.length = length; j.setup.upstream = upstream; j.setup.height = height;
    return j;
}

static void wake_job(const WakeJob *job, WakeOut *out)
{
    Wake w;
    memset(out, 0, sizeof *out);
    if (wake_init_setup(&w, &job->setup)) exit(1);
    w.first_order = job->first_order;
    w.split_penalty = job->split;
    clock_t start = clock();
    float dt = wake_dt(&w) * job->dt_scale;
    int steps = (int)(job->t_end / dt + 0.5f);
    float *cl = malloc((size_t)steps * sizeof(float)), *cd = malloc((size_t)steps * sizeof(float));
    float *cv = malloc((size_t)steps * sizeof(float));
    if (!cl || !cd || !cv) exit(1);
    double sweeps = 0.0;
    for (int k = 0; k < steps; k++) {
        wake_step(&w, dt);
        cl[k] = wake_lift_coefficient(&w);
        cd[k] = wake_drag_coefficient(&w);
        cv[k] = wake_control_volume_drag(&w, CV_HALF);
        sweeps += w.sweeps;
        if (k % TRACE_EVERY == 0 && out->traced < TRACE) {
            out->t[out->traced] = (float)w.t;
            out->cl[out->traced] = cl[k];
            out->cd[out->traced] = cd[k];
            out->traced++;
        }
    }
    out->s = wake_stats(cl, cd, steps, steps / 2, dt);
    out->cd_cv = wake_stats(cl, cv, steps, steps / 2, dt).cd_mean;
    out->lr = wake_recirculation_length(&w);
    out->maxdiv = wake_max_divergence(&w);
    out->sweeps = (float)(sweeps / steps);
    out->steps = steps;
    out->dt = dt;
    float ui, vi;
    wake_sample(&w, w.cx, w.cy, &ui, &vi);
    out->inside = sqrtf(ui * ui + vi * vi);
    double in = 0.0, outflow = 0.0;
    for (int j = 0; j < w.ny; j++) {
        float u, v;
        wake_sample(&w, 0.0f, ((float)j + 0.5f) * w.h, &u, &v);
        in += u;
        wake_sample(&w, (float)w.length, ((float)j + 0.5f) * w.h, &u, &v);
        outflow += u;
    }
    out->mass = (float)(outflow / in - 1.0);
    out->cpu = (float)((double)(clock() - start) / CLOCKS_PER_SEC);
    if (job->svg && render_wake_svg(&w, path(job->svg), job->title)) exit(1);
    free(cl); free(cd); free(cv);
    wake_free(&w);
}

static void wake_run_all(const WakeJob *jobs, WakeOut *outs, int count)
{
    int next = 0, running = 0, fds[64];
    pid_t pids[64];
    int done = 0;
    while (done < count) {
        while (running < WAKE_JOBS && next < count) {
            int fd[2];
            if (pipe(fd)) exit(1);
            pid_t pid = fork();
            if (pid < 0) exit(1);
            if (pid == 0) {
                close(fd[0]);
                static WakeOut out;
                wake_job(&jobs[next], &out);
                const char *p = (const char *)&out;
                size_t left = sizeof out;
                while (left > 0) {
                    ssize_t wrote = write(fd[1], p, left);
                    if (wrote <= 0) _exit(1);
                    p += wrote; left -= (size_t)wrote;
                }
                _exit(0);
            }
            close(fd[1]);
            fds[next] = fd[0];
            pids[next] = pid;
            next++;
            running++;
        }
        /* Collect the oldest running job (reading drains its pipe so the child can finish). */
        int k = done;
        char *p = (char *)&outs[k];
        size_t left = sizeof outs[k];
        while (left > 0) {
            ssize_t got = read(fds[k], p, left);
            if (got <= 0) { fprintf(stderr, "wake job %d failed\n", k); exit(1); }
            p += got; left -= (size_t)got;
        }
        close(fds[k]);
        int status;
        waitpid(pids[k], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) { fprintf(stderr, "wake job %d failed\n", k); exit(1); }
        const WakeSetup *s = &jobs[k].setup;
        fprintf(stderr, "wake: Re %.0f n %d %dx%d up %d shift %.2f%s%s dt x%.2f: St %.4f C_D %.4f (box %.4f) C_L amp %.4f L %.3f (%.0f s)\n",
                s->reynolds, s->cells, s->length, s->height, s->upstream, s->wall_shift, jobs[k].first_order ? " first-order" : "",
                jobs[k].split ? " split" : "", jobs[k].dt_scale, outs[k].s.strouhal, outs[k].s.cd_mean, outs[k].cd_cv,
                outs[k].s.cl_amp, outs[k].lr, outs[k].cpu);
        done++;
        running--;
    }
}

/* Roshko (1954), NACA Report 1191, eq. (2a), 50 < Re < 150: St = 0.212 (1 - 21.2 / Re). Used at 150 and above only as a guide. */
static float roshko(float re) { return 0.212f * (1.0f - 21.2f / re); }
/* Williamson (1989), J. Fluid Mech. 206, eq. (6), parallel shedding, data for Re 49..178. */
static float williamson(float re) { return -3.3265f / re + 0.1816f + 1.6e-4f * re; }

/* Two-dimensional computations in very wide domains, Qu et al. (2013), J. Fluids Struct. 39, Table 3:
 * mean C_D and St at Re 100, 150, 200. */
static const float QU_CD[3] = {1.317f, 1.305f, 1.316f};
static const float QU_ST[3] = {0.1649f, 0.1841f, 0.1958f};
/* Steady flow: C_D and closed-wake length L/D. Re 20: Dennis & Chang (1970) as tabulated by Fujii & Omori
 * (2025); Re 40: Gautier, Biau & Lamballais (2013), Table 2. */
#define LIT_CD20 2.05
#define LIT_LR20 0.94
#define LIT_CD40 1.4931
#define LIT_LR40 2.2360

static void fig_wake(void)
{
    enum { NRE = 9, J_FIRST = NRE, J_DT, J_N6, J_N12, J_N16, J_H16, J_H24, J_SPLIT, J_SPLIT_DT, J_SHIFT, J_UP, J_MID,
           J_WIDE, J_WIDE150, J_WIDE200, J_WIDE20, J_WIDE40, NJOBS };
    static const float res[NRE] = {20, 30, 40, 50, 60, 80, 100, 150, 200};
    static WakeJob jobs[NJOBS];
    static WakeOut outs[NJOBS];
    for (int k = 0; k < NRE; k++) jobs[k] = wjob(8, res[k]);
    jobs[1].svg = "wake-steady.svg";
    jobs[1].title = "Re 30: a steady pair of attached eddies, t = 100 D/U (8 cells per D)";
    jobs[7].svg = "wake-street.svg";
    jobs[7].title = "Re 150: a Karman vortex street, t = 100 D/U (8 cells per D)";
    jobs[J_FIRST] = wjob(8, 100.0f); jobs[J_FIRST].first_order = 1;
    jobs[J_DT] = wjob(8, 100.0f); jobs[J_DT].dt_scale = 0.5f;
    jobs[J_N6] = wjob(6, 100.0f);
    jobs[J_N12] = wjob(12, 100.0f);
    jobs[J_N16] = wjob(16, 100.0f);
    jobs[J_H16] = wdomain(100.0f, WAKE_LX, WAKE_CX, 16);
    jobs[J_H24] = wdomain(100.0f, WAKE_LX, WAKE_CX, 24);
    jobs[J_SPLIT] = wjob(8, 100.0f); jobs[J_SPLIT].split = 1;
    jobs[J_SPLIT_DT] = jobs[J_SPLIT]; jobs[J_SPLIT_DT].dt_scale = 0.5f;
    jobs[J_SHIFT] = wjob(8, 100.0f); jobs[J_SHIFT].setup.wall_shift = -0.5f;
    jobs[J_UP] = wdomain(100.0f, 24, 8, WAKE_LY);
    jobs[J_MID] = wdomain(100.0f, 24, 8, 16);
    jobs[J_WIDE] = wdomain(100.0f, 32, 12, 24);
    jobs[J_WIDE150] = wdomain(150.0f, 32, 12, 24);
    jobs[J_WIDE200] = wdomain(200.0f, 32, 12, 24);
    jobs[J_WIDE20] = wdomain(20.0f, 32, 12, 24);
    jobs[J_WIDE40] = wdomain(40.0f, 32, 12, 24);
    /* Longest first so the pool finishes early. */
    static const int order[NJOBS] = {J_WIDE200, J_WIDE150, J_WIDE, J_N16, J_MID, J_WIDE40, J_WIDE20, J_N12, J_H24, J_UP, J_H16,
                                     J_DT, J_SPLIT_DT, 0, 1, 2, 3, 4, 5, 6, 7, 8, J_FIRST, J_SPLIT, J_SHIFT, J_N6};
    static WakeJob queue[NJOBS];
    static WakeOut got[NJOBS];
    for (int k = 0; k < NJOBS; k++) queue[k] = jobs[order[k]];
    wake_run_all(queue, got, NJOBS);
    for (int k = 0; k < NJOBS; k++) outs[order[k]] = got[k];
    const WakeOut *r100 = &outs[6], *r150 = &outs[7], *r200 = &outs[8];
    const WakeOut *split = &outs[J_SPLIT], *wide = &outs[J_WIDE];

    /* S22: MacCormack against the plain semi-Lagrangian step of fluid.c, lift at Re 100. */
    int count = r100->traced < outs[J_FIRST].traced ? r100->traced : outs[J_FIRST].traced;
    for (int k = 0; k < count; k++) { xs[k] = r100->t[k]; ya[k] = r100->cl[k]; yb[k] = outs[J_FIRST].cl[k]; }
    Series adv[] = {{"MacCormack (used)", ya, "#2471a3"}, {"first-order semi-Lagrangian", yb, "#c0392b"}};
    plot("wake-advection.svg", "Lift at Re 100: the first-order step damps the street", "t [D/U]", "C_L", count, adv, 2);
    value("wake-div", "largest |div u| in any cell after the last step of the Re 100 run [U/D] (theory 0; solver tolerance 1e-4)", r100->maxdiv, 0.0);
    value("wake-mass", "outflow / inflow - 1 at the end of the Re 100 run (theory 0)", r100->mass, 0.0);
    value("wake-sweeps", "mean pressure sweeps per step, Re 100, 8 cells per D (no theory; warm start)", r100->sweeps, 0.0);
    value("wake-first-order-cl", "C_L amplitude at Re 100 with first-order advection over the MacCormack amplitude (no theory: smaller means more numerical damping)",
          outs[J_FIRST].s.cl_amp / r100->s.cl_amp, 1.0);
    value("wake-first-order-st", "Strouhal number at Re 100 with first-order advection (0 = no shedding found)", outs[J_FIRST].s.strouhal, roshko(100.0f));

    /* S23: forces on the cylinder against time at Re 100, and the solid-aware projection against the M2 split. */
    for (int k = 0; k < r100->traced; k++) { xs[k] = r100->t[k]; ya[k] = r100->cd[k]; yb[k] = r100->cl[k]; }
    Series forces[] = {{"C_D drag", ya, "#c0392b"}, {"C_L lift", yb, "#2471a3"}};
    plot("wake-forces.svg", "Forces on the cylinder at Re 100 (8 cells per D)", "t [D/U]", "coefficient", r100->traced, forces, 2);
    count = r100->traced < split->traced ? r100->traced : split->traced;
    int from = 0;
    while (from < count && r100->t[from] < 5.0f) from++;
    for (int k = from; k < count; k++) { xs[k - from] = r100->t[k]; ya[k - from] = r100->cd[k]; yb[k - from] = split->cd[k]; }
    Series sp[] = {{"projection that knows the solid (used)", ya, "#c0392b"}, {"penalty, then a plain projection (M2)", yb, "#888888"}};
    plot("wake-splitting.svg", "Drag at Re 100: a plain projection after the penalty reads higher", "t [D/U]", "C_D", count - from, sp, 2);
    value("wake-cd-100", "mean C_D over whole lift periods, Re 100, 8 cells per D, default channel 16 x 8 D (theory: 1.317 in open water, Qu et al. 2013)", r100->s.cd_mean, QU_CD[0]);
    value("wake-cd-amp-100", "C_D amplitude over C_L amplitude at Re 100 (drag swings at twice the lift frequency, much smaller)", r100->s.cd_amp / r100->s.cl_amp, 0.0);
    value("wake-cd-split", "mean C_D at Re 100 with the M2 scheme (penalty, then a plain projection) over the value with the solid-aware projection (theory 1)", split->s.cd_mean / r100->s.cd_mean, 1.0);
    value("wake-cd-split-dt2", "the M2 scheme: C_D at half the time step over C_D at the full step (theory 1; a splitting error shrinks with the step)", outs[J_SPLIT_DT].s.cd_mean / split->s.cd_mean, 1.0);
    value("wake-inside", "speed at the cylinder centre after the last step, solid-aware projection [U] (theory 0)", r100->inside, 0.0);
    value("wake-inside-split", "speed at the cylinder centre after the last step, M2 scheme [U] (theory 0: this is the leak)", split->inside, 0.0);
    value("wake-cd-box", "mean C_D from the momentum balance of a 3 D box over the mean C_D from the force on the faces, Re 100 (theory 1)", r100->cd_cv / r100->s.cd_mean, 1.0);
    value("wake-cd-box-split", "the same ratio for the M2 scheme (theory 1)", split->cd_cv / split->s.cd_mean, 1.0);

    /* S24: the two regimes. */
    for (int k = 0; k < NRE; k++) { xs[k] = res[k]; ya[k] = outs[k].s.cl_amp; yb[k] = outs[k].lr; }
    Series regimes[] = {{"C_L amplitude", ya, "#2471a3"}, {"eddy length L/D at t = 100 (a snapshot when shedding)", yb, "#7d3c98"}};
    plot("wake-regimes.svg", "Steady below Re 47, shedding above (8 cells per D, t = 100 D/U)", "Re = U D / nu", "C_L amplitude, L/D", NRE, regimes, 2);
    value("wake-clamp-30", "C_L amplitude at Re 30 (theory 0: steady)", outs[1].s.cl_amp, 0.0);
    value("wake-clamp-40", "C_L amplitude at Re 40 (theory 0: steady)", outs[2].s.cl_amp, 0.0);
    value("wake-clamp-60", "C_L amplitude at Re 60 (shedding; no single theory value)", outs[4].s.cl_amp, 0.0);
    value("wake-lr-20", "closed wake L/D at Re 20, default channel (theory: 0.94 in open water, Dennis & Chang 1970)", outs[0].lr, LIT_LR20);
    value("wake-lr-40", "closed wake L/D at Re 40, default channel (theory: 2.236 in open water, Gautier et al. 2013)", outs[2].lr, LIT_LR40);
    value("wake-cd-20", "mean C_D at Re 20, default channel (theory: 2.05 in open water, Dennis & Chang 1970)", outs[0].s.cd_mean, LIT_CD20);
    value("wake-cd-40", "mean C_D at Re 40, default channel (theory: 1.493 in open water, Gautier et al. 2013)", outs[2].s.cd_mean, LIT_CD40);
    value("wake-lr-20-wide", "closed wake L/D at Re 20, channel 32 x 24 D, centre 12 D from the inflow (theory 0.94)", outs[J_WIDE20].lr, LIT_LR20);
    value("wake-lr-40-wide", "closed wake L/D at Re 40, channel 32 x 24 D at t = 100 D/U (theory 2.236)", outs[J_WIDE40].lr, LIT_LR40);
    value("wake-cd-20-wide", "mean C_D at Re 20, channel 32 x 24 D (theory 2.05)", outs[J_WIDE20].s.cd_mean, LIT_CD20);
    value("wake-cd-40-wide", "mean C_D at Re 40, channel 32 x 24 D (theory 1.493)", outs[J_WIDE40].s.cd_mean, LIT_CD40);

    /* S25: Strouhal number against Re over the shedding runs (Re >= 60); drag at Re 100, 150, 200. */
    int m = 0;
    static float fit2[NRE], lo[NRE], hi[NRE];
    for (int k = 4; k < NRE; k++, m++) {
        xs[m] = res[k];
        ya[m] = outs[k].s.strouhal;
        yb[m] = roshko(res[k]);
        fit2[m] = williamson(res[k]);
        lo[m] = 0.16f;
        hi[m] = 0.22f;
    }
    Series st[] = {{"measured St, default channel", ya, "#2471a3"}, {"Roshko 1954 fit", yb, "#888888"}, {"Williamson 1989 fit", fit2, "#7d3c98"},
                   {"band 0.16", lo, "#27ae60"}, {"band 0.22", hi, "#27ae60"}};
    plot("wake-strouhal.svg", "Strouhal number of the shed street against Re (8 cells per D)", "Re", "St = f D / U", m, st, 5);
    const WakeOut *chan[3] = {r100, r150, r200}, *open[3] = {wide, &outs[J_WIDE150], &outs[J_WIDE200]};
    static const char *const tag[3] = {"100", "150", "200"};
    for (int k = 0; k < 3; k++) { xs[k] = res[6 + k]; ya[k] = chan[k]->s.cd_mean; yb[k] = open[k]->s.cd_mean; yc[k] = QU_CD[k]; }
    Series cd[] = {{"measured, default channel 16 x 8 D", ya, "#c0392b"}, {"measured, channel 32 x 24 D", yb, "#e67e22"},
                   {"open water, Qu et al. 2013", yc, "#888888"}};
    plot("wake-drag.svg", "Mean drag coefficient against Re (8 cells per D)", "Re", "C_D", 3, cd, 3);
    value("wake-st-100", "Strouhal number at Re 100, 8 cells per D, default channel (theory: Roshko 1954 fit 0.212(1 - 21.2/Re))", r100->s.strouhal, roshko(100.0f));
    value("wake-st-150", "Strouhal number at Re 150 (theory: the fit extrapolated to 150)", r150->s.strouhal, roshko(150.0f));
    value("wake-st-200", "Strouhal number at Re 200 (theory: the fit taken past its range of 150)", r200->s.strouhal, roshko(200.0f));
    value("wake-cd-150", "mean C_D at Re 150, default channel (theory: 1.305 in open water, Qu et al. 2013)", r150->s.cd_mean, QU_CD[1]);
    value("wake-cd-200", "mean C_D at Re 200, default channel (theory: 1.316 in open water, Qu et al. 2013)", r200->s.cd_mean, QU_CD[2]);
    for (int k = 0; k < 3; k++) {
        char id[64], what[200];
        snprintf(id, sizeof id, "wake-cd-%s-wide", tag[k]);
        snprintf(what, sizeof what, "mean C_D at Re %s, channel 32 x 24 D, centre 12 D from the inflow (theory: open water, Qu et al. 2013)", tag[k]);
        value(id, what, open[k]->s.cd_mean, QU_CD[k]);
        snprintf(id, sizeof id, "wake-st-%s-wide", tag[k]);
        snprintf(what, sizeof what, "Strouhal number at Re %s, channel 32 x 24 D (theory: open water, Qu et al. 2013)", tag[k]);
        value(id, what, open[k]->s.strouhal, QU_ST[k]);
        snprintf(id, sizeof id, "wake-clamp-%s-wide", tag[k]);
        snprintf(what, sizeof what, "C_L amplitude at Re %s, channel 32 x 24 D (no theory column: see page 5 for the literature range)", tag[k]);
        value(id, what, open[k]->s.cl_amp, 0.0);
    }
    value("wake-clamp-100", "C_L amplitude at Re 100, default channel, 8 cells per D", r100->s.cl_amp, 0.0);

    /* S26: grid, time step, wall position, domain at Re 100. */
    const int grid[] = {J_N6, 6, J_N12, J_N16};
    const float cells[] = {6, 8, 12, 16};
    for (int k = 0; k < 4; k++) { xs[k] = cells[k]; ya[k] = outs[grid[k]].s.strouhal * 10.0f; yb[k] = outs[grid[k]].s.cd_mean; }
    Series conv[] = {{"10 x St", ya, "#2471a3"}, {"mean C_D", yb, "#c0392b"}};
    plot("wake-convergence.svg", "Re 100 on finer grids (default channel, step 0.4 cell per U)", "cells per diameter", "10 x St, C_D", 4, conv, 2);
    value("wake-st-n6", "St at Re 100, 6 cells per D (the grid of make test)", outs[J_N6].s.strouhal, roshko(100.0f));
    value("wake-st-n12", "St at Re 100, 12 cells per D", outs[J_N12].s.strouhal, roshko(100.0f));
    value("wake-st-n16", "St at Re 100, 16 cells per D", outs[J_N16].s.strouhal, roshko(100.0f));
    value("wake-cd-n6", "C_D at Re 100, 6 cells per D, default channel (theory: open water)", outs[J_N6].s.cd_mean, QU_CD[0]);
    value("wake-cd-n12", "C_D at Re 100, 12 cells per D", outs[J_N12].s.cd_mean, QU_CD[0]);
    value("wake-cd-n16", "C_D at Re 100, 16 cells per D", outs[J_N16].s.cd_mean, QU_CD[0]);
    value("wake-clamp-n16", "C_L amplitude at Re 100, 16 cells per D, default channel", outs[J_N16].s.cl_amp, 0.0);
    value("wake-st-dt2", "St at Re 100, 8 cells per D, half the time step, over St at the full step (theory 1)", outs[J_DT].s.strouhal / r100->s.strouhal, 1.0);
    value("wake-cd-dt2", "C_D at Re 100, half the time step, over C_D at the full step (theory 1)", outs[J_DT].s.cd_mean / r100->s.cd_mean, 1.0);
    value("wake-cd-shift", "C_D at Re 100 with the wall moved half a cell inwards over C_D with the wall at the radius (no theory: sensitivity)", outs[J_SHIFT].s.cd_mean / r100->s.cd_mean, 1.0);
    value("wake-st-shift", "St at Re 100 with the wall moved half a cell inwards over St with the wall at the radius", outs[J_SHIFT].s.strouhal / r100->s.strouhal, 1.0);
    const float blockage[] = {1.0f / 24.0f, 1.0f / 16.0f, 1.0f / 8.0f};
    const WakeOut *high[] = {&outs[J_H24], &outs[J_H16], r100};
    for (int k = 0; k < 3; k++) { xs[k] = blockage[k]; ya[k] = high[k]->s.strouhal * 10.0f; yb[k] = high[k]->s.cd_mean; }
    Series blk[] = {{"10 x St", ya, "#2471a3"}, {"mean C_D", yb, "#c0392b"}};
    plot("wake-blockage.svg", "Re 100 in higher channels, centre 4 D from the inflow (8 cells per D)", "blockage D / H", "10 x St, C_D", 3, blk, 2);
    value("wake-st-h16", "St at Re 100 in a channel 16 D high (blockage 1/16), centre 4 D from the inflow", outs[J_H16].s.strouhal, roshko(100.0f));
    value("wake-st-h24", "St at Re 100 in a channel 24 D high (blockage 1/24), centre 4 D from the inflow", outs[J_H24].s.strouhal, roshko(100.0f));
    value("wake-cd-h16", "C_D at Re 100, blockage 1/16, centre 4 D from the inflow", outs[J_H16].s.cd_mean, QU_CD[0]);
    value("wake-cd-h24", "C_D at Re 100, blockage 1/24, centre 4 D from the inflow", outs[J_H24].s.cd_mean, QU_CD[0]);
    /* The domain: 16 x 8 (centre at 4 D), 24 x 8 (8 D), 24 x 16 (8 D), 32 x 24 (12 D). */
    const WakeOut *dom[] = {r100, &outs[J_UP], &outs[J_MID], wide};
    static float qcd[4], qst[4];
    for (int k = 0; k < 4; k++) { xs[k] = (float)(k + 1); ya[k] = dom[k]->s.strouhal * 10.0f; yb[k] = dom[k]->s.cd_mean; qcd[k] = QU_CD[0]; qst[k] = QU_ST[0] * 10.0f; }
    Series ds[] = {{"mean C_D", yb, "#c0392b"}, {"C_D open water, Qu et al. 2013", qcd, "#888888"}, {"10 x St", ya, "#2471a3"},
                   {"10 x St open water, Qu et al. 2013", qst, "#27ae60"}};
    plot("wake-domain.svg", "Re 100 in larger domains: 1 = 16x8 D (inflow 4 D), 2 = 24x8 (8 D), 3 = 24x16 (8 D), 4 = 32x24 (12 D)", "domain", "10 x St, C_D", 4, ds, 4);
    value("wake-cd-up8", "C_D at Re 100, channel 24 x 8 D, centre 8 D from the inflow", outs[J_UP].s.cd_mean, QU_CD[0]);
    value("wake-st-up8", "St at Re 100, channel 24 x 8 D, centre 8 D from the inflow", outs[J_UP].s.strouhal, QU_ST[0]);
    value("wake-cd-mid", "C_D at Re 100, channel 24 x 16 D, centre 8 D from the inflow", outs[J_MID].s.cd_mean, QU_CD[0]);
    value("wake-st-mid", "St at Re 100, channel 24 x 16 D, centre 8 D from the inflow", outs[J_MID].s.strouhal, QU_ST[0]);
    value("wake-cd-domain", "C_D at Re 100 in the default channel over C_D in the 32 x 24 D channel (theory 1: what the small domain adds)", r100->s.cd_mean / wide->s.cd_mean, 1.0);
    value("wake-st-domain", "St at Re 100 in the default channel over St in the 32 x 24 D channel (theory 1)", r100->s.strouhal / wide->s.strouhal, 1.0);
}

int main(int argc, char **argv)
{
    snprintf(dir, sizeof dir, "%s", argc > 1 ? argv[1] : "docs/theory/figures");
    values = fopen(path("values.md"), "w");
    if (!values) { fprintf(stderr, "cannot write %s/values.md\n", dir); return 1; }
    fprintf(values, "# Measured values\n\nWritten by `make theory-figs` (tests/theory_figs.c) from the program's own functions. Do not edit by hand.\n"
                    "Units: the pond is the unit square (lengths in domain widths), times in seconds, density 1. The last column is the relative error; where the theory column is 0 it is the absolute value.\n\n"
                    "| id | what is measured | measured | theory | error |\n|---|---|---|---|---|\n");
    fig_walls();
    fig_diffusion();
    fig_advection();
    fig_projection();
    fig_force();
    fig_vorticity();
    fig_swimmer();
    fig_reynolds();
    fig_sample();
    val_view();
    fig_jet();
    fig_stamina();
    fig_rules();
    val_story();
    fig_wake();
    fclose(values);
    return 0;
}

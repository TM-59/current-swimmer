/* M2: flow past a cylinder. These runs use the small grid (6 cells per diameter, 60 D/U) so that CI stays
 * short; the finer grids, the longer runs and the convergence study are measured by `make theory-figs`
 * (docs/theory/05-wake.md). */
#include "../src/wake.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846
#define CELLS 6
#define T_END 60.0f

static int failures;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* The zero-crossing counter on a known signal: C_L = 0.3 sin(2 pi f t) + 0.01 with f = 0.2. */
static void test_stats_on_a_sine(void)
{
    enum { N = 4000 };
    static float cl[N], cd[N];
    float dt = 0.05f;
    for (int k = 0; k < N; k++) {
        cl[k] = 0.3f * (float)sin(2.0 * PI * 0.2 * k * dt) + 0.01f;
        cd[k] = 1.5f + 0.02f * (float)sin(4.0 * PI * 0.2 * k * dt);
    }
    WakeStats s = wake_stats(cl, cd, N, N / 2, dt);
    check(fabsf(s.frequency - 0.2f) < 1e-4f && fabsf(s.strouhal - 0.2f) < 1e-4f, "wake_stats: frequency of a sine to 1e-4");
    check(fabsf(s.cl_amp - 0.3f) < 1e-3f && fabsf(s.cd_amp - 0.02f) < 1e-3f, "wake_stats: amplitudes are half the peak-to-peak");
    check(fabsf(s.cd_mean - 1.5f) < 1e-3f && s.periods >= 15, "wake_stats: drag averaged over whole periods");
    check(fabsf(strouhal_number(0.5f, 2.0f, 4.0f) - 0.25f) < 1e-7f, "strouhal_number: St = f D / U");
}

/* Mass: after a step the projection leaves every cell divergence-free, and what comes in at the left
 * leaves at the right. */
static void test_projection_and_mass(void)
{
    Wake w;
    if (wake_init(&w, CELLS, 100.0f)) { check(0, "wake_init"); return; }
    float dt = wake_dt(&w);
    for (int k = 0; k < 200; k++) wake_step(&w, dt);
    check(wake_max_divergence(&w) < 1e-4f, "projection: |div u| < 1e-4 U/D in every cell after each step");
    double in = 0.0, out = 0.0;
    for (int j = 0; j < w.ny; j++) {
        float u, v;
        wake_sample(&w, 0.0f, ((float)j + 0.5f) * w.h, &u, &v);
        in += u * w.h;
        wake_sample(&w, (float)WAKE_LX, ((float)j + 0.5f) * w.h, &u, &v);
        out += u * w.h;
    }
    printf("     inflow %.6f, outflow %.6f [U D]\n", in, out);
    check(fabs(out / in - 1.0) < 1e-3, "mass: outflow equals inflow to 0.1 %");
    float u, v;
    wake_sample(&w, w.cx, w.cy, &u, &v);
    check(fabsf(u) < 1e-6f && fabsf(v) < 1e-6f, "penalisation: the water inside the cylinder is at rest (|u| < 1e-6 U)");
    wake_free(&w);
}

/* The projection knows the solid: no pressure correction through a solid face. The M2 scheme (a plain
 * projection after the penalty) pushed water back into the cylinder every step and read the drag higher. */
static void test_solid_aware_projection(void)
{
    double mean[2] = {0.0, 0.0};
    float inside[2] = {0.0f, 0.0f};
    for (int split = 0; split < 2; split++) {
        Wake w;
        if (wake_init(&w, CELLS, 100.0f)) { check(0, "wake_init"); return; }
        w.split_penalty = split;
        float dt = wake_dt(&w);
        int steps = (int)(20.0f / dt + 0.5f), n = 0;
        for (int k = 0; k < steps; k++) {
            wake_step(&w, dt);
            if (k >= steps / 2) { mean[split] += wake_drag_coefficient(&w); n++; }
        }
        mean[split] /= n;
        float u, v;
        wake_sample(&w, w.cx, w.cy, &u, &v);
        inside[split] = sqrtf(u * u + v * v);
        wake_free(&w);
    }
    printf("     t 10..20 D/U at Re 100: C_D %.4f with the solid-aware projection, %.4f with the M2 split; speed at the centre %.2g and %.3f U\n",
           mean[0], mean[1], (double)inside[0], (double)inside[1]);
    check(inside[1] > 0.01f && inside[0] < 1e-6f, "the M2 split leaks through the cylinder (> 0.01 U at its centre); the solid-aware projection does not");
    check(mean[1] > 1.03 * mean[0], "the M2 split reads the drag more than 3 % higher");
}

/* Below Re about 47 the wake stays steady: two attached eddies, no lift oscillation, drag above the
 * shedding values. Open water (2D): C_D 2.05 and L/D 0.94 at Re 20 (Dennis & Chang 1970), C_D 1.493 and
 * L/D 2.236 at Re 40 (Gautier et al. 2013). The small channel adds drag (docs/theory/05-wake.md, S26). */
static void test_steady_twin_vortex(void)
{
    WakeStats s;
    float lr;
    if (wake_run(CELLS, 30.0f, 50.0f, 0.0f, &s, &lr, NULL, NULL, 0, NULL)) { check(0, "wake_run"); return; }
    printf("     Re 30: C_L amplitude %.5f, C_D %.4f, closed wake %.3f D\n", s.cl_amp, s.cd_mean, lr);
    check(s.cl_amp < 0.01f, "Re 30: no shedding (C_L amplitude < 0.01)");
    check(lr > 0.8f && lr < 2.2f, "Re 30: a closed recirculation behind the cylinder, 0.8 D < L < 2.2 D");
    check(s.cd_mean > 1.9f && s.cd_mean < 2.25f, "Re 30: C_D within 1.9..2.25 (measured 2.06 in this channel; open water 2.05 at Re 20, 1.49 at Re 40)");
}

/* Shedding: the Strouhal number of the lift oscillation lies in the literature band 0.16..0.22 for
 * Re 100..200 and grows with Re; the mean drag stays within 1.42..1.66: measured 1.50..1.59 on this grid,
 * against 1.31..1.32 in open water (Qu et al. 2013) raised by about 15 % by this channel (16 x 8 D, inflow
 * 4 D ahead; docs/theory/05-wake.md, S26). At Re 100 the drag is also taken from the momentum balance of a
 * box around the cylinder, which must agree with the force on the faces. */
static void test_shedding(void)
{
    const float res[] = {100.0f, 150.0f, 200.0f};
    float last = 0.0f;
    for (int k = 0; k < 3; k++) {
        Wake w;
        if (wake_init(&w, CELLS, res[k])) { check(0, "wake_init"); return; }
        float dt = wake_dt(&w);
        int steps = (int)(T_END / dt + 0.5f);
        float *cl = malloc((size_t)steps * sizeof(float)), *cd = malloc((size_t)steps * sizeof(float));
        float *box = malloc((size_t)steps * sizeof(float));
        if (!cl || !cd || !box) { check(0, "malloc"); return; }
        for (int i = 0; i < steps; i++) {
            wake_step(&w, dt);
            cl[i] = wake_lift_coefficient(&w);
            cd[i] = wake_drag_coefficient(&w);
            box[i] = wake_control_volume_drag(&w, 1.5f);
        }
        WakeStats s = wake_stats(cl, cd, steps, steps / 2, dt);
        float cd_box = wake_stats(cl, box, steps, steps / 2, dt).cd_mean;
        free(cl); free(cd); free(box);
        wake_free(&w);
        char what[160];
        printf("     Re %.0f: St %.4f, C_D %.4f (box balance %.4f), C_L amplitude %.4f, %d periods\n", res[k], s.strouhal, s.cd_mean, cd_box, s.cl_amp, s.periods);
        snprintf(what, sizeof what, "Re %.0f: vortex shedding (C_L amplitude > 0.1, at least 4 periods measured)", res[k]);
        check(s.cl_amp > 0.1f && s.periods >= 4, what);
        snprintf(what, sizeof what, "Re %.0f: Strouhal number in 0.16..0.22", res[k]);
        check(s.strouhal >= 0.16f && s.strouhal <= 0.22f, what);
        snprintf(what, sizeof what, "Re %.0f: mean C_D in 1.42..1.66", res[k]);
        check(s.cd_mean >= 1.42f && s.cd_mean <= 1.66f, what);
        snprintf(what, sizeof what, "Re %.0f: C_D from the momentum balance of a 3 D box within 4 %% of the force on the faces", res[k]);
        check(fabsf(cd_box / s.cd_mean - 1.0f) < 0.04f, what);
        if (k > 0) {
            snprintf(what, sizeof what, "Re %.0f: St higher than at the lower Re", res[k]);
            check(s.strouhal > last, what);
        }
        last = s.strouhal;
    }
}

int main(void)
{
    test_stats_on_a_sine();
    test_projection_and_mass();
    test_solid_aware_projection();
    test_steady_twin_vortex();
    test_shedding();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    puts("wake: all checks passed");
    return 0;
}

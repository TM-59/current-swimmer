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
    check(fabsf(u) < 0.05f && fabsf(v) < 0.05f, "penalisation: the water inside the cylinder is at rest (|u| < 0.05 U)");
    wake_free(&w);
}

/* Below Re about 47 the wake stays steady: two attached eddies, no lift oscillation, drag above the
 * shedding values. Literature for an unbounded 2D cylinder (Dennis & Chang 1970): L/D 0.94 and C_D 2.05 at Re 20, 2.35 and 1.52 at Re 40. */
static void test_steady_twin_vortex(void)
{
    WakeStats s;
    float lr;
    if (wake_run(CELLS, 30.0f, 50.0f, 0.0f, &s, &lr, NULL, NULL, 0, NULL)) { check(0, "wake_run"); return; }
    printf("     Re 30: C_L amplitude %.5f, C_D %.4f, closed wake %.3f D\n", s.cl_amp, s.cd_mean, lr);
    check(s.cl_amp < 0.01f, "Re 30: no shedding (C_L amplitude < 0.01)");
    check(lr > 0.8f && lr < 2.2f, "Re 30: a closed recirculation behind the cylinder, 0.8 D < L < 2.2 D");
    check(s.cd_mean > 1.6f && s.cd_mean < 2.6f, "Re 30: C_D within 1.6..2.6 (unbounded literature 2.05 at Re 20, 1.52 at Re 40; see page 5 for the excess)");
}

/* Shedding: the Strouhal number of the lift oscillation lies in the literature band 0.16..0.22 for
 * Re 100..200 and grows with Re; the mean drag stays within 1.3..1.9 (unbounded 2D value about 1.33, raised
 * by the 1/8 blockage of the channel and by the diffuse penalised wall, see docs/theory/05-wake.md). */
static void test_shedding(void)
{
    const float res[] = {100.0f, 150.0f, 200.0f};
    float last = 0.0f;
    for (int k = 0; k < 3; k++) {
        WakeStats s;
        if (wake_run(CELLS, res[k], T_END, 0.0f, &s, NULL, NULL, NULL, 0, NULL)) { check(0, "wake_run"); return; }
        char what[160];
        printf("     Re %.0f: St %.4f, C_D %.4f, C_L amplitude %.4f, %d periods\n", res[k], s.strouhal, s.cd_mean, s.cl_amp, s.periods);
        snprintf(what, sizeof what, "Re %.0f: vortex shedding (C_L amplitude > 0.1, at least 4 periods measured)", res[k]);
        check(s.cl_amp > 0.1f && s.periods >= 4, what);
        snprintf(what, sizeof what, "Re %.0f: Strouhal number in 0.16..0.22", res[k]);
        check(s.strouhal >= 0.16f && s.strouhal <= 0.22f, what);
        snprintf(what, sizeof what, "Re %.0f: mean C_D in 1.3..1.9", res[k]);
        check(s.cd_mean >= 1.3f && s.cd_mean <= 1.9f, what);
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
    test_steady_twin_vortex();
    test_shedding();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    puts("wake: all checks passed");
    return 0;
}

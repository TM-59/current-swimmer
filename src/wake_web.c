/* Browser readout of the cylinder wake (M2). The page calls these through WebAssembly; every number on
 * the page comes from src/wake.c. One wake at a time; a new Reynolds number starts it again. */
#include "wake.h"

#include <stdlib.h>
#include <string.h>

enum { WV_RECORD = 6000, WV_TRACE = 400, WV_EVERY = 3 };
enum { WV_T, WV_RE, WV_CD, WV_CL, WV_ST, WV_CD_MEAN, WV_CL_AMP, WV_PERIODS, WV_NX, WV_NY, WV_N,
       WV_LR, WV_DIV, WV_MEASURED_FROM, WV_SWEEPS, WV_COUNT };

static Wake wake;
static int ready;
static float re_now, dt;
static float cl[WV_RECORD], cd[WV_RECORD];
static int recorded, since;
static float state[WV_COUNT];
static float *field;
static float trace[2 * WV_TRACE];
static int traced, step_count;

int wv_init(float reynolds, int cells_per_diameter)
{
    if (ready) wake_free(&wake);
    ready = 0;
    if (wake_init(&wake, cells_per_diameter, reynolds)) return -1;
    free(field);
    field = calloc((size_t)wake.nx * (size_t)wake.ny, sizeof(float));
    if (!field) return -1;
    ready = 1;
    re_now = reynolds;
    dt = wake_dt(&wake);
    recorded = since = traced = step_count = 0;
    memset(state, 0, sizeof state);
    return 0;
}

/* Measures over the newest half of the record once 20 D/U have passed (the start-up is not periodic). */
static void measure(void)
{
    int from = recorded / 2;
    WakeStats s = wake_stats(cl, cd, recorded, from, dt);
    int shedding = s.periods >= 2 && s.cl_amp > 0.02f;
    state[WV_ST] = shedding ? s.strouhal : 0.0f;
    state[WV_CD_MEAN] = s.cd_mean;
    state[WV_CL_AMP] = s.cl_amp;
    state[WV_PERIODS] = shedding ? (float)s.periods : 0.0f;
    state[WV_MEASURED_FROM] = (float)(since + from) * dt;
}

void wv_step(int steps)
{
    if (!ready) return;
    for (int k = 0; k < steps; k++) {
        wake_step(&wake, dt);
        if (recorded == WV_RECORD) {  /* keep the newest half */
            memmove(cl, cl + WV_RECORD / 2, (WV_RECORD / 2) * sizeof(float));
            memmove(cd, cd + WV_RECORD / 2, (WV_RECORD / 2) * sizeof(float));
            recorded = WV_RECORD / 2;
            since += WV_RECORD / 2;
        }
        cl[recorded] = wake_lift_coefficient(&wake);
        cd[recorded] = wake_drag_coefficient(&wake);
        recorded++;
        if (step_count++ % WV_EVERY == 0) {
            if (traced == WV_TRACE) {
                memmove(trace, trace + 1, (WV_TRACE - 1) * sizeof(float));
                memmove(trace + WV_TRACE, trace + WV_TRACE + 1, (WV_TRACE - 1) * sizeof(float));
                traced--;
            }
            trace[traced] = cl[recorded - 1];
            trace[WV_TRACE + traced] = cd[recorded - 1];
            traced++;
        }
    }
    if (wake.t >= 20.0) measure();
}

/* t, Re, C_D, C_L, St, mean C_D, C_L amplitude, periods, nx, ny, cells per D, closed wake L/D,
 * max |div u|, start of the measured window, pressure sweeps of the last step. */
const float *wv_state(void)
{
    if (!ready) return state;
    state[WV_T] = (float)wake.t;
    state[WV_RE] = re_now;
    state[WV_CD] = wake_drag_coefficient(&wake);
    state[WV_CL] = wake_lift_coefficient(&wake);
    state[WV_NX] = (float)wake.nx;
    state[WV_NY] = (float)wake.ny;
    state[WV_N] = (float)wake.n;
    state[WV_LR] = wake_recirculation_length(&wake);
    state[WV_DIV] = wake_max_divergence(&wake);
    state[WV_SWEEPS] = (float)wake.sweeps;
    return state;
}

/* Vorticity of every cell, row-major from the bottom left [U / D]. */
const float *wv_field(void)
{
    if (!ready) return NULL;
    for (int j = 0; j < wake.ny; j++)
        for (int i = 0; i < wake.nx; i++) field[i + wake.nx * j] = wake_vorticity_at(&wake, i, j);
    return field;
}

/* The newest lift and drag samples, one every WV_EVERY steps: C_L[0..n), then C_D at offset WV_TRACE. */
const float *wv_trace(void) { return trace; }
int wv_trace_count(void) { return traced; }
float wv_trace_dt(void) { return dt * (float)WV_EVERY; }

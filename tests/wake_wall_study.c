#include "../src/wake.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const WakeSetup setup = {8, 100.0f, WAKE_LX, WAKE_LY, WAKE_CX, 0.0f, 1};
    Wake w;
    if (wake_init_setup(&w, &setup)) {
        fputs("wake_init_setup failed\n", stderr);
        return 1;
    }
    float dt = wake_dt(&w);
    int steps = (int)(100.0f / dt + 0.5f);
    float *cl = malloc((size_t)steps * sizeof *cl);
    float *cd = malloc((size_t)steps * sizeof *cd);
    if (!cl || !cd) {
        fputs("allocation failed\n", stderr);
        free(cl);
        free(cd);
        wake_free(&w);
        return 1;
    }
    for (int i = 0; i < steps; i++) {
        wake_step(&w, dt);
        cl[i] = wake_lift_coefficient(&w);
        cd[i] = wake_drag_coefficient(&w);
    }
    WakeStats s = wake_stats(cl, cd, steps, steps / 2, dt);
    printf("sharp wall: St %.6f C_D %.6f C_L amplitude %.6f periods %d\n",
           s.strouhal, s.cd_mean, s.cl_amp, s.periods);
    free(cl);
    free(cd);
    wake_free(&w);
    if (s.periods < 3) {
        fputs("insufficient measured periods\n", stderr);
        return 1;
    }
    return 0;
}

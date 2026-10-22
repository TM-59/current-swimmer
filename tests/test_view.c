#include "../src/view.h"
#include "../src/fluid.h"
#include "../src/swimmer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    Fluid water;
    Swimmer fish;
    assert(fluid_init(&water, 48, 0.0004f) == 0);
    swimmer_init(&fish, 0.3f, 0.5f, 0.0f);
    assert(view_init() == 0);
    for (int step = 0; step < 900; step++) {
        float t = step * 0.02f;
        Controls c = {t < 7.0f ? 0.5f : -0.5f, fmodf(t, 2.4f) < 0.5f};
        swimmer_step(&fish, &water, &c, 0.02f);
        fluid_step(&water, 0.02f);
        view_step(c.turn, c.stroke);
        const float *s = view_state(), *f = view_field();
        assert(s[VIEW_X] == fish.x && s[VIEW_Y] == fish.y);
        assert(s[VIEW_HEADING] == fish.heading && s[VIEW_SPEED] == swimmer_speed(&fish));
        assert(s[VIEW_THRUST] == fish.thrust && s[VIEW_DRAG] == fish.drag);
        assert(s[VIEW_ENERGY] == fluid_kinetic_energy(&water));
        assert(s[VIEW_DIV] == fluid_max_divergence(&water));
        for (int j = 1; j <= 48; j++)
            for (int i = 1; i <= 48; i++) {
                int dst = ((j - 1) * 48 + i - 1) * 3, src = j * 50 + i;
                assert(f[dst] == fluid_vorticity_at(&water, i, j));
                assert(f[dst + 1] == water.u[src] && f[dst + 2] == water.v[src]);
            }
    }
    assert(view_state()[VIEW_TIME] == 18.0f);
    assert(view_init() == 0 && view_state()[VIEW_TIME] == 0.0f);
    assert(view_state()[VIEW_ENERGY] == 0.0f);
    for (int i = 0; i < 48 * 48 * 3; i++) assert(view_field()[i] == 0.0f);
    fluid_free(&water);
    puts("view: 900 steps and all 2,304 cells match the original C model exactly; reset ok");
    return 0;
}

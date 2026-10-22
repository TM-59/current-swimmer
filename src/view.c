#include "view.h"
#include "fluid.h"
#include "swimmer.h"

#include <math.h>

static Fluid water;
static Swimmer fish;
static unsigned steps;
static float state[VIEW_STATE_COUNT];
static float field[VIEW_GRID * VIEW_GRID * 3];

static void refresh(void)
{
    state[VIEW_X] = fish.x;
    state[VIEW_Y] = fish.y;
    state[VIEW_HEADING] = fish.heading;
    state[VIEW_SPEED] = swimmer_speed(&fish);
    state[VIEW_RE] = reynolds_number(state[VIEW_SPEED], 2.0f * fish.radius, water.visc);
    state[VIEW_THRUST] = fish.thrust;
    state[VIEW_DRAG] = fish.drag;
    state[VIEW_ENERGY] = fluid_kinetic_energy(&water);
    state[VIEW_DIV] = fluid_max_divergence(&water);
    state[VIEW_TIME] = steps * 0.02f;
    state[VIEW_RADIUS] = fish.radius;
    state[VIEW_VORT] = fluid_max_abs_vorticity(&water);
    for (int j = 1; j <= VIEW_GRID; j++)
        for (int i = 1; i <= VIEW_GRID; i++) {
            int dst = ((j - 1) * VIEW_GRID + i - 1) * 3;
            int src = j * (VIEW_GRID + 2) + i;
            field[dst] = fluid_vorticity_at(&water, i, j);
            field[dst + 1] = water.u[src];
            field[dst + 2] = water.v[src];
        }
}

int view_init(void)
{
    fluid_free(&water);
    if (fluid_init(&water, VIEW_GRID, 0.0004f)) return 1;
    swimmer_init(&fish, 0.3f, 0.5f, 0.0f);
    steps = 0;
    refresh();
    return 0;
}

void view_step(float turn, int stroke)
{
    if (!water.u) return;
    if (!isfinite(turn)) turn = 0.0f;
    Controls c = {fmaxf(-1.0f, fminf(1.0f, turn)), stroke != 0};
    swimmer_step(&fish, &water, &c, 0.02f);
    fluid_step(&water, 0.02f);
    steps++;
    refresh();
}

const float *view_state(void) { return state; }
const float *view_field(void) { return field; }

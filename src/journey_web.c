#include "journey.h"
#include "view.h"

#include <math.h>

/* Browser readout of the "Upstream" journey; the same C code the story bench plays. */
enum { JW_STATE_COUNT = 33 };
static Journey journey;
static int ready;
static float state[JW_STATE_COUNT];
static float field[VIEW_GRID * VIEW_GRID * 3];

static void refresh(void)
{
    const Journey *j = &journey;
    float speed = swimmer_speed(&j->fish);
    float values[JW_STATE_COUNT] = {
        j->fish.x, j->fish.y, j->fish.heading, speed,
        reynolds_number(speed, 2.0f * j->fish.radius, j->water.visc), j->fish.thrust, j->fish.radius,
        fluid_max_abs_vorticity(&j->water), j->stamina, j->stamina_max, (float)j->reach, (float)j->scene,
        (float)j->banner, j->time, j->strain, (float)j->sweeps, j->mark_x[j->mark], j->mark_y[j->mark],
        (float)j->route, (float)j->stay, (float)j->silt, (float)j->winded, (float)journey_ending(j),
        j->flow, (float)j->loud, (float)j->marks, (float)j->mark, (float)journey_heavy(j->scene),
        fluid_kinetic_energy(&j->water), fluid_max_divergence(&j->water), j->held, journey_hold_need(j), journey_hold_radius()};
    for (int k = 0; k < JW_STATE_COUNT; k++) state[k] = values[k];
    for (int y = 1; y <= VIEW_GRID; y++)
        for (int x = 1; x <= VIEW_GRID; x++) {
            int dst = ((y - 1) * VIEW_GRID + x - 1) * 3, src = y * (VIEW_GRID + 2) + x;
            field[dst] = fluid_vorticity_at(&j->water, x, y);
            field[dst + 1] = j->water.u[src];
            field[dst + 2] = j->water.v[src];
        }
}

int jw_init(void)
{
    if (ready) journey_free(&journey);
    ready = journey_init(&journey, JE_ALL) == 0;
    if (ready) refresh();
    return !ready;
}

void jw_step(float turn, int stroke)
{
    if (!ready) return;
    journey_step(&journey, isfinite(turn) ? turn : 0.0f, stroke != 0);
    refresh();
}

int jw_answer(int option)
{
    if (!ready) return 1;
    int rc = journey_answer(&journey, option);
    refresh();
    return rc;
}

const float *jw_state(void) { return state; }
const float *jw_field(void) { return field; }

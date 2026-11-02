#include "journey.h"

#include <math.h>
#include <string.h>

#define J_DT 0.02f
#define J_GRID 48
#define J_VISC 0.0004f
#define START_X 0.12f
#define START_Y 0.5f
#define MARK_RADIUS 0.07f
#define GATE_X 0.82f
#define SLUICE_X 0.96f
#define SLUICE_Y 0.5f
#define SLUICE_RADIUS 0.07f
#define SPIN_UP_STEPS 500
#define SWEEP_LOSS 0.20f
#define EASE_SECONDS 10.0f
#define CELLS ((J_GRID + 2) * (J_GRID + 2))

/* Sluice jet strength for each reach before story effects (the base difficulty). */
static const float BASE_FLOW[J_REACHES] = {0.45f, 0.60f, 0.30f, 0.55f, 0.62f};

#define DRAIN 0.50f          /* stamina per second while the tail beats */
#define REGEN 0.25f          /* stamina per second while coasting */
#define PACE_DRAIN 0.60f     /* JE_PACE: drain multiplier while Silt sets the pace */
#define PIPE_FLOW 1.30f      /* JE_ROUTE: the pipe narrows the water */
#define EDDY_RESERVE 1.25f   /* JE_EDDY: stamina reserve for the reach after the eddy */
#define WARM_REGEN 0.32f     /* JE_WARM: warm water holds less oxygen */
#define STAY_SECONDS 45.0f   /* JE_STAY: time spent beside Silt */
#define STAY_REGEN 1.40f     /* JE_STAY: "push, then let go" */
#define GO_SURGE 1.15f       /* JE_STAY: leaving at once beats the second board to the halfway mark */
#define DITCH_DRAIN 0.70f    /* JE_ROUTE: the flooded ditch is still water */
#define DITCH_WAIT 45.0f     /* JE_ROUTE: the ditch ends at a weir; wait for the water to rise over the lip */
#define SURGE_FLOW 1.30f     /* JE_SURGE: the second sluice board */
#define EASE_FLOW 0.85f      /* the pond keeps emptying: the jet weakens after each sweep */

static const int HEAVY[JS_COUNT] = {0, 0, 0, 0, 0, 1, 1, 0};

int journey_options(int scene) { return scene == JS_CULVERT || scene == JS_REEDS ? 2 : 1; }
int journey_heavy(int scene) { return scene >= 0 && scene < JS_COUNT && HEAVY[scene]; }

int journey_ending(const Journey *j)
{
    return j->scene == JS_SPRING || j->reach >= J_REACHES ? j->stay : -1;
}

static int on(const Journey *j, unsigned effect) { return (j->effects & effect) != 0; }

static void open_scene(Journey *j, int scene)
{
    j->scene = scene;
    if (j->order_len < JS_COUNT) j->order[j->order_len++] = scene;
    if (journey_heavy(scene)) { j->banner = JB_NONE; j->banner_left = 0.0f; }
}

static void raise_banner(Journey *j, int banner)
{
    j->banner = banner;
    j->banner_left = 1.5f;
    if (banner == JB_CLEAR) j->loud++;
}

static void set_marks(Journey *j)
{
    static const float ditch_x[] = {0.28f, 0.55f, 0.78f}, ditch_y[] = {0.82f, 0.86f, 0.80f};
    j->mark = 0;
    if (j->reach == 1 && j->route == 1) {
        j->marks = 3;
        memcpy(j->mark_x, ditch_x, sizeof ditch_x);
        memcpy(j->mark_y, ditch_y, sizeof ditch_y);
    } else if (j->reach == 1) {
        j->marks = 2;
        j->mark_x[0] = 0.5f; j->mark_y[0] = 0.5f;
        j->mark_x[1] = GATE_X; j->mark_y[1] = 0.5f;
    } else {
        j->marks = 1;
        j->mark_x[0] = GATE_X; j->mark_y[0] = 0.5f;
    }
}

static float drain_rate(const Journey *j)
{
    float d = DRAIN;
    if (j->silt && on(j, JE_PACE)) d *= PACE_DRAIN;
    if (j->reach == 1 && j->route == 1 && on(j, JE_ROUTE)) d *= DITCH_DRAIN;
    return d;
}

static float regen_rate(const Journey *j)
{
    float r = REGEN;
    if (j->reach == 3 && on(j, JE_WARM)) r *= WARM_REGEN;
    if (j->reach == 4 && j->stay == 0 && on(j, JE_STAY)) r *= STAY_REGEN;
    return r;
}

static void sluice(Journey *j)
{
    fluid_add_force(&j->water, SLUICE_X, SLUICE_Y, -j->flow, 0.0f, SLUICE_RADIUS, J_DT);
}

/* The sluice has been running for SPIN_UP_STEPS steps (10 s) before the swimmer gets there. The developed
 * jet depends only on its strength, so it is computed once per strength and copied. */
static void spin_up(Journey *j)
{
    static struct { float flow; float u[CELLS], v[CELLS]; } cache[8];
    static int cached;
    for (int i = 0; i < cached; i++)
        if (cache[i].flow == j->flow) {
            memcpy(j->water.u, cache[i].u, sizeof cache[i].u);
            memcpy(j->water.v, cache[i].v, sizeof cache[i].v);
            return;
        }
    for (int k = 0; k < SPIN_UP_STEPS; k++) { sluice(j); fluid_step(&j->water, J_DT); }
    int slot = cached < 8 ? cached++ : 7;
    cache[slot].flow = j->flow;
    memcpy(cache[slot].u, j->water.u, sizeof cache[slot].u);
    memcpy(cache[slot].v, j->water.v, sizeof cache[slot].v);
}

static void start_reach(Journey *j)
{
    fluid_free(&j->water);
    if (fluid_init(&j->water, J_GRID, J_VISC)) { j->reach = J_REACHES; return; }
    swimmer_init(&j->fish, START_X, START_Y, 0.0f);
    j->flow = BASE_FLOW[j->reach];
    if (j->reach == 1 && j->route == 0 && on(j, JE_ROUTE)) j->flow *= PIPE_FLOW;
    if (j->reach != 2) j->stamina_max = 1.0f;
    if (j->stamina > j->stamina_max) j->stamina = j->stamina_max;
    j->reach_time = 0.0f;
    j->best_x = START_X;
    set_marks(j);
    spin_up(j);
}

int journey_init(Journey *j, unsigned effects)
{
    memset(j, 0, sizeof *j);
    j->effects = effects;
    j->route = j->stay = -1;
    j->silt = 1;
    j->stamina = j->stamina_max = 1.0f;
    if (fluid_init(&j->water, J_GRID, J_VISC)) return 1;
    swimmer_init(&j->fish, START_X, START_Y, 0.0f);
    j->scene = JS_NONE;
    open_scene(j, JS_OUTLET);
    return 0;
}

void journey_free(Journey *j) { fluid_free(&j->water); }

static void clear_reach(Journey *j)
{
    static const int next[J_REACHES] = {JS_CULVERT, JS_EDDY, JS_WARM, JS_REEDS, JS_SPRING};
    int scene = next[j->reach];
    if (j->reach == 1 && j->route == 1 && on(j, JE_ROUTE)) j->time += DITCH_WAIT;
    j->reach++;
    /* The heavy scenes and the ending get the screen to themselves: no cleared banner. */
    if (!journey_heavy(scene) && scene != JS_SPRING) raise_banner(j, JB_CLEAR);
    open_scene(j, scene);
}

void journey_step(Journey *j, float turn, int stroke)
{
    if (j->scene != JS_NONE || j->reach >= J_REACHES || !j->water.u) return;
    if (!isfinite(turn)) turn = 0.0f;
    int r = j->reach;
    int beat = stroke && !j->winded && j->stamina > 0.0f;
    if (beat) {
        j->stamina -= drain_rate(j) * J_DT;
        if (j->stamina <= 0.0f) { j->stamina = 0.0f; j->winded = 1; }
    } else {
        j->stamina = fminf(j->stamina_max, j->stamina + regen_rate(j) * J_DT);
        if (j->winded && j->stamina >= 0.35f * j->stamina_max) j->winded = 0;
    }
    Controls c = {fmaxf(-1.0f, fminf(1.0f, turn)), beat};
    sluice(j);
    swimmer_step(&j->fish, &j->water, &c, J_DT);
    fluid_step(&j->water, J_DT);
    j->time += J_DT;
    j->reach_time += J_DT;
    if (j->stamina < 0.2f * j->stamina_max) j->strain += J_DT;
    if (j->stamina < 0.12f * j->stamina_max) j->crisis_reach[r] = 1;
    if (j->banner_left > 0.0f && (j->banner_left -= J_DT) <= 0.0f) j->banner = JB_NONE;
    if (j->fish.x > j->best_x) j->best_x = j->fish.x;

    /* Swept back: the current took more than SWEEP_LOSS of the ground won. The pond keeps emptying. */
    if (j->best_x - j->fish.x > SWEEP_LOSS) {
        j->sweeps++;
        j->sweeps_reach[r]++;
        j->crisis_reach[r] = 1;
        j->flow *= EASE_FLOW;
        j->best_x = j->fish.x;
        raise_banner(j, JB_SWEPT);
        return;
    }
    /* A reach that takes too long gets easier the same way. */
    if (j->reach_time >= EASE_SECONDS) { j->flow *= EASE_FLOW; j->reach_time = 0.0f; }

    if (r == 4 && !j->surged && j->fish.x >= 0.5f) {
        j->surged = 1;
        open_scene(j, JS_SURGE);
        return;
    }
    float dx = j->fish.x - j->mark_x[j->mark], dy = j->fish.y - j->mark_y[j->mark];
    if (dx * dx + dy * dy <= MARK_RADIUS * MARK_RADIUS && ++j->mark >= j->marks) clear_reach(j);
}

int journey_answer(Journey *j, int option)
{
    int scene = j->scene;
    if (scene == JS_NONE || option < 0 || option >= journey_options(scene)) return 1;
    j->scene = JS_NONE;
    switch (scene) {
    case JS_OUTLET:
        start_reach(j);
        break;
    case JS_CULVERT:
        j->route = option;
        start_reach(j);
        break;
    case JS_EDDY:
        if (on(j, JE_EDDY)) { j->stamina_max = EDDY_RESERVE; j->stamina = EDDY_RESERVE; }
        start_reach(j);
        break;
    case JS_WARM:
        open_scene(j, JS_TASTE);
        break;
    case JS_TASTE:
        start_reach(j);
        break;
    case JS_REEDS:
        j->stay = option;
        if (on(j, JE_PARTING)) j->silt = 0;
        if (option == 0 && on(j, JE_STAY)) { j->time += STAY_SECONDS; j->stamina = j->stamina_max; }
        start_reach(j);
        break;
    case JS_SURGE:
        if (on(j, JE_SURGE)) j->flow *= j->stay == 1 && on(j, JE_STAY) ? GO_SURGE : SURGE_FLOW;
        break;
    case JS_SPRING:
        break;
    default:
        return 1;
    }
    return 0;
}

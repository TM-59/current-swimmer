#ifndef STORY_RUN_H
#define STORY_RUN_H

/* One whole journey with the human-like policy; shared by the story test and bench. */
#include "story_policy.h"

#include <string.h>

enum { RUN_GUARD_STEPS = 60000 };  /* 1,200 s of swimming: counted as "did not finish" */

typedef struct {
    float seconds, strain;
    int sweeps, completed, ending;
    int crisis[J_REACHES], failure[J_REACHES];
    int order[JS_COUNT], order_len;
    int quiet[JS_COUNT];  /* heavy scene shown with no cleared banner, before or after it */
} RunResult;

static const char *const SCENE_ID[JS_COUNT] = {
    "outlet", "culvert", "eddy", "warm", "taste", "reeds", "surge", "spring"};

static inline void run_journey(uint64_t key, unsigned effects, int route, int stay, RunResult *out)
{
    Journey j;
    Policy p;
    memset(out, 0, sizeof *out);
    out->ending = -1;
    if (journey_init(&j, effects)) return;
    policy_init(&p, key);
    int steps = 0;
    while (j.reach < J_REACHES && steps < RUN_GUARD_STEPS) {
        if (j.scene != JS_NONE) {
            int scene = j.scene, loud = j.loud, calm = j.banner == JB_NONE;
            int option = scene == JS_CULVERT ? route : scene == JS_REEDS ? stay : 0;
            journey_answer(&j, option);
            if (journey_heavy(scene)) {
                float turn; int stroke;
                policy_controls(&p, &j, &turn, &stroke);
                journey_step(&j, turn, stroke);  /* the first step after the scene closes */
                out->quiet[scene] = calm && j.loud == loud && j.banner != JB_CLEAR;
            }
            continue;
        }
        float turn; int stroke;
        policy_controls(&p, &j, &turn, &stroke);
        journey_step(&j, turn, stroke);
        steps++;
    }
    out->completed = j.reach >= J_REACHES;
    out->seconds = j.time;
    out->strain = j.strain;
    out->sweeps = j.sweeps;
    out->ending = journey_ending(&j);
    for (int r = 0; r < J_REACHES; r++) { out->crisis[r] = j.crisis_reach[r]; out->failure[r] = j.sweeps_reach[r]; }
    out->order_len = j.order_len;
    memcpy(out->order, j.order, sizeof j.order);
    journey_free(&j);
}

/* Seeds: every row and run gets its own stirred key (no XOR or sums of raw keys). */
#define STORY_BASE 20261102ULL
enum { SALT_SEED = 1, SALT_PLAY = 2, SALT_ABLATION = 3, SALT_TENSION = 4 };
static inline uint64_t story_seed(int i) { return sub_seed(STORY_BASE, (uint64_t)i, SALT_SEED); }
static inline uint64_t play_key(int i) { return sub_seed(story_seed(i), 0, SALT_PLAY); }
static inline uint64_t ablation_key(int i) { return sub_seed(story_seed(i), 0, SALT_ABLATION); }
static inline uint64_t tension_key(int i, int run) { return sub_seed(story_seed(i), (uint64_t)run, SALT_TENSION); }

enum { PLAY_SEEDS = 12, ABLATION_SEEDS = 20, TENSION_SEEDS = 16, TENSION_RUNS = 8 };
/* Default choices for the ablation rows: the pipe, and leaving Silt at once. */
enum { DEFAULT_ROUTE = 0, DEFAULT_STAY = 1 };
static inline void play_choices(int i, int *route, int *stay) { *route = i % 2; *stay = (i / 2) % 2; }
static inline void tension_choices(int run, int *route, int *stay) { *route = run % 2; *stay = (run / 2) % 2; }

#endif

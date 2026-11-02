#ifndef JOURNEY_H
#define JOURNEY_H

#include "fluid.h"
#include "swimmer.h"

/* "Upstream": a story layer on top of the unchanged fluid and disc model.
 * The pond is being drained; a sluice jet at the right wall pushes water
 * towards the swimmer, and Copperfin has to reach the cold spring upstream
 * in five reaches. Story beats open at fixed progress points (a reach starts,
 * a reach is cleared, the halfway mark of the last reach) and change game
 * numbers only: the sluice jet strength, and the stamina budget for strokes.
 * The physics in fluid.c and swimmer.c is not modified. */

enum { J_REACHES = 5, J_MAX_MARKS = 4 };

enum {
    JS_NONE = -1,
    JS_OUTLET,   /* the sluice board is lifted; Silt sets the pace */
    JS_CULVERT,  /* choice: the pipe or the flooded ditch */
    JS_EDDY,     /* a back-eddy behind a stone: rest */
    JS_WARM,     /* warm still water: "we made it" */
    JS_TASTE,    /* it is the greenhouse drain, not the spring */
    JS_REEDS,    /* Silt cannot go on; choice: stay with him or leave */
    JS_SURGE,    /* the second board comes out; the pond is gone */
    JS_SPRING,   /* the cold spring; ending */
    JS_COUNT
};

/* Numeric story effects, one bit each, so the bench can remove them one by one. */
enum {
    JE_PACE = 1u << 0,     /* swimming with Silt: strokes cost less stamina */
    JE_ROUTE = 1u << 1,    /* the pipe narrows the water: stronger jet in reach 2; the ditch is slack and ends at a weir */
    JE_EDDY = 1u << 2,     /* rest in the eddy: stamina refilled, a larger reserve for reaches 3 and 4 */
    JE_WARM = 1u << 3,     /* warm drain water: stamina comes back at half speed in reach 4 */
    JE_PARTING = 1u << 4,  /* Silt stays behind: no more pace in reach 5 */
    JE_STAY = 1u << 5,     /* staying with Silt: stamina back, faster recovery; the last board comes out as you leave */
    JE_SURGE = 1u << 6,    /* the second board: stronger jet from the halfway mark of reach 5 */
    JE_ALL = (1u << 7) - 1u
};

enum { JB_NONE, JB_CLEAR, JB_SWEPT };

typedef struct {
    Fluid water;
    Swimmer fish;
    unsigned effects;
    int reach;            /* 0..4; J_REACHES when the journey is over */
    int scene;            /* JS_* open now, or JS_NONE */
    int route;            /* -1 until chosen; 0 = pipe, 1 = ditch */
    int stay;             /* -1 until chosen; 0 = stay with Silt, 1 = leave */
    int silt;             /* 1 while Silt swims with Copperfin */
    int surged;
    int mark, marks;      /* next waypoint index and count for this reach */
    float mark_x[J_MAX_MARKS], mark_y[J_MAX_MARKS];
    float stamina, stamina_max;
    int winded;           /* stamina ran out: strokes do nothing until it is back to 35 % */
    float flow;           /* sluice jet strength for this reach */
    float time;           /* journey seconds in the water (every second is a swimming step; no flat waits) */
    float strain;         /* seconds spent with stamina below 20 % */
    float reach_time;
    float held;           /* seconds held at a hold mark so far (the ditch's weir, the reeds beside Silt) */
    float best_x;
    int sweeps;
    int sweeps_reach[J_REACHES];
    int crisis_reach[J_REACHES];  /* 1 if stamina dropped below 12 % or the swimmer was swept back */
    int banner;           /* JB_* shown on screen */
    float banner_left;
    int loud;             /* number of times a "cleared" banner was raised */
    int order[JS_COUNT];
    int order_len;
} Journey;

int journey_init(Journey *j, unsigned effects);
void journey_free(Journey *j);
/* One fixed 0.02 s step. Does nothing while a scene is open or after the end. */
void journey_step(Journey *j, float turn, int stroke);
/* Close the open scene. Choice scenes take option 0 or 1; others take 0. Returns 0 when accepted. */
int journey_answer(Journey *j, int option);
/* 2 for a choice scene, 1 otherwise. */
int journey_options(int scene);
/* 1 for scenes that must be shown without a cleared banner (the heavy scenes). */
int journey_heavy(int scene);
/* Seconds the swimmer must hold within journey_hold_radius() of the current mark (the ditch's weir, the reeds when
 * staying with Silt); 0 for an ordinary mark. The clock and the current keep running while it holds. */
float journey_hold_need(const Journey *j);
float journey_hold_radius(void);
/* Index of the ending reached: 0 = stayed with Silt, 1 = left him; -1 before the spring. */
int journey_ending(const Journey *j);

#endif

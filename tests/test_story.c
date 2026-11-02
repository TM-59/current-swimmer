/* "Upstream" story layer: scene order, the numbers each story beat changes, and quiet heavy scenes. */
#include "story_run.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const int DESIGN[JS_COUNT] = {JS_OUTLET, JS_CULVERT, JS_EDDY, JS_WARM, JS_TASTE, JS_REEDS, JS_SURGE, JS_SPRING};

static int close_to(float a, float b) { return fabsf(a - b) <= 1e-4f * fmaxf(1.0f, fabsf(b)); }

/* Swim with the policy, answering scenes with the given choices, until `target` opens. Returns steps swum. */
static int drive(Journey *j, Policy *p, int target, int route, int stay)
{
    int steps = 0;
    while (j->scene != target) {
        assert(j->reach < J_REACHES && steps < RUN_GUARD_STEPS);
        if (j->scene != JS_NONE) {
            assert(journey_answer(j, j->scene == JS_CULVERT ? route : j->scene == JS_REEDS ? stay : 0) == 0);
            continue;
        }
        float turn; int stroke;
        policy_controls(p, j, &turn, &stroke);
        journey_step(j, turn, stroke);
        steps++;
    }
    return steps;
}

static void start(Journey *j, Policy *p, unsigned effects)
{
    assert(journey_init(j, effects) == 0);
    policy_init(p, play_key(0));
    assert(j->scene == JS_OUTLET && journey_options(JS_OUTLET) == 1);
}

int main(void)
{
    /* [order] the bench's play-order.json (re-checked against the code by story_bench --check) has every seed
     * meeting the scenes in the designed order: setups before payoffs, the bottom before the end, both endings reached. */
    {
        FILE *f = fopen("docs/narrative/play-order.json", "rb");
        assert(f);
        static char text[1 << 16];
        size_t len = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[len] = 0;
        char want[256] = "\"order\": [";
        for (int k = 0; k < JS_COUNT; k++) {
            strcat(want, k ? ", \"" : "\"");
            strcat(want, SCENE_ID[DESIGN[k]]);
            strcat(want, "\"");
        }
        strcat(want, "], \"won\": true");
        int seeds = 0, orders = 0;
        for (const char *c = text; (c = strstr(c, "\"seed\": ")); c++) seeds++;
        for (const char *c = text; (c = strstr(c, want)); c++) orders++;
        assert(seeds >= 8 && orders == seeds);
        assert(strstr(text, "\"ending\": \"end-stay\", \"quiet\": [\"reeds\", \"surge\"]"));
        assert(strstr(text, "\"ending\": \"end-left\", \"quiet\": [\"reeds\", \"surge\"]"));
        printf("[order] %d seeds: outlet .. spring in the designed order; both endings reached\n", seeds);
    }
    {
        RunResult r;
        run_journey(play_key(1), JE_ALL, 1, 0, &r);
        assert(r.completed && r.order_len == JS_COUNT && r.ending == 0 && r.quiet[JS_REEDS] && r.quiet[JS_SURGE]);
        for (int k = 0; k < JS_COUNT; k++) assert(r.order[k] == DESIGN[k]);
    }

    /* [mech] pace: while Silt sets the pace a beat of the tail costs 60 % of the stamina. */
    {
        Journey a, b; Policy p;
        start(&a, &p, JE_ALL); start(&b, &p, JE_ALL & ~JE_PACE);
        assert(journey_answer(&a, 0) == 0 && journey_answer(&b, 0) == 0);
        for (int k = 0; k < 50; k++) { journey_step(&a, 0.0f, 1); journey_step(&b, 0.0f, 1); }
        float da = 1.0f - a.stamina, db = 1.0f - b.stamina;
        assert(a.silt == 1 && close_to(db, 0.5f) && close_to(da / db, 0.6f));
        journey_free(&a); journey_free(&b);
        puts("[mech] pace: stroke cost x0.60 with Silt");
    }

    /* [mech] route: the pipe squeezes the jet; the ditch is still water and ends at a weir. */
    {
        Journey a, b; Policy p, q;
        start(&a, &p, JE_ALL); start(&b, &q, JE_ALL);
        drive(&a, &p, JS_CULVERT, 0, 0); drive(&b, &q, JS_CULVERT, 1, 0);
        assert(journey_options(JS_CULVERT) == 2);
        assert(journey_answer(&a, 0) == 0 && journey_answer(&b, 1) == 0);
        assert(a.route == 0 && b.route == 1 && close_to(a.flow, 0.60f * 1.30f) && close_to(b.flow, 0.60f));
        assert(a.marks == 2 && b.marks == 3);
        float sa = a.stamina, sb = b.stamina;
        journey_step(&a, 0.0f, 1); journey_step(&b, 0.0f, 1);
        assert(close_to((sb - b.stamina) / (sa - a.stamina), 0.70f));
        float ta = a.time, tb = b.time;
        int na = drive(&a, &p, JS_EDDY, 0, 0), nb = drive(&b, &q, JS_EDDY, 1, 0);
        assert(fabsf(a.time - ta - (float)na * 0.02f) < 0.05f);
        assert(fabsf(b.time - tb - (float)nb * 0.02f - 45.0f) < 0.05f);
        journey_free(&a); journey_free(&b);
        puts("[mech] route: pipe jet x1.30; ditch strokes x0.70 and a 45 s wait at the weir");
    }

    /* [mech] eddy: resting behind the stone fills a larger reserve for the next reach. */
    {
        Journey a, b; Policy p, q;
        start(&a, &p, JE_ALL); start(&b, &q, JE_ALL & ~JE_EDDY);
        drive(&a, &p, JS_EDDY, 0, 0); drive(&b, &q, JS_EDDY, 0, 0);
        b.stamina = 0.4f;
        assert(journey_answer(&a, 0) == 0 && journey_answer(&b, 0) == 0);
        assert(close_to(a.stamina, 1.25f) && close_to(a.stamina_max, 1.25f));
        assert(close_to(b.stamina, 0.4f) && close_to(b.stamina_max, 1.0f));
        journey_free(&a); journey_free(&b);
        puts("[mech] eddy: stamina refilled to a 1.25 reserve");
    }

    /* [mech] warm: in the drain water stamina comes back at 32 % of the usual rate. */
    {
        Journey a, b; Policy p, q;
        start(&a, &p, JE_ALL); start(&b, &q, JE_ALL & ~JE_WARM);
        drive(&a, &p, JS_TASTE, 0, 0); drive(&b, &q, JS_TASTE, 0, 0);
        assert(journey_answer(&a, 0) == 0 && journey_answer(&b, 0) == 0);
        assert(a.reach == 3 && b.reach == 3);
        a.stamina = b.stamina = 0.5f;
        for (int k = 0; k < 10; k++) { journey_step(&a, 0.0f, 0); journey_step(&b, 0.0f, 0); }
        assert(close_to(b.stamina - 0.5f, 10 * 0.02f * 0.25f) && close_to((a.stamina - 0.5f) / (b.stamina - 0.5f), 0.32f));
        journey_free(&a); journey_free(&b);
        puts("[mech] warm: recovery x0.32 in the drain water");
    }

    /* [mech] parting, stay and surge: what Silt's last stop costs on each path. */
    for (int stay = 0; stay < 2; stay++) {
        Journey a, b; Policy p, q;
        start(&a, &p, JE_ALL); start(&b, &q, JE_ALL & ~JE_PARTING & ~JE_STAY & ~JE_SURGE);
        drive(&a, &p, JS_REEDS, 0, stay); drive(&b, &q, JS_REEDS, 0, stay);
        assert(journey_options(JS_REEDS) == 2 && journey_heavy(JS_REEDS));
        /* [quiet] reeds: no cleared banner when Silt stops, nor on the next stroke. */
        assert(a.banner == JB_NONE);
        int loud = a.loud;
        a.stamina = b.stamina = 0.3f;
        float ta = a.time, tb = b.time;
        assert(journey_answer(&a, stay) == 0 && journey_answer(&b, stay) == 0);
        assert(a.silt == 0 && b.silt == 1);
        assert(stay == 0 ? close_to(a.time - ta, 45.0f) && close_to(a.stamina, 1.0f) : close_to(a.time, ta) && close_to(a.stamina, 0.3f));
        assert(close_to(b.time, tb) && close_to(b.stamina, 0.3f));
        journey_step(&a, 0.0f, 0);
        assert(a.loud == loud && a.banner != JB_CLEAR);
        if (stay == 0) puts("[quiet] reeds: shown with no cleared banner");
        drive(&a, &p, JS_SURGE, 0, stay); drive(&b, &q, JS_SURGE, 0, stay);
        /* [quiet] surge: the pond is gone; the swept banner is cleared and nothing loud follows. */
        assert(journey_heavy(JS_SURGE) && a.banner == JB_NONE && b.banner == JB_NONE);
        loud = a.loud;
        float fa = a.flow, fb = b.flow;
        assert(journey_answer(&a, 0) == 0 && journey_answer(&b, 0) == 0);
        assert(close_to(a.flow / fa, stay == 0 ? 1.30f : 1.15f) && close_to(b.flow, fb));
        journey_step(&a, 0.0f, 1);
        assert(a.loud == loud && a.banner != JB_CLEAR);
        if (stay == 0) puts("[quiet] surge: shown with no cleared banner");
        drive(&a, &p, JS_SPRING, 0, stay);
        assert(journey_ending(&a) == stay && a.loud == loud && a.banner != JB_CLEAR);
        journey_free(&a); journey_free(&b);
    }
    puts("[mech] parting: Silt stays behind; [mech] stay: 45 s beside him, stamina back; [mech] surge: jet x1.30, x1.15 if you left at once");

    /* An ordinary cleared reach is loud, so the quiet checks above are not vacuous. */
    {
        Journey a; Policy p;
        start(&a, &p, JE_ALL);
        drive(&a, &p, JS_CULVERT, 0, 0);
        assert(a.banner == JB_CLEAR && a.loud == 1);
        assert(journey_answer(&a, 2) == 1 && journey_answer(&a, -1) == 1);
        journey_free(&a);
    }
    puts("story tests passed");
    return 0;
}

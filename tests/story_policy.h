#ifndef STORY_POLICY_H
#define STORY_POLICY_H

/* Shared by the story test and the story bench: seeds and a human-like swimmer. */
#include "../src/journey.h"

#include <math.h>
#include <stdint.h>

static inline uint64_t mix64(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* Seed and index are stirred separately before they are combined (no XOR or sums of raw keys). */
static inline uint64_t sub_seed(uint64_t seed, uint64_t index, uint64_t salt)
{
    return mix64(mix64(seed + salt * 0x632be59bd9b4e019ULL) + (index + 1u) * 0x9e3779b97f4a7c15ULL);
}

typedef struct { uint64_t s; } Rng;
static inline float rng_unit(Rng *r)
{
    r->s += 0x9e3779b97f4a7c15ULL;
    return (float)(mix64(r->s) >> 40) / 16777216.0f;
}

/* A swimmer who does not know the numbers: rests when the stamina bar looks low,
 * beats the tail in bursts, steers towards the next mark with some wobble. */
typedef struct {
    Rng rng;
    float rest, resume, burst, glide, phase, wobble, wobble_left;
    int resting;
} Policy;

static inline void policy_init(Policy *p, uint64_t key)
{
    p->rng.s = key;
    p->rest = 0.02f + 0.18f * rng_unit(&p->rng);
    p->resume = 0.40f + 0.40f * rng_unit(&p->rng);
    p->burst = 0.45f + 0.45f * rng_unit(&p->rng);
    p->glide = 0.10f + 0.30f * rng_unit(&p->rng);
    p->phase = 0.0f;
    p->wobble = 0.0f;
    p->wobble_left = 0.0f;
    p->resting = 0;
}

static inline void policy_controls(Policy *p, const Journey *j, float *turn, int *stroke)
{
    float tx = j->mark_x[j->mark], ty = j->mark_y[j->mark];
    float want = atan2f(ty - j->fish.y, tx - j->fish.x);
    if ((p->wobble_left -= 0.02f) <= 0.0f) {
        p->wobble = (rng_unit(&p->rng) - 0.5f) * 0.5f;
        p->wobble_left = 0.4f + 0.4f * rng_unit(&p->rng);
    }
    float err = remainderf(want + p->wobble - j->fish.heading, 6.2831853f);
    *turn = fmaxf(-1.0f, fminf(1.0f, err * 2.5f));
    float level = j->stamina / j->stamina_max;
    if (!p->resting && level < p->rest) p->resting = 1;
    if (p->resting && level >= p->resume) p->resting = 0;
    p->phase += 0.02f;
    if (p->phase >= p->burst + p->glide) p->phase = 0.0f;
    *stroke = !p->resting && !j->winded && p->phase < p->burst;
}

#endif

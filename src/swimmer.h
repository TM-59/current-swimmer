#ifndef SWIMMER_H
#define SWIMMER_H

#include "fluid.h"

/* A neutrally buoyant disc swimmer. Added mass of a cylinder equals the displaced
 * fluid mass, so the effective inertia is twice the body mass. */
typedef struct {
    float x, y;       /* position in the unit square */
    float vx, vy;
    float heading;    /* radians, 0 = +x */
    float radius;
    float mass;
    float thrust;     /* last applied thrust force */
    float drag;       /* last drag magnitude */
} Swimmer;

typedef struct {
    float turn;   /* -1 (clockwise) .. +1 (counter-clockwise) */
    int stroke;   /* 1 while the tail beats */
} Controls;

void swimmer_init(Swimmer *s, float x, float y, float heading);
float swimmer_speed(const Swimmer *s);
/* Advance the body one step and push the equal and opposite forces into the fluid. */
void swimmer_step(Swimmer *s, Fluid *f, const Controls *c, float dt);

#endif

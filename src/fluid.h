#ifndef FLUID_H
#define FLUID_H

#include "vec.h"

/* 2D incompressible flow on the unit square, solved with Stam's "Stable Fluids".
 * Grid cells are 1..n in each direction; index 0 and n+1 are wall ghost cells.
 * Velocities are in domain-lengths per second. */
typedef struct {
    int n;
    float visc;     /* kinematic viscosity [domain^2 / s] */
    float *u, *v;   /* velocity components */
    float *u0, *v0; /* scratch */
    float *p, *div; /* pressure and divergence scratch */
} Fluid;

/* n cells per side and the kinematic viscosity [domain^2 / s]. */
typedef struct {
    int n;
    float visc;
} FluidSpec;

/* A Gaussian blob of force: centre (x, y), momentum per unit time (fx, fy) and its radius. */
typedef struct {
    float x, y, fx, fy, radius;
} ForceBlob;

int fluid_init(Fluid *f, FluidSpec spec);
void fluid_free(Fluid *f);
void fluid_step(Fluid *f, float dt);

/* Add the blob's momentum per unit time for dt. */
void fluid_add_force(Fluid *f, ForceBlob blob, float dt);
/* Bilinear velocity sample at a point in [0, 1]^2. */
Vec2f fluid_sample(const Fluid *f, Vec2f at);

float fluid_kinetic_energy(const Fluid *f);
float fluid_max_divergence(const Fluid *f);
float fluid_vorticity_at(const Fluid *f, int i, int j);
float fluid_max_abs_vorticity(const Fluid *f);
float fluid_total_circulation(const Fluid *f);

/* Reynolds number U * L / nu for body length L. */
float reynolds_number(float speed, float length, float visc);

#endif

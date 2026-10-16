#ifndef SVG_H
#define SVG_H

typedef struct {
    const char *name;
    const float *y;
    const char *color;
} Series;

/* Write a self-contained line chart. Returns 0 on success. */
int svg_plot(const char *path, const char *title, const char *xlabel, const char *ylabel,
             const float *x, int count, const Series *series, int nseries);

#endif

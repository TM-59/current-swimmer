#ifndef VIEW_H
#define VIEW_H

/* Browser readout of the existing C simulation; unit square, fixed 0.02 s step. */
enum { VIEW_GRID = 48, VIEW_STATE_COUNT = 12 };
enum { VIEW_X, VIEW_Y, VIEW_HEADING, VIEW_SPEED, VIEW_RE, VIEW_THRUST,
       VIEW_DRAG, VIEW_ENERGY, VIEW_DIV, VIEW_TIME, VIEW_RADIUS, VIEW_VORT };

int view_init(void);
void view_step(float turn, int stroke);
const float *view_state(void);
/* Row-major cells from bottom left, each cell: signed vorticity, u, v. */
const float *view_field(void);

#endif

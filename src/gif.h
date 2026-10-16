#ifndef GIF_H
#define GIF_H

/* Minimal animated GIF89a writer: 16-colour palette, LZW compression, looping, no dependencies. */
typedef struct Gif Gif;

#define GIF_COLOURS 16

/* delay_cs is the frame delay in hundredths of a second. Returns NULL on failure. */
Gif *gif_open(const char *path, int width, int height, unsigned char palette[GIF_COLOURS][3], int delay_cs);
/* pixels: width * height palette indices (0..15). `hold_cs` > 0 overrides the delay of this frame. */
int gif_frame(Gif *g, const unsigned char *pixels, int hold_cs);
int gif_close(Gif *g);

/* Text screens: each character is drawn with a built-in 5x7 glyph in a 6x9 cell, times scale. */
void gif_text_size(const char *text, int scale, int *width, int *height);
/* Clears the buffer to index 0 and draws `text` (lines separated by '\n'). */
void gif_draw_text(unsigned char *pixels, int width, int height, const char *text, int scale,
                   int (*colour_of)(char));

#endif

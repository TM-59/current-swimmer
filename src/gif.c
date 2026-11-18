#include "gif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_CODE 4U
#define CLEAR (1U << MIN_CODE)
#define EOI (CLEAR + 1U)
#define HASH 8209

struct Gif {
    FILE *f;
    int w, h, delay;
    unsigned char block[255];
    int blen;
    unsigned long acc;
    unsigned nbits;
    unsigned code_bits; /* width of the next LZW code */
    int hkey[HASH];
    int hval[HASH];
};

static void put16(FILE *f, unsigned v)
{
    (void)fputc((int)(v & 255U), f);
    (void)fputc((int)((v >> 8U) & 255U), f);
}

Gif *gif_open(const char *path, int width, int height, unsigned char palette[GIF_COLOURS][3],
              int delay_cs)
{
    Gif *g = calloc(1, sizeof *g);
    if (!g) {
        return NULL;
    }
    g->f = fopen(path, "wb");
    if (!g->f) {
        free(g);
        return NULL;
    }
    g->w = width;
    g->h = height;
    g->delay = delay_cs;
    (void)fwrite("GIF89a", 1, 6, g->f);
    put16(g->f, (unsigned)width);
    put16(g->f, (unsigned)height);
    (void)fputc(0xB3, g->f); /* global table, 16 colours */
    (void)fputc(0, g->f);
    (void)fputc(0, g->f);
    for (int i = 0; i < GIF_COLOURS; i++) {
        (void)fwrite(palette[i], 1, 3, g->f);
    }
    (void)fwrite("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 1, 19, g->f);
    return g;
}

static void emit_byte(Gif *g, int b)
{
    g->block[g->blen++] = (unsigned char)b;
    if (g->blen == 255) {
        (void)fputc(255, g->f);
        (void)fwrite(g->block, 1, 255, g->f);
        g->blen = 0;
    }
}

/* Appends one code of the current width (g->code_bits) to the bit stream. */
static void put_code(Gif *g, unsigned code)
{
    g->acc |= (unsigned long)code << g->nbits;
    g->nbits += g->code_bits;
    while (g->nbits >= 8U) {
        emit_byte(g, (int)(g->acc & 255U));
        g->acc >>= 8U;
        g->nbits -= 8U;
    }
}

static void reset_table(Gif *g)
{
    for (int i = 0; i < HASH; i++) {
        g->hkey[i] = -1;
    }
}

static int *lookup(Gif *g, int key)
{
    int slot = (int)(((unsigned)key * 2654435761u) % HASH);
    while (g->hkey[slot] != -1 && g->hkey[slot] != key) {
        slot = (slot + 1) % HASH;
    }
    g->hkey[slot] = key;
    return &g->hval[slot];
}

static int find(Gif *g, int key)
{
    int slot = (int)(((unsigned)key * 2654435761u) % HASH);
    while (g->hkey[slot] != -1) {
        if (g->hkey[slot] == key) {
            return g->hval[slot];
        }
        slot = (slot + 1) % HASH;
    }
    return -1;
}

int gif_frame(Gif *g, const unsigned char *pixels, int hold_cs)
{
    int delay = hold_cs > 0 ? hold_cs : g->delay;
    (void)fwrite("\x21\xF9\x04\x04", 1, 4, g->f);
    put16(g->f, (unsigned)delay);
    (void)fputc(0, g->f);
    (void)fputc(0, g->f);
    (void)fputc(0x2C, g->f);
    put16(g->f, 0U);
    put16(g->f, 0U);
    put16(g->f, (unsigned)g->w);
    put16(g->f, (unsigned)g->h);
    (void)fputc(0, g->f);
    (void)fputc((int)MIN_CODE, g->f);

    int n = g->w * g->h;
    unsigned next = EOI + 1U;
    g->acc = 0;
    g->nbits = 0;
    g->code_bits = MIN_CODE + 1U;
    g->blen = 0;
    reset_table(g);
    put_code(g, CLEAR);
    unsigned prefix = pixels[0] & 15U;
    for (int i = 1; i < n; i++) {
        unsigned c = pixels[i] & 15U;
        int key = (int)((prefix << 8U) | c), hit = find(g, key);
        if (hit >= 0) {
            prefix = (unsigned)hit;
            continue;
        }
        put_code(g, prefix);
        *lookup(g, key) = (int)next++;
        if (next == 4096U) {
            put_code(g, CLEAR);
            reset_table(g);
            g->code_bits = MIN_CODE + 1U;
            next = EOI + 1U;
        } else if (next > (1U << g->code_bits)) {
            g->code_bits++;
        }
        prefix = c;
    }
    put_code(g, prefix);
    /* the decoder adds one more entry after the last code, which may widen the next code */
    if (++next > (1U << g->code_bits) && g->code_bits < 12U) {
        g->code_bits++;
    }
    put_code(g, EOI);
    if (g->nbits > 0U) {
        emit_byte(g, (int)(g->acc & 255U));
    }
    if (g->blen > 0) {
        (void)fputc(g->blen, g->f);
        (void)fwrite(g->block, 1, (size_t)g->blen, g->f);
    }
    (void)fputc(0, g->f);
    return ferror(g->f) ? 1 : 0;
}

int gif_close(Gif *g)
{
    (void)fputc(0x3B, g->f);
    int bad = ferror(g->f) != 0;
    if (fclose(g->f)) {
        bad = 1;
    }
    free(g);
    return bad;
}

/* 5x7 glyphs, one byte per row, bit 4 is the leftmost pixel. Characters not listed here are drawn
 * by dedicated code in draw_cell. */
static const unsigned char *glyph(char ch)
{
    static const unsigned char gt[7] = {16, 24, 28, 30, 28, 24, 16};
    static const unsigned char lt[7] = {1, 3, 7, 15, 7, 3, 1};
    static const unsigned char up[7] = {4, 14, 31, 14, 14, 14, 0};
    static const unsigned char dn[7] = {0, 14, 14, 14, 31, 14, 4};
    static const unsigned char sl[7] = {1, 2, 2, 4, 8, 8, 16};
    static const unsigned char bs[7] = {16, 8, 8, 4, 2, 2, 1};
    static const unsigned char dot[7] = {14, 31, 31, 31, 31, 31, 14};
    static const unsigned char star[7] = {4, 21, 14, 31, 14, 21, 4};
    static const unsigned char dash[7] = {0, 0, 0, 31, 0, 0, 0};
    switch (ch) {
    case '>':
        return gt;
    case '<':
        return lt;
    case '^':
        return up;
    case 'v':
        return dn;
    case '/':
        return sl;
    case '\\':
        return bs;
    case 'o':
    case '@':
        return dot;
    case '*':
        return star;
    case '-':
        return dash;
    default:
        return NULL;
    }
}

GifSize gif_text_size(const char *text, int scale)
{
    int cols = 0, rows = 0, cur = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            rows++;
            if (cur > cols) {
                cols = cur;
            }
            cur = 0;
        } else {
            cur++;
        }
    }
    if (cur > 0) {
        rows++;
        if (cur > cols) {
            cols = cur;
        }
    }
    GifSize size = {.width = cols * 6 * scale, .height = rows * 9 * scale};
    return size;
}

typedef struct {
    unsigned char *px;
    int w, h;
} Canvas;

/* A pixel rectangle [x0, x1) x [y0, y1). */
typedef struct {
    int x0, y0, x1, y1;
} Box;

/* One character at column `col` and row `row` of the text grid, `scale` pixels per glyph dot. */
typedef struct {
    int col, row, scale;
    char ch;
} Cell;

static void fill(const Canvas *cv, Box b, int colour)
{
    for (int y = b.y0; y < b.y1 && y < cv->h; y++) {
        for (int x = b.x0; x < b.x1 && x < cv->w; x++) {
            if (x >= 0 && y >= 0) {
                cv->px[y * cv->w + x] = (unsigned char)colour;
            }
        }
    }
}

static void draw_cell(const Canvas *cv, const Cell *cell, int colour)
{
    int s = cell->scale, x0 = cell->col * 6 * s, y0 = cell->row * 9 * s;
    char ch = cell->ch;
    switch (ch) {
    case ' ':
        return;
    case '#':
        fill(cv, (Box){x0, y0, x0 + 6 * s, y0 + 9 * s}, colour);
        return;
    case '=':
        fill(cv, (Box){x0, y0 + 3 * s, x0 + 6 * s, y0 + 6 * s}, colour);
        return;
    case '_':
        fill(cv, (Box){x0, y0 + 8 * s, x0 + 6 * s, y0 + 9 * s}, colour);
        return;
    case '|':
        fill(cv, (Box){x0 + 2 * s, y0, x0 + 4 * s, y0 + 9 * s}, colour);
        return;
    case '.':
        fill(cv, (Box){x0 + 2 * s, y0 + 4 * s, x0 + 4 * s, y0 + 6 * s}, colour);
        return;
    default:
        break;
    }
    const unsigned char *rows = glyph(ch);
    if (!rows) {
        fill(cv, (Box){x0 + 2 * s, y0 + 3 * s, x0 + 4 * s, y0 + 5 * s}, colour);
        return;
    }
    for (int r = 0; r < 7; r++) {
        for (int b = 0; b < 5; b++) {
            if (rows[r] & (16U >> (unsigned)b)) {
                fill(cv, (Box){x0 + b * s, y0 + (r + 1) * s, x0 + (b + 1) * s, y0 + (r + 2) * s},
                     colour);
            }
        }
    }
}

void gif_draw_text(unsigned char *pixels, int width, int height, const char *text, int scale,
                   int (*colour_of)(char))
{
    memset(pixels, 0, (size_t)width * (size_t)height);
    const Canvas cv = {.px = pixels, .w = width, .h = height};
    Cell cell = {.col = 0, .row = 0, .scale = scale, .ch = 0};
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            cell.row++;
            cell.col = 0;
            continue;
        }
        cell.ch = *p;
        draw_cell(&cv, &cell, colour_of(*p));
        cell.col++;
    }
}

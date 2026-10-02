/*
 * VGA bitmap font model, file formats and glyph transforms.
 *
 * This module is plain C (no Windows API) so it can be unit tested on any
 * platform. A glyph is FONT_WIDTH (8) pixels wide and font->height rows tall;
 * each row is one byte with the most significant bit as the leftmost pixel,
 * exactly as the VGA stores it in character generator RAM.
 */
#ifndef FONT_H
#define FONT_H

#include <stddef.h>
#include <wchar.h>

#define FONT_GLYPHS     256
#define FONT_WIDTH      8
#define FONT_MIN_HEIGHT 1
#define FONT_MAX_HEIGHT 32

typedef struct VgaFont {
    int height;
    /* Rows at or beyond height are always zero. */
    unsigned char rows[FONT_GLYPHS][FONT_MAX_HEIGHT];
} VgaFont;

typedef enum FontFormat {
    FONT_FORMAT_RAW,      /* 256 * height bytes, no header (.fnt, .f08, .f14, .f16) */
    FONT_FORMAT_PSF1,     /* PC Screen Font version 1 */
    FONT_FORMAT_PSF2,     /* PC Screen Font version 2 */
    FONT_FORMAT_C_SOURCE  /* C array (export only) */
} FontFormat;

typedef enum FontError {
    FONT_OK = 0,
    FONT_ERR_UNKNOWN_FORMAT,
    FONT_ERR_BAD_HEIGHT,
    FONT_ERR_BAD_WIDTH,
    FONT_ERR_TRUNCATED,
    FONT_ERR_NO_MEMORY
} FontError;

/* Warning flags reported by font_parse(). */
#define FONT_WARN_EXTRA_GLYPHS   0x01  /* more than 256 glyphs, the rest were dropped */
#define FONT_WARN_MISSING_GLYPHS 0x02  /* fewer than 256 glyphs, the rest are blank */
#define FONT_WARN_UNICODE_TABLE  0x04  /* PSF Unicode table was dropped */

typedef enum FontResizeMode {
    FONT_RESIZE_TOP,     /* keep top rows, add/remove rows at the bottom */
    FONT_RESIZE_CENTER,  /* add/remove rows evenly at top and bottom */
    FONT_RESIZE_BOTTOM,  /* keep bottom rows, add/remove rows at the top */
    FONT_RESIZE_SCALE    /* nearest-neighbour vertical scaling */
} FontResizeMode;

void font_init(VgaFont *font, int height);

int  font_get_pixel(const VgaFont *font, int ch, int x, int y);
void font_set_pixel(VgaFont *font, int ch, int x, int y, int on);

/*
 * Pixel as shown on screen. With nine_dot set, x may be 8: the VGA's ninth
 * column, which repeats column 7 for the line drawing codes 0xC0-0xDF and is
 * blank otherwise.
 */
int  font_display_pixel(const VgaFont *font, int ch, int x, int y, int nine_dot);

/*
 * Detects the format (PSF1, PSF2 or raw) and loads the font. height_hint is
 * used for raw files that hold more than 256 glyphs (for example taken from a
 * ".f16" extension); pass 0 if unknown. format and warnings may be NULL.
 */
FontError font_parse(VgaFont *font, const unsigned char *data, size_t size,
                     int height_hint, FontFormat *format, unsigned *warnings);

/*
 * Serializes the font. Returns a malloc'd buffer (free() it) or NULL when out
 * of memory. c_name is the array name for FONT_FORMAT_C_SOURCE and is
 * sanitized into a valid identifier; it is ignored for other formats.
 */
unsigned char *font_serialize(const VgaFont *font, FontFormat format,
                              const char *c_name, size_t *out_size);

void font_resize(VgaFont *font, int new_height, FontResizeMode mode);

/* Single glyph operations. */
void glyph_clear(VgaFont *font, int ch);
void glyph_invert(VgaFont *font, int ch);
void glyph_flip_horizontal(VgaFont *font, int ch);
void glyph_flip_vertical(VgaFont *font, int ch);
void glyph_shift(VgaFont *font, int ch, int dx, int dy);  /* pixels shifted out are lost */
void glyph_bold(VgaFont *font, int ch);                   /* OR with itself shifted right */

/*
 * Glyph <-> text art ('#' = set, '.' = clear, one line per row) for the
 * clipboard. glyph_to_art writes at most cap wide chars including the
 * terminator. glyph_from_art returns 0 if the text does not look like a glyph.
 */
void glyph_to_art(const VgaFont *font, int ch, wchar_t *buf, size_t cap);
int  glyph_from_art(const wchar_t *text, unsigned char rows[FONT_MAX_HEIGHT], int *row_count);

#endif

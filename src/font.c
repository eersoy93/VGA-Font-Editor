#include "font.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PSF1_MAGIC0     0x36
#define PSF1_MAGIC1     0x04
#define PSF1_MODE512    0x01
#define PSF1_MODEHASTAB 0x02
#define PSF1_MODESEQ    0x04

#define PSF2_HEADER_SIZE 32
#define PSF2_HAS_UNICODE_TABLE 0x01

static const unsigned char psf2_magic[4] = { 0x72, 0xB5, 0x4A, 0x86 };

static int valid_height(long h)
{
    return h >= FONT_MIN_HEIGHT && h <= FONT_MAX_HEIGHT;
}

static unsigned long read_le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void write_le32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

void font_init(VgaFont *font, int height)
{
    memset(font, 0, sizeof(*font));
    font->height = valid_height(height) ? height : 16;
}

int font_get_pixel(const VgaFont *font, int ch, int x, int y)
{
    if (ch < 0 || ch >= FONT_GLYPHS || x < 0 || x >= FONT_WIDTH ||
        y < 0 || y >= font->height)
        return 0;
    return (font->rows[ch][y] >> (7 - x)) & 1;
}

void font_set_pixel(VgaFont *font, int ch, int x, int y, int on)
{
    unsigned char mask;

    if (ch < 0 || ch >= FONT_GLYPHS || x < 0 || x >= FONT_WIDTH ||
        y < 0 || y >= font->height)
        return;
    mask = (unsigned char)(0x80 >> x);
    if (on)
        font->rows[ch][y] |= mask;
    else
        font->rows[ch][y] &= (unsigned char)~mask;
}

int font_display_pixel(const VgaFont *font, int ch, int x, int y, int nine_dot)
{
    if (x == FONT_WIDTH && nine_dot && ch >= 0xC0 && ch <= 0xDF)
        return font_get_pixel(font, ch, FONT_WIDTH - 1, y);
    return font_get_pixel(font, ch, x, y);
}

/* Copies count glyphs of stride bytes each (only the first height used). */
static void load_glyphs(VgaFont *font, const unsigned char *src, size_t count,
                        size_t stride, int height)
{
    size_t ch;

    font_init(font, height);
    if (count > FONT_GLYPHS)
        count = FONT_GLYPHS;
    for (ch = 0; ch < count; ch++)
        memcpy(font->rows[ch], src + ch * stride, (size_t)height);
}

static FontError parse_psf1(VgaFont *font, const unsigned char *data,
                            size_t size, unsigned *warnings)
{
    unsigned mode = data[2];
    int height = data[3];
    size_t count = (mode & PSF1_MODE512) ? 512 : 256;

    if (!valid_height(height))
        return FONT_ERR_BAD_HEIGHT;
    if (size < 4 + count * (size_t)height)
        return FONT_ERR_TRUNCATED;
    load_glyphs(font, data + 4, count, (size_t)height, height);
    if (count > FONT_GLYPHS)
        *warnings |= FONT_WARN_EXTRA_GLYPHS;
    if (mode & (PSF1_MODEHASTAB | PSF1_MODESEQ))
        *warnings |= FONT_WARN_UNICODE_TABLE;
    return FONT_OK;
}

static FontError parse_psf2(VgaFont *font, const unsigned char *data,
                            size_t size, unsigned *warnings)
{
    unsigned long header_size = read_le32(data + 8);
    unsigned long flags = read_le32(data + 12);
    unsigned long count = read_le32(data + 16);
    unsigned long char_size = read_le32(data + 20);
    unsigned long height = read_le32(data + 24);
    unsigned long width = read_le32(data + 28);

    if (width < 1 || width > FONT_WIDTH)
        return FONT_ERR_BAD_WIDTH;
    if (!valid_height((long)height))
        return FONT_ERR_BAD_HEIGHT;
    /* One byte per row since width <= 8. */
    if (header_size < PSF2_HEADER_SIZE || char_size < height || count == 0)
        return FONT_ERR_UNKNOWN_FORMAT;
    if (header_size > size || count > (size - header_size) / char_size)
        return FONT_ERR_TRUNCATED;
    load_glyphs(font, data + header_size, count, char_size, (int)height);
    if (count > FONT_GLYPHS)
        *warnings |= FONT_WARN_EXTRA_GLYPHS;
    else if (count < FONT_GLYPHS)
        *warnings |= FONT_WARN_MISSING_GLYPHS;
    if (flags & PSF2_HAS_UNICODE_TABLE)
        *warnings |= FONT_WARN_UNICODE_TABLE;
    return FONT_OK;
}

FontError font_parse(VgaFont *font, const unsigned char *data, size_t size,
                     int height_hint, FontFormat *format, unsigned *warnings)
{
    unsigned dummy_warnings = 0;
    FontFormat dummy_format;
    size_t height, count;

    if (!warnings)
        warnings = &dummy_warnings;
    if (!format)
        format = &dummy_format;
    *warnings = 0;

    if (size >= 4 && data[0] == PSF1_MAGIC0 && data[1] == PSF1_MAGIC1) {
        *format = FONT_FORMAT_PSF1;
        return parse_psf1(font, data, size, warnings);
    }
    if (size >= PSF2_HEADER_SIZE && memcmp(data, psf2_magic, 4) == 0) {
        *format = FONT_FORMAT_PSF2;
        return parse_psf2(font, data, size, warnings);
    }

    /* Raw dump: glyphs stored back to back, one byte per row. */
    *format = FONT_FORMAT_RAW;
    if (valid_height(height_hint) && size >= (size_t)height_hint &&
        size % (size_t)height_hint == 0) {
        height = (size_t)height_hint;
    } else if (size > 0 && size % FONT_GLYPHS == 0) {
        height = size / FONT_GLYPHS;
        if (!valid_height((long)height))
            return FONT_ERR_BAD_HEIGHT;
    } else {
        return FONT_ERR_UNKNOWN_FORMAT;
    }
    count = size / height;
    load_glyphs(font, data, count, height, (int)height);
    if (count > FONT_GLYPHS)
        *warnings |= FONT_WARN_EXTRA_GLYPHS;
    else if (count < FONT_GLYPHS)
        *warnings |= FONT_WARN_MISSING_GLYPHS;
    return FONT_OK;
}

/* Growable text buffer used for C source export. */
typedef struct TextBuf {
    char *data;
    size_t len, cap;
    int failed;
} TextBuf;

static void tb_printf(TextBuf *tb, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (tb->failed)
        return;
    for (;;) {
        va_start(ap, fmt);
        n = vsnprintf(tb->data + tb->len, tb->cap - tb->len, fmt, ap);
        va_end(ap);
        if (n < 0) {
            tb->failed = 1;
            return;
        }
        if ((size_t)n < tb->cap - tb->len) {
            tb->len += (size_t)n;
            return;
        }
        {
            size_t new_cap = tb->cap * 2 + (size_t)n + 1;
            char *p = (char *)realloc(tb->data, new_cap);
            if (!p) {
                tb->failed = 1;
                return;
            }
            tb->data = p;
            tb->cap = new_cap;
        }
    }
}

static void make_identifier(const char *in, char *out, size_t cap)
{
    size_t n = 0;

    /* Identifiers cannot start with a digit (and "_X" names are reserved). */
    if (in && isdigit((unsigned char)*in) && cap > 6) {
        memcpy(out, "font_", 5);
        n = 5;
    }
    for (; in && *in && n + 1 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        out[n++] = (char)((isalnum(c) && c < 0x80) ? c : '_');
    }
    out[n] = '\0';
    if (n == 0)
        snprintf(out, cap, "vga_font");
}

static unsigned char *serialize_c(const VgaFont *font, const char *c_name,
                                  size_t *out_size)
{
    TextBuf tb;
    char name[64], macro[64];
    int ch, y, h = font->height;
    size_t i;

    make_identifier(c_name, name, sizeof(name));
    for (i = 0; i <= strlen(name); i++)
        macro[i] = (char)toupper((unsigned char)name[i]);

    tb.cap = 64 * 1024;
    tb.len = 0;
    tb.failed = 0;
    tb.data = (char *)malloc(tb.cap);
    if (!tb.data)
        return NULL;
    tb.data[0] = '\0';

    tb_printf(&tb,
        "/*\n"
        " * %s: %dx%d VGA font, %d glyphs (%d bytes).\n"
        " * Generated by VGA Font Editor.\n"
        " *\n"
        " * Each glyph is %s_HEIGHT bytes, one byte per row from top to bottom.\n"
        " * The most significant bit of a byte is the leftmost pixel.\n"
        " */\n\n"
        "#define %s_WIDTH  %d\n"
        "#define %s_HEIGHT %d\n"
        "#define %s_GLYPHS %d\n\n"
        "static const unsigned char %s[%s_GLYPHS * %s_HEIGHT] = {\n",
        name, FONT_WIDTH, h, FONT_GLYPHS, FONT_GLYPHS * h, macro,
        macro, FONT_WIDTH, macro, h, macro, FONT_GLYPHS,
        name, macro, macro);

    for (ch = 0; ch < FONT_GLYPHS; ch++) {
        if (ch > 0x20 && ch < 0x7F)
            tb_printf(&tb, "    /* 0x%02X '%c' */", ch, ch);
        else
            tb_printf(&tb, "    /* 0x%02X */", ch);
        for (y = 0; y < h; y++) {
            if (y % 16 == 0)
                tb_printf(&tb, "\n   ");
            tb_printf(&tb, " 0x%02X,", font->rows[ch][y]);
        }
        tb_printf(&tb, "\n");
    }
    tb_printf(&tb, "};\n");

    if (tb.failed) {
        free(tb.data);
        return NULL;
    }
    *out_size = tb.len;
    return (unsigned char *)tb.data;
}

unsigned char *font_serialize(const VgaFont *font, FontFormat format,
                              const char *c_name, size_t *out_size)
{
    size_t header = 0, size, ch;
    size_t h = (size_t)font->height;
    unsigned char *buf;

    if (format == FONT_FORMAT_C_SOURCE)
        return serialize_c(font, c_name, out_size);

    if (format == FONT_FORMAT_PSF1)
        header = 4;
    else if (format == FONT_FORMAT_PSF2)
        header = PSF2_HEADER_SIZE;
    size = header + FONT_GLYPHS * h;

    buf = (unsigned char *)calloc(1, size);
    if (!buf)
        return NULL;

    if (format == FONT_FORMAT_PSF1) {
        buf[0] = PSF1_MAGIC0;
        buf[1] = PSF1_MAGIC1;
        buf[2] = 0;  /* 256 glyphs, no Unicode table */
        buf[3] = (unsigned char)h;
    } else if (format == FONT_FORMAT_PSF2) {
        memcpy(buf, psf2_magic, 4);
        write_le32(buf + 4, 0);                 /* version */
        write_le32(buf + 8, PSF2_HEADER_SIZE);  /* header size */
        write_le32(buf + 12, 0);                /* flags */
        write_le32(buf + 16, FONT_GLYPHS);      /* glyph count */
        write_le32(buf + 20, (unsigned long)h); /* bytes per glyph */
        write_le32(buf + 24, (unsigned long)h); /* height */
        write_le32(buf + 28, FONT_WIDTH);       /* width */
    }
    for (ch = 0; ch < FONT_GLYPHS; ch++)
        memcpy(buf + header + ch * h, font->rows[ch], h);

    *out_size = size;
    return buf;
}

void font_resize(VgaFont *font, int new_height, FontResizeMode mode)
{
    int old_height = font->height;
    int ch, y;

    if (!valid_height(new_height))
        return;
    for (ch = 0; ch < FONT_GLYPHS; ch++) {
        unsigned char src[FONT_MAX_HEIGHT];

        memcpy(src, font->rows[ch], sizeof(src));
        memset(font->rows[ch], 0, sizeof(src));
        for (y = 0; y < new_height; y++) {
            int sy;

            switch (mode) {
            case FONT_RESIZE_CENTER: sy = y - (new_height - old_height) / 2; break;
            case FONT_RESIZE_BOTTOM: sy = y - (new_height - old_height); break;
            case FONT_RESIZE_SCALE:  sy = (2 * y + 1) * old_height / (2 * new_height); break;
            default:                 sy = y; break;
            }
            if (sy >= 0 && sy < old_height)
                font->rows[ch][y] = src[sy];
        }
    }
    font->height = new_height;
}

void glyph_clear(VgaFont *font, int ch)
{
    memset(font->rows[ch], 0, FONT_MAX_HEIGHT);
}

void glyph_invert(VgaFont *font, int ch)
{
    int y;

    for (y = 0; y < font->height; y++)
        font->rows[ch][y] = (unsigned char)~font->rows[ch][y];
}

void glyph_flip_horizontal(VgaFont *font, int ch)
{
    int y, bit;

    for (y = 0; y < font->height; y++) {
        unsigned char in = font->rows[ch][y], out = 0;
        for (bit = 0; bit < 8; bit++)
            if (in & (1u << bit))
                out |= (unsigned char)(0x80u >> bit);
        font->rows[ch][y] = out;
    }
}

void glyph_flip_vertical(VgaFont *font, int ch)
{
    int top = 0, bottom = font->height - 1;

    while (top < bottom) {
        unsigned char t = font->rows[ch][top];
        font->rows[ch][top++] = font->rows[ch][bottom];
        font->rows[ch][bottom--] = t;
    }
}

void glyph_shift(VgaFont *font, int ch, int dx, int dy)
{
    unsigned char src[FONT_MAX_HEIGHT];
    int y;

    memcpy(src, font->rows[ch], sizeof(src));
    for (y = 0; y < font->height; y++) {
        int sy = y - dy;
        unsigned int row = (sy >= 0 && sy < font->height) ? src[sy] : 0;

        if (dx > 0)
            row = dx >= 8 ? 0 : row >> dx;
        else if (dx < 0)
            row = -dx >= 8 ? 0 : row << -dx;
        font->rows[ch][y] = (unsigned char)(row & 0xFF);
    }
}

void glyph_bold(VgaFont *font, int ch)
{
    int y;

    for (y = 0; y < font->height; y++)
        font->rows[ch][y] |= (unsigned char)(font->rows[ch][y] >> 1);
}

void glyph_to_art(const VgaFont *font, int ch, wchar_t *buf, size_t cap)
{
    size_t n = 0;
    int x, y;

    if (cap == 0)
        return;
    for (y = 0; y < font->height; y++) {
        if (n + FONT_WIDTH + 3 > cap)
            break;
        for (x = 0; x < FONT_WIDTH; x++)
            buf[n++] = font_get_pixel(font, ch, x, y) ? L'#' : L'.';
        buf[n++] = L'\r';
        buf[n++] = L'\n';
    }
    buf[n] = L'\0';
}

static int art_pixel(wchar_t c)
{
    switch (c) {
    case L'#': case L'X': case L'x': case L'*': case L'@': case L'1':
    case 0x2588: /* full block */
        return 1;
    case L'.': case L' ': case L'-': case L'_': case L'0':
    case 0x00B7: /* middle dot */
        return 0;
    }
    return -1;
}

int glyph_from_art(const wchar_t *text, unsigned char rows[FONT_MAX_HEIGHT], int *row_count)
{
    int y = 0, x = 0, rows_used = 0, saw_pixel_char = 0;

    memset(rows, 0, FONT_MAX_HEIGHT);
    for (; *text; text++) {
        wchar_t c = *text;
        int p;

        if (c == L'\r')
            continue;
        if (c == L'\n') {
            y++;
            x = 0;
            continue;
        }
        p = art_pixel(c);
        if (p < 0)
            return 0;
        if (c != L' ')
            saw_pixel_char = 1;
        if (y < FONT_MAX_HEIGHT) {
            if (p && x < FONT_WIDTH)
                rows[y] |= (unsigned char)(0x80 >> x);
            if (y + 1 > rows_used)
                rows_used = y + 1;
        }
        x++;
    }
    if (!saw_pixel_char)
        return 0;
    if (row_count)
        *row_count = rows_used;
    return 1;
}

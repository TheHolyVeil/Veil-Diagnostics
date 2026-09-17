#ifndef GFX_H
#define GFX_H
#include "efi_types.h"

UINT32 make_color_gop(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT8 r, UINT8 g, UINT8 b);
void fb_pixel(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int x, int y, UINT32 color);
void fb_fill_rect(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int rx, int ry, int rw, int rh, UINT32 color);
void fb_draw_rect(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int rx, int ry, int rw, int rh, int border, UINT32 color);
void fb_draw_char(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int x, int y, char c, int scale, UINT32 fg, UINT32 bg, int draw_bg);
void fb_draw_text(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int x, int y, const char *text, int scale, UINT32 fg, UINT32 bg, int draw_bg);
void fb_draw_cursor(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int cx, int cy, UINT32 white_col, UINT32 black_col, BOOLEAN is_clicked);

#endif /* GFX_H */

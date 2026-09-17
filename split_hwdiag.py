#!/usr/bin/env python3
import re

with open("hwdiag.c") as f:
    lines = f.readlines()  # 0-indexed list; line N is lines[N-1]

def rng(a, b):  # inclusive 1-indexed -> joined string
    return "".join(lines[a-1:b])

# ---- exact slices (1-indexed, inclusive), per grep/sed audit ----
HEADER      = rng(1, 8) + rng(11, 457)          # skip lines 9-10 (_fltused + its comment)
RUNTIME_GL  = rng(458, 478)
MIN_RUNTIME = rng(479, 492)
CONSOLE     = rng(493, 637)
FONT_DATA   = rng(638, 740)
GFX_PRIM    = rng(741, 878)
PORTIO      = rng(879, 1110)
CPUID       = rng(1111, 1225)
SECTION_FN  = rng(1226, 1229)
SET_TEXTMODE= rng(1230, 1252)
# 1253-1261 forward-decl hack: dropped entirely, no longer needed
DONUT       = rng(1262, 1626)
TEXT_PAGER  = rng(1627, 1907)
HWINFO      = rng(1908, 2128)
UI_MENU     = rng(2129, 2557)
ENTRY_PT    = rng(2558, 2602)

# Extract read_key_nonblocking (1665-1671) out of TEXT_PAGER, moving it to core.c
RKN = rng(1665, 1671)
TEXT_PAGER = TEXT_PAGER.replace(RKN, "")

# Extract point_in_rect (2141-2143) out of UI_MENU, moving it to core.c
PIR = rng(2141, 2143)
UI_MENU = UI_MENU.replace(PIR, "")

# Strip the SCAN_* defines out of TEXT_PAGER (moving to core.h; keep PAGER_MAX_LINES local)
SCAN_BLOCK = """#define SCAN_UP        0x01
#define SCAN_DOWN      0x02
#define SCAN_RIGHT     0x03
#define SCAN_LEFT      0x04
#define SCAN_PAGE_UP   0x09
#define SCAN_PAGE_DOWN 0x0A
#define SCAN_ESC       0x17

"""
assert SCAN_BLOCK in TEXT_PAGER, "SCAN block not found verbatim"
TEXT_PAGER = TEXT_PAGER.replace(SCAN_BLOCK, "")

def unstatic(text, names):
    for n in names:
        text = re.sub(r'\bstatic (\w[\w\s\*]*\b' + re.escape(n) + r'\s*\()', r'\1', text, count=1)
    return text

def unstatic_var(text, names):
    for n in names:
        text = re.sub(r'^static (.*\b' + re.escape(n) + r'\b.*;)$', r'\1', text, count=1, flags=re.MULTILINE)
    return text

# ---- core.c: globals + runtime + console + pointer/input polling ----
core_c = RUNTIME_GL + "\n" + MIN_RUNTIME + "\n" + CONSOLE + "\n" + PORTIO
core_c = unstatic_var(core_c, [
    "ST;", "g_simple_pointers\\[MAX_POINTER_HANDLES\\];", "g_num_simple_pointers = 0;",
    "g_abs_pointers\\[MAX_POINTER_HANDLES\\];", "g_num_abs_pointers = 0;",
    "g_mouse_packets_count = 0;", "g_last_dx = 0, g_last_dy = 0;",
    "g_last_abs_status = EFI_NOT_FOUND;",
    "g_abs_min_x = 0, g_abs_max_x = 0, g_abs_min_y = 0, g_abs_max_y = 0;",
    "g_abs_cur_x = 0, g_abs_cur_y = 0;",
    "outbuf\\[OUTBUF_MAX\\];", "outbuf_len = 0;",
])
core_c = unstatic(core_c, ["wprint_raw", "uprintf_str", "printa", "print_rev",
                           "init_pointer_protocols", "poll_pointer_inputs"])
core_c += "\n" + RKN + "\n" + PIR
core_c = unstatic(core_c, ["read_key_nonblocking", "point_in_rect"])
core_c = '#include "efi_types.h"\n#include "core.h"\n\n' + core_c

# ---- gfx.c ----
gfx_c = FONT_DATA + "\n" + GFX_PRIM
gfx_c = unstatic(gfx_c, ["make_color_gop", "fb_pixel", "fb_fill_rect", "fb_draw_rect",
                          "fb_draw_char", "fb_draw_text", "fb_draw_cursor"])
gfx_c = '#include "efi_types.h"\n#include "core.h"\n#include "gfx.h"\n\n' + gfx_c

# ---- cpu_hwinfo.c ----
cpu_hwinfo_c = CPUID + "\n" + SECTION_FN + "\n" + HWINFO
cpu_hwinfo_c = unstatic(cpu_hwinfo_c, ["gather_hardware_info"])
cpu_hwinfo_c = '#include "efi_types.h"\n#include "core.h"\n#include "gfx.h"\n#include "cpu_hwinfo.h"\n\n' + cpu_hwinfo_c

# ---- donut.c ----
donut_c = DONUT
donut_c = unstatic(donut_c, ["easter_egg_donut"])
donut_c = '#include "efi_types.h"\n#include "core.h"\n#include "gfx.h"\n#include "donut.h"\n\n' + donut_c

# ---- ui.c ----
ui_c = TEXT_PAGER + "\n" + UI_MENU
ui_c = unstatic(ui_c, ["run_pager", "run_graphical_home_menu"])
ui_c = ('#include "efi_types.h"\n#include "core.h"\n#include "gfx.h"\n'
        '#include "donut.h"\n#include "cpu_hwinfo.h"\n#include "ui.h"\n\n' + ui_c)

# ---- main.c ----
main_c = ('#include "efi_types.h"\n#include "core.h"\n#include "gfx.h"\n'
          '#include "cpu_hwinfo.h"\n#include "donut.h"\n#include "ui.h"\n\n'
          '/* ---- CRT symbol required for floating-point ----------------------------- */\n'
          'int _fltused = 1;\n\n' + SET_TEXTMODE + "\n" + ENTRY_PT)

# ---- headers ----
efi_types_h = "#ifndef EFI_TYPES_H\n#define EFI_TYPES_H\n\n" + HEADER + "\n#endif /* EFI_TYPES_H */\n"

core_h = """#ifndef CORE_H
#define CORE_H
#include "efi_types.h"

#define MAX_POINTER_HANDLES 16
#define SCAN_UP        0x01
#define SCAN_DOWN      0x02
#define SCAN_RIGHT     0x03
#define SCAN_LEFT      0x04
#define SCAN_PAGE_UP   0x09
#define SCAN_PAGE_DOWN 0x0A
#define SCAN_ESC       0x17

extern EFI_SYSTEM_TABLE *ST;
extern EFI_SIMPLE_POINTER_PROTOCOL *g_simple_pointers[MAX_POINTER_HANDLES];
extern UINTN g_num_simple_pointers;
extern EFI_ABSOLUTE_POINTER_PROTOCOL *g_abs_pointers[MAX_POINTER_HANDLES];
extern UINTN g_num_abs_pointers;
extern UINT64 g_mouse_packets_count;
extern INT32 g_last_dx, g_last_dy;
extern EFI_STATUS g_last_abs_status;
extern UINT64 g_abs_min_x, g_abs_max_x, g_abs_min_y, g_abs_max_y;
extern UINT64 g_abs_cur_x, g_abs_cur_y;

#define OUTBUF_MAX 24000
extern CHAR16 outbuf[OUTBUF_MAX];
extern UINTN  outbuf_len;

static inline void outb(UINT16 port, UINT8 val) {
  __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline UINT8 inb(UINT16 port) {
  UINT8 ret;
  __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

VOID* memset(VOID *dst, int c, UINTN n);
VOID* memcpy(VOID *dst, const VOID *src, UINTN n);
void wprint_raw(const CHAR16 *s);
void uprintf(const char *fmt, ...);
void uprintf_str(char *buf, UINTN buf_size, const char *fmt, ...);
void printa(const char *s);
void print_rev(UINT32 rev);
void init_pointer_protocols(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES *bs);
void poll_pointer_inputs(UINT32 screen_w, UINT32 screen_h, int *cursor_x, int *cursor_y, BOOLEAN *curr_left_btn);
EFI_INPUT_KEY read_key_nonblocking(void);
int point_in_rect(int px, int py, int rx, int ry, int rw, int rh);
void serial_write_str(const char *s);

#endif /* CORE_H */
"""

gfx_h = """#ifndef GFX_H
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
"""

cpu_hwinfo_h = """#ifndef CPU_HWINFO_H
#define CPU_HWINFO_H
#include "efi_types.h"

void gather_hardware_info(EFI_HANDLE ImageHandle);

#endif /* CPU_HWINFO_H */
"""

donut_h = """#ifndef DONUT_H
#define DONUT_H
#include "efi_types.h"

int easter_egg_donut(EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 *fb, UINT32 *back_buf,
  UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn);

#endif /* DONUT_H */
"""

ui_h = """#ifndef UI_H
#define UI_H
#include "efi_types.h"

int run_pager(EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 *fb, UINT32 *back_buf,
  UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn);
void run_graphical_home_menu(EFI_HANDLE ImageHandle, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

#endif /* UI_H */
"""

files = {
    "efi_types.h": efi_types_h, "core.h": core_h, "core.c": core_c,
    "gfx.h": gfx_h, "gfx.c": gfx_c,
    "cpu_hwinfo.h": cpu_hwinfo_h, "cpu_hwinfo.c": cpu_hwinfo_c,
    "donut.h": donut_h, "donut.c": donut_c,
    "ui.h": ui_h, "ui.c": ui_c,
    "main.c": main_c,
}
import os
os.makedirs("src", exist_ok=True)
for name, content in files.items():
    with open(os.path.join("src", name), "w") as f:
        f.write(content)
print("wrote", len(files), "files to src/")
for name in files:
    print(" ", name, len(files[name]), "bytes")

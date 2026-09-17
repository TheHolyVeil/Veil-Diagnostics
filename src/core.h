#ifndef CORE_H
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
void serial_init(void);
void init_pointer_protocols(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES *bs);
void poll_pointer_inputs(UINT32 screen_w, UINT32 screen_h, int *cursor_x, int *cursor_y, BOOLEAN *curr_left_btn);
EFI_INPUT_KEY read_key_nonblocking(void);
int point_in_rect(int px, int py, int rx, int ry, int rw, int rh);
void serial_write_str(const char *s);

#endif /* CORE_H */

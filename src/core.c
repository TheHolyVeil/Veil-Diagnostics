#include "efi_types.h"
#include "core.h"

/* ========================================================================= */
/* Runtime globals                                                           */
/* ========================================================================= */
static EFI_SYSTEM_TABLE *ST;

#define MAX_POINTER_HANDLES 16
static EFI_SIMPLE_POINTER_PROTOCOL *g_simple_pointers[MAX_POINTER_HANDLES];
static UINTN g_num_simple_pointers = 0;

static EFI_ABSOLUTE_POINTER_PROTOCOL *g_abs_pointers[MAX_POINTER_HANDLES];
static UINTN g_num_abs_pointers = 0;

static UINT64 g_mouse_packets_count = 0;
static INT32 g_last_dx = 0, g_last_dy = 0;

/* Raw absolute-pointer diagnostics, captured every poll of device[0] so we
 * can see on-screen exactly what the firmware is actually reporting. */
static EFI_STATUS g_last_abs_status = EFI_NOT_FOUND;
static UINT64 g_abs_min_x = 0, g_abs_max_x = 0, g_abs_min_y = 0, g_abs_max_y = 0;
static UINT64 g_abs_cur_x = 0, g_abs_cur_y = 0;


/* ========================================================================= */
/* Minimal runtime services (no libc)                                        */
/* ========================================================================= */
VOID* memset(VOID *dst, int c, UINTN n) {
  UINT8 *p = (UINT8*)dst;
  while (n--) *p++ = (UINT8)c;
  return dst;
}
VOID* memcpy(VOID *dst, const VOID *src, UINTN n) {
  UINT8 *d = (UINT8*)dst; const UINT8 *s = (const UINT8*)src;
  while (n--) *d++ = *s++;
  return dst;
}


/* ========================================================================= */
/* Console printing & Pager                                                  */
/* ========================================================================= */
static CHAR16 wbuf[1024];

#define OUTBUF_MAX 24000
static CHAR16 outbuf[OUTBUF_MAX];
static UINTN  outbuf_len = 0;

void wprint_raw(const CHAR16 *s) {
  if (ST->ConOut) ST->ConOut->OutputString(ST->ConOut, (CHAR16*)s);
}

static void wprint(const CHAR16 *s) {
  UINTN i = 0;
  while (s[i] && outbuf_len < OUTBUF_MAX - 1) {
    outbuf[outbuf_len++] = s[i++];
  }
}

static void putc16(CHAR16 c) {
  if (c == L'\n') {
    wbuf[0] = L'\r';
    wbuf[1] = 0;
    wprint(wbuf);
  }
  wbuf[0] = c;
  wbuf[1] = 0;
  wprint(wbuf);
}

void printa(const char *s) {
  UINTN i = 0, o = 0;
  while (s[i] && o < 1000) {
    if (s[i] == '\n') {
      wbuf[o++] = L'\r';
      if (o < 1000) wbuf[o++] = L'\n';
    } else {
      wbuf[o++] = (CHAR16)(UINT8)s[i];
    }
    i++;
  }
  wbuf[o] = 0;
  wprint(wbuf);
}

static void print_u64(UINT64 v, int base, int width) {
  char tmp[24];
  int i = 0, j;
  const char *hexd = "0123456789abcdef";
  if (v == 0) tmp[i++] = '0';
  while (v) {
    tmp[i++] = hexd[v % (UINT64)base];
    v /= (UINT64)base;
  }
  while (i < width) tmp[i++] = '0';
  for (j = i - 1; j >= 0; j--) putc16((CHAR16)(UINT8)tmp[j]);
}

static UINTN strlen_a(const char *s) {
  UINTN n = 0;
  while (s[n]) n++;
  return n;
}

void uprintf_str(char *buf, UINTN buf_size, const char *fmt, ...) {
  UINTN pos = 0;
  __builtin_va_list ap;
  __builtin_va_start(ap, fmt);
  while (*fmt && pos < buf_size - 1) {
    if (*fmt != '%') { buf[pos++] = *fmt++; continue; }
    fmt++;
    if (*fmt == 'u') {
      UINT64 v = __builtin_va_arg(ap, UINT64);
      char tmp[24]; int i = 0, j;
      if (v == 0) tmp[i++] = '0';
      while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
      for (j = i - 1; j >= 0 && pos < buf_size - 1; j--) buf[pos++] = tmp[j];
    } else if (*fmt == 'd' || *fmt == 'i') {
      INT64 v = __builtin_va_arg(ap, INT64);
      if (v < 0) { buf[pos++] = '-'; v = -v; }
      char tmp[24]; int i = 0, j;
      if (v == 0) tmp[i++] = '0';
      while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
      for (j = i - 1; j >= 0 && pos < buf_size - 1; j--) buf[pos++] = tmp[j];
    }
    if (*fmt) fmt++;
  }
  buf[pos] = 0;
  __builtin_va_end(ap);
}

static void __attribute__((used)) uprintf(const char *fmt, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, fmt);
  while (*fmt) {
    if (*fmt != '%') { putc16((CHAR16)(UINT8)*fmt++); continue; }
    fmt++;

    int left_align = 0;
    int width = 0;
    if (*fmt == '-') { left_align = 1; fmt++; }
    while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

    switch (*fmt) {
      case 's': {
        const char *s = __builtin_va_arg(ap, const char*);
        UINTN len = s ? strlen_a(s) : 0;
        int pad;
        if (!left_align) for (pad = (int)len; pad < width; pad++) putc16(L' ');
        if (s) printa(s);
        if (left_align) for (pad = (int)len; pad < width; pad++) putc16(L' ');
        break;
      }
      case 'S': { const CHAR16 *s = __builtin_va_arg(ap, const CHAR16*); if (s) wprint(s); break; }
      case 'u':
      case 'x':
      case 'X': {
        UINT64 v = __builtin_va_arg(ap, UINT64);
        int base = (*fmt == 'u') ? 10 : 16;
        int fixed_width = (*fmt == 'X') ? 8 : 0;
        if (width > 0 && !left_align) {
          UINT64 tmp = v;
          int digits = (tmp == 0) ? 1 : 0;
          while (tmp) { digits++; tmp /= (UINT64)base; }
          for (; digits < width; digits++) putc16(L' ');
        }
        print_u64(v, base, fixed_width);
        break;
      }
      case 'c': putc16((CHAR16)(UINT8)__builtin_va_arg(ap, int)); break;
      case '%': putc16(L'%'); break;
      default:  putc16((CHAR16)(UINT8)*fmt); break;
    }
    if (*fmt) fmt++;
  }
  __builtin_va_end(ap);
}

void print_rev(UINT32 rev) {
  UINT32 major = rev >> 16;
  UINT32 minor = ((rev & 0xFFFF) >> 12) * 100 + (((rev & 0xFFFF) >> 8) & 0xF) * 10 + ((rev & 0xFFFF) & 0xF);
  uprintf("%u.%u", (UINT64)major, (UINT64)minor);
}


/* ========================================================================= */
/* Port I/O Helpers & UEFI Driver Protocol Discovery                        */
/* ========================================================================= */
/* outb/inb now live as static inline in core.h so every TU that needs port
 * I/O (donut.c's PC speaker included) gets them without duplicate symbols. */

/* Minimal COM1 (0x3F8) serial logger — bypasses the framebuffer entirely so
 * pointer diagnostics can be read straight from QEMU's stdio/log instead of
 * off a screenshot. Freestanding, no dependency on anything else in this
 * file besides outb/inb above. */
void serial_init(void) {
  outb(0x3F9, 0x00);
  outb(0x3FB, 0x80);
  outb(0x3F8, 0x01);
  outb(0x3F9, 0x00);
  outb(0x3FB, 0x03);
  outb(0x3FA, 0xC7);
  outb(0x3FC, 0x0B);
}
void serial_write_str(const char *s) {
  while (*s) {
    if (*s == '\n') {
      while (!(inb(0x3FD) & 0x20)) { }
      outb(0x3F8, '\r');
    }
    while (!(inb(0x3FD) & 0x20)) { }
    outb(0x3F8, (UINT8)*s++);
  }
}

static void connect_all_controllers(EFI_BOOT_SERVICES *bs) {
  if (!bs || !bs->LocateHandleBuffer || !bs->ConnectController) return;

  UINTN count = 0;
  EFI_HANDLE *handles = NULL;
  #define EFI_ALL_HANDLES 0
  if (!EFI_ERROR(bs->LocateHandleBuffer(EFI_ALL_HANDLES, NULL, NULL, &count, &handles)) && handles) {
    UINTN i;
    for (i = 0; i < count; i++) {
      bs->ConnectController(handles[i], NULL, NULL, TRUE);
    }
    bs->FreePool(handles);
  }
}

static void add_simple_pointer_if_new(EFI_SIMPLE_POINTER_PROTOCOL *sp) {
  if (!sp) return;
  UINTN i;
  for (i = 0; i < g_num_simple_pointers; i++) {
    if (g_simple_pointers[i] == sp) return;
  }
  if (g_num_simple_pointers < MAX_POINTER_HANDLES) {
    g_simple_pointers[g_num_simple_pointers++] = sp;
  }
}

static void add_abs_pointer_if_new(EFI_ABSOLUTE_POINTER_PROTOCOL *ap) {
  if (!ap) return;
  UINTN i;
  for (i = 0; i < g_num_abs_pointers; i++) {
    if (g_abs_pointers[i] == ap) return;
  }
  if (g_num_abs_pointers < MAX_POINTER_HANDLES) {
    g_abs_pointers[g_num_abs_pointers++] = ap;
  }
}

void init_pointer_protocols(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES *bs) {
  g_num_simple_pointers = 0;
  g_num_abs_pointers = 0;

  if (!bs) return;

  connect_all_controllers(bs);

  if (bs->LocateProtocol) {
    EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL;
    if (!EFI_ERROR(bs->LocateProtocol((EFI_GUID*)&gEfiSimplePointerProtocolGuid, NULL, (VOID**)&sp)) && sp) {
      add_simple_pointer_if_new(sp);
    }
    EFI_ABSOLUTE_POINTER_PROTOCOL *ap = NULL;
    if (!EFI_ERROR(bs->LocateProtocol((EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, NULL, (VOID**)&ap)) && ap) {
      add_abs_pointer_if_new(ap);
    }
  }

  if (ST->ConsoleInHandle && bs->HandleProtocol) {
    EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL;
    if (!EFI_ERROR(bs->HandleProtocol(ST->ConsoleInHandle, (EFI_GUID*)&gEfiSimplePointerProtocolGuid, (VOID**)&sp)) && sp) {
      add_simple_pointer_if_new(sp);
    }
    EFI_ABSOLUTE_POINTER_PROTOCOL *ap = NULL;
    if (!EFI_ERROR(bs->HandleProtocol(ST->ConsoleInHandle, (EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, (VOID**)&ap)) && ap) {
      add_abs_pointer_if_new(ap);
    }
  }

  if (bs->LocateHandleBuffer) {
    UINTN count = 0;
    EFI_HANDLE *handles = NULL;

    if (!EFI_ERROR(bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, (EFI_GUID*)&gEfiSimplePointerProtocolGuid, NULL, &count, &handles)) && handles) {
      UINTN i;
      for (i = 0; i < count; i++) {
        EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL;
        if (bs->OpenProtocol && !EFI_ERROR(bs->OpenProtocol(handles[i], (EFI_GUID*)&gEfiSimplePointerProtocolGuid, (VOID**)&sp, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) && sp) {
          add_simple_pointer_if_new(sp);
        } else if (bs->HandleProtocol && !EFI_ERROR(bs->HandleProtocol(handles[i], (EFI_GUID*)&gEfiSimplePointerProtocolGuid, (VOID**)&sp)) && sp) {
          add_simple_pointer_if_new(sp);
        }
      }
      bs->FreePool(handles);
    }

    count = 0; handles = NULL;
    if (!EFI_ERROR(bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, (EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, NULL, &count, &handles)) && handles) {
      UINTN i;
      for (i = 0; i < count; i++) {
        EFI_ABSOLUTE_POINTER_PROTOCOL *ap = NULL;
        if (bs->OpenProtocol && !EFI_ERROR(bs->OpenProtocol(handles[i], (EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, (VOID**)&ap, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) && ap) {
          add_abs_pointer_if_new(ap);
        } else if (bs->HandleProtocol && !EFI_ERROR(bs->HandleProtocol(handles[i], (EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, (VOID**)&ap)) && ap) {
          add_abs_pointer_if_new(ap);
        }
      }
      bs->FreePool(handles);
    }
  }

  /* Flush/reset pointer buffer states once on init */
  UINTN i;
  for (i = 0; i < g_num_simple_pointers; i++) {
    if (g_simple_pointers[i] && g_simple_pointers[i]->Reset) {
      g_simple_pointers[i]->Reset(g_simple_pointers[i], FALSE);
    }
  }
  for (i = 0; i < g_num_abs_pointers; i++) {
    if (g_abs_pointers[i] && g_abs_pointers[i]->Reset) {
      g_abs_pointers[i]->Reset(g_abs_pointers[i], FALSE);
    }
  }
}

void poll_pointer_inputs(UINT32 screen_w, UINT32 screen_h, int *cursor_x, int *cursor_y, BOOLEAN *curr_left_btn) {
  UINTN i;
  BOOLEAN got_relative_packet = FALSE;

  for (i = 0; i < g_num_simple_pointers; i++) {
    EFI_SIMPLE_POINTER_PROTOCOL *sp = g_simple_pointers[i];
    if (!sp || !sp->GetState) continue;

    EFI_SIMPLE_POINTER_STATE state = {0};
    if (sp->GetState(sp, &state) == EFI_SUCCESS) {
      g_mouse_packets_count++;
      got_relative_packet = TRUE;

      *cursor_x += state.RelativeMovementX;
      *cursor_y += state.RelativeMovementY;
      g_last_dx = state.RelativeMovementX;
      g_last_dy = state.RelativeMovementY;

      if (state.LeftButton) *curr_left_btn = TRUE;
    }
  }

  /* Absolute Pointer devices (e.g. a laptop touchpad's UEFI driver) are only
   * trusted when no relative device produced a packet this poll. On real
   * hardware it's common to have BOTH a Simple Pointer handle (real mouse)
   * and an idle/phantom Absolute Pointer handle. The old code applied the
   * absolute reading unconditionally *after* the relative update, so a
   * stale default packet (often reporting the device's min corner, i.e.
   * bottom-left, with a garbage ActiveButtons bit) stomped the cursor and
   * click state back every single frame — that's the "stuck" behavior. */
  if (!got_relative_packet) {
    for (i = 0; i < g_num_abs_pointers; i++) {
      EFI_ABSOLUTE_POINTER_PROTOCOL *ap = g_abs_pointers[i];
      if (!ap || !ap->GetState) continue;

      EFI_ABSOLUTE_POINTER_STATE astate = {0};
      EFI_STATUS abs_status = ap->GetState(ap, &astate);
      if (i == 0) {
        g_last_abs_status = abs_status;
        if (ap->Mode) {
          g_abs_min_x = ap->Mode->AbsoluteMinX; g_abs_max_x = ap->Mode->AbsoluteMaxX;
          g_abs_min_y = ap->Mode->AbsoluteMinY; g_abs_max_y = ap->Mode->AbsoluteMaxY;
        }
        g_abs_cur_x = astate.CurrentX; g_abs_cur_y = astate.CurrentY;
      }
      if (abs_status == EFI_SUCCESS) {
        g_mouse_packets_count++;

        if (ap->Mode && ap->Mode->AbsoluteMaxX > ap->Mode->AbsoluteMinX &&
            ap->Mode->AbsoluteMaxY > ap->Mode->AbsoluteMinY) {
          UINT64 spanX = ap->Mode->AbsoluteMaxX - ap->Mode->AbsoluteMinX;
          UINT64 spanY = ap->Mode->AbsoluteMaxY - ap->Mode->AbsoluteMinY;
          UINT64 relX  = astate.CurrentX - ap->Mode->AbsoluteMinX;
          UINT64 relY  = astate.CurrentY - ap->Mode->AbsoluteMinY;
          int new_x = (int)((relX * screen_w) / spanX);
          int new_y = (int)((relY * screen_h) / spanY);
          /* Absolute devices report position, not delta — synthesize dX/dY
           * from the change since last poll so the HUD isn't dead-zero. */
          g_last_dx = new_x - *cursor_x;
          g_last_dy = new_y - *cursor_y;
          *cursor_x = new_x;
          *cursor_y = new_y;
        }
        if (astate.ActiveButtons & 1) *curr_left_btn = TRUE;
      }
    }
  }

  {
    static UINT64 poll_n = 0;
    poll_n++;
    if ((poll_n % 30) == 0) {
      char dbg[220];
      uprintf_str(dbg, sizeof(dbg),
        "poll#%u simple=%u abs=%u pkts=%u relpkt=%u cx=%d cy=%d dx=%d dy=%d absSt=%u absCur=%u,%u\n",
        poll_n, (UINT64)g_num_simple_pointers, (UINT64)g_num_abs_pointers, g_mouse_packets_count,
        (UINT64)got_relative_packet, (INT64)*cursor_x, (INT64)*cursor_y, (INT64)g_last_dx, (INT64)g_last_dy,
        (UINT64)g_last_abs_status, g_abs_cur_x, g_abs_cur_y);
      serial_write_str(dbg);
    }
  }
}


EFI_INPUT_KEY read_key_nonblocking(void) {
  EFI_INPUT_KEY key = { 0, 0 };
  if (ST->ConIn) {
    ST->ConIn->ReadKeyStroke(ST->ConIn, &key);
  }
  return key;
}

int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
  return (px >= rx && px < rx + rw && py >= ry && py < ry + rh);
}

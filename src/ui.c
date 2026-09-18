#include "efi_types.h"
#include "core.h"
#include "gfx.h"
#include "donut.h"
#include "cpu_hwinfo.h"
#include "ram_test.h"
#include "interactive_tests.h"
#include "ui.h"

/* ========================================================================= */
/* Text Output Pager                                                         */
/* ========================================================================= */
#define PAGER_MAX_LINES 700

static UINTN line_starts[PAGER_MAX_LINES];

static void print_uint_raw(UINT64 v) {
  CHAR16 tmp[24], out[24];
  int i = 0, j;
  if (v == 0) tmp[i++] = L'0';
  while (v) { tmp[i++] = (CHAR16)(L'0' + (v % 10)); v /= 10; }
  for (j = 0; j < i; j++) out[j] = tmp[i - 1 - j];
  out[i] = 0;
  wprint_raw(out);
}

static void render_line(UINTN start, UINTN end) {
  CHAR16 line[512];
  UINTN n = 0, k;
  for (k = start; k < end && n < 509; k++) {
    if (outbuf[k] == L'\r' || outbuf[k] == L'\n') continue;
    line[n++] = outbuf[k];
  }
  line[n++] = L'\r';
  line[n++] = L'\n';
  line[n] = 0;
  wprint_raw(line);
}


int run_pager(EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 *fb, UINT32 *back_buf,
  UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn)
{
  UINTN line_count = 0, i;

  /* Build line index from outbuf */
  line_starts[line_count++] = 0;
  for (i = 0; i < outbuf_len && line_count < PAGER_MAX_LINES; i++) {
    if (outbuf[i] == L'\n' && i + 1 < outbuf_len) {
      line_starts[line_count++] = i + 1;
    }
  }

  if (gop && fb) {
    /* ------------------------------------------------------------------ */
    /* Graphical pager                                                      */
    /* ------------------------------------------------------------------ */
    UINT32 col_bg        = make_color_gop(gop, 15,  23,  42);  /* #0F172A */
    UINT32 col_topbar    = make_color_gop(gop, 15,  23,  42);
    UINT32 col_border    = make_color_gop(gop, 51,  65,  85);  /* #334155 */
    UINT32 col_white     = make_color_gop(gop, 255, 255, 255);
    UINT32 col_gray      = make_color_gop(gop, 148, 163, 184); /* #94A3B8 */
    UINT32 col_text      = make_color_gop(gop, 226, 232, 240); /* #E2E8F0 */
    UINT32 col_card      = make_color_gop(gop, 30,  41,  59);  /* #1E293B */
    UINT32 col_btn_hov   = make_color_gop(gop, 59, 130, 246);  /* #3B82F6 */
    UINT32 col_exit_norm = make_color_gop(gop, 153, 27,  27);  /* #991B1B */
    UINT32 col_exit_hov  = make_color_gop(gop, 220, 38,  38);  /* #DC2626 */
    UINT32 col_scrolltrk = make_color_gop(gop, 30,  41,  59);  /* #1E293B */
    UINT32 col_scrollthm = make_color_gop(gop, 148, 163, 184); /* #94A3B8 */
    UINT32 col_black     = make_color_gop(gop, 0,    0,   0);

    int top_back_x = 15, top_back_y = 10, top_back_w = 90, top_back_h = 32;
    int top_exit_x = (int)screen_w - 110, top_exit_y = 10, top_exit_w = 95, top_exit_h = 32;

    /* Text area geometry */
    int text_y0 = 52;           /* below header+border */
    int text_y1 = (int)screen_h - 38; /* above status bar */
    int text_area_h = text_y1 - text_y0;
    if (text_area_h < 18) text_area_h = 18;
    int line_h = 18;            /* 16px font + 2px gap */
    int visible_lines = text_area_h / line_h;
    if (visible_lines < 1) visible_lines = 1;
    int left_margin = 30;
    int scroll_x = (int)screen_w - 14; /* scrollbar left edge */
    int scroll_w = 12;
    UINTN max_top = (line_count > (UINTN)visible_lines) ? (line_count - (UINTN)visible_lines) : 0;
    UINTN top = 0;

    /* Flush key buffer */
    if (ST->ConIn) {
      EFI_INPUT_KEY dummy;
      while (!EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &dummy)));
    }

    for (;;) {
      UINT32 *draw_fb = back_buf ? back_buf : fb;
      BOOLEAN curr_left_btn = FALSE;

      poll_pointer_inputs(screen_w, screen_h, cursor_x, cursor_y, &curr_left_btn);
      BOOLEAN click_event = (curr_left_btn && !(*prev_left_btn));
      *prev_left_btn = curr_left_btn;

      /* Keyboard */
      EFI_INPUT_KEY key = read_key_nonblocking();
      if (key.ScanCode == SCAN_UP) {
        if (top > 0) top--;
      } else if (key.ScanCode == SCAN_DOWN) {
        if (top < max_top) top++;
      } else if (key.ScanCode == SCAN_PAGE_UP) {
        top = (top > (UINTN)visible_lines) ? top - (UINTN)visible_lines : 0;
      } else if (key.ScanCode == SCAN_PAGE_DOWN) {
        top = ((top + (UINTN)visible_lines) < max_top) ? top + (UINTN)visible_lines : max_top;
      } else if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27 ||
                 key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n' ||
                 key.UnicodeChar == L'q'  || key.UnicodeChar == L'Q'  ||
                 key.UnicodeChar == L' '  || key.UnicodeChar == L'b'  || key.UnicodeChar == L'B') {
        return 0;
      }

      int hov_back = point_in_rect(*cursor_x, *cursor_y, top_back_x, top_back_y, top_back_w, top_back_h);
      int hov_exit = point_in_rect(*cursor_x, *cursor_y, top_exit_x, top_exit_y, top_exit_w, top_exit_h);

      if (click_event) {
        if (hov_back) return 0;
        if (hov_exit) return 1;
      }

      /* --- Draw frame --- */

      /* Background */
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col_bg);

      /* Header bar */
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, 50, col_topbar);
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 50, screen_w, 2, col_border);
      fb_draw_text(draw_fb, stride, screen_w, screen_h, 120, 14, "HARDWARE INFORMATION", 2, col_white, 0, 0);

      /* Back button */
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h,
                   hov_back ? col_btn_hov : col_card);
      fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h, 2, col_border);
      fb_draw_text(draw_fb, stride, screen_w, screen_h, top_back_x + 14, top_back_y + 8, "< Back", 1, col_white, 0, 0);

      /* Exit button */
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h,
                   hov_exit ? col_exit_hov : col_exit_norm);
      fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h, 2, col_white);
      fb_draw_text(draw_fb, stride, screen_w, screen_h, top_exit_x + 18, top_exit_y + 8, "[X] Exit", 1, col_white, 0, 0);

      /* Scrollable text area */
      {
        UINTN shown_lines = 0;
        UINTN li;
        for (li = top; li < line_count && (int)shown_lines < visible_lines; li++, shown_lines++) {
          UINTN ls = line_starts[li];
          UINTN le = (li + 1 < line_count) ? line_starts[li + 1] : outbuf_len;
          /* Convert CHAR16 line to char for fb_draw_text */
          char line_buf[512];
          UINTN n = 0, k;
          for (k = ls; k < le && n < 510; k++) {
            CHAR16 wc = outbuf[k];
            if (wc == L'\r' || wc == L'\n') continue;
            line_buf[n++] = (wc >= 32 && wc < 127) ? (char)(UINT8)wc : ' ';
          }
          line_buf[n] = '\0';
          int py = text_y0 + (int)shown_lines * line_h;
          fb_draw_text(draw_fb, stride, screen_w, screen_h, left_margin, py, line_buf, 1, col_text, col_bg, 0);
        }
      }

      /* Scrollbar track */
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, scroll_x, text_y0, scroll_w, text_area_h, col_scrolltrk);
      /* Scrollbar thumb */
      if (line_count > 0) {
        int thumb_h = (int)((UINT64)text_area_h * (UINT64)visible_lines / line_count);
        if (thumb_h < 8) thumb_h = 8;
        if (thumb_h > text_area_h) thumb_h = text_area_h;
        int thumb_y = text_y0 + (int)((UINT64)(text_area_h - thumb_h) * top / (line_count > 1 ? line_count - 1 : 1));
        fb_fill_rect(draw_fb, stride, screen_w, screen_h, scroll_x, thumb_y, scroll_w, thumb_h, col_scrollthm);
      }

      /* Bottom status bar */
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, (int)screen_h - 38, screen_w, 38, col_topbar);
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, (int)screen_h - 39, screen_w, 1, col_border);
      {
        UINTN shown_end = top + (UINTN)visible_lines;
        if (shown_end > line_count) shown_end = line_count;
        char status[220];
        uprintf_str(status, sizeof(status),
          "Line %u-%u of %u | Up/Dn Arrow: scroll | PgUp/PgDn: page | Esc/Click: Back",
          (UINT64)(top + 1), (UINT64)shown_end, (UINT64)line_count);
        fb_draw_text(draw_fb, stride, screen_w, screen_h, 20, (int)screen_h - 28, status, 1, col_gray, 0, 0);
      }

      /* Mouse cursor */
      fb_draw_cursor(draw_fb, stride, screen_w, screen_h, *cursor_x, *cursor_y, col_white, col_black, curr_left_btn);

      /* Blit back buffer */
      if (back_buf) {
        memcpy((void*)fb, (const void*)back_buf, (UINTN)screen_h * stride * sizeof(UINT32));
      }

      if (bs && bs->Stall) bs->Stall(16000);
    }
    return 0;

  } else {
    /* ------------------------------------------------------------------ */
    /* Text / fallback pager (original ConOut implementation)              */
    /* ------------------------------------------------------------------ */
    UINTN cols = 0, rows = 0, top, max_top, visible_rows, shown;

    if (!ST->ConOut || ST->ConOut->QueryMode(ST->ConOut, (UINTN)ST->ConOut->Mode->Mode, &cols, &rows) != 0 || rows < 3) {
      rows = 25;
    }
    visible_rows = rows - 2;
    max_top = (line_count > visible_rows) ? (line_count - visible_rows) : 0;
    top = 0;

    /* Flush key buffer before entering pager */
    if (ST->ConIn) {
      EFI_INPUT_KEY dummy;
      while (!EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &dummy)));
    }

    for (;;) {
      ST->ConOut->ClearScreen(ST->ConOut);
      shown = 0;
      for (i = top; i < line_count && shown < visible_rows; i++, shown++) {
        UINTN end = (i + 1 < line_count) ? line_starts[i + 1] : outbuf_len;
        render_line(line_starts[i], end);
      }

      wprint_raw(L"-- line ");
      print_uint_raw(top + 1);
      wprint_raw(L"-");
      print_uint_raw(top + shown);
      wprint_raw(L" of ");
      print_uint_raw(line_count);
      wprint_raw(L" | Up/Dn scroll, PgUp/PgDn page | Press Esc, Enter, Space, Q, or Click to Exit --");

      BOOLEAN exit_requested = FALSE;
      EFI_INPUT_KEY key = read_key_nonblocking();

      if (key.ScanCode == SCAN_UP) {
        if (top > 0) top--;
      } else if (key.ScanCode == SCAN_DOWN) {
        if (top < max_top) top++;
      } else if (key.ScanCode == SCAN_PAGE_UP) {
        top = (top > visible_rows) ? top - visible_rows : 0;
      } else if (key.ScanCode == SCAN_PAGE_DOWN) {
        top = (top + visible_rows < max_top) ? top + visible_rows : max_top;
      } else if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27 ||
                 key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n' ||
                 key.UnicodeChar == L'q'  || key.UnicodeChar == L'Q'  ||
                 key.UnicodeChar == L' '  || key.UnicodeChar == L'b'  || key.UnicodeChar == L'B') {
        exit_requested = TRUE;
      }

      /* Check mouse click to exit pager */
      BOOLEAN mouse_click = FALSE;
      int dummy_x = 0, dummy_y = 0;
      poll_pointer_inputs(800, 600, &dummy_x, &dummy_y, &mouse_click);
      if (mouse_click) exit_requested = TRUE;

      if (exit_requested) break;

      if (bs && bs->Stall) bs->Stall(20000);
    }
    return 0;
  }
}


/* ========================================================================= */
/* Graphical UI Page & Menu Engine                                           */
/* ========================================================================= */
typedef struct {
  int x, y, w, h;
  const char *title;
  const char *subtext;
  UINT32 bg_normal, bg_hover, bg_active;
  UINT32 border_normal, border_hover;
  UINT32 fg_title, fg_subtext;
} UI_BUTTON;


/* ========================================================================= */
/* Zig RAM Test & Diagnostic Suite Screen                                     */
/* ========================================================================= */
static int run_zig_ram_test_screen(
  EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
  UINT32 *fb, UINT32 *back_buf, UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn)
{
  UINT32 col_bg       = make_color_gop(gop, 15, 23, 42);   /* #0F172A */
  UINT32 col_card     = make_color_gop(gop, 30, 41, 59);   /* #1E293B */
  UINT32 col_topbar   = make_color_gop(gop, 15, 23, 42);   /* Top bar bg */
  UINT32 col_border   = make_color_gop(gop, 51, 65, 85);   /* #334155 */
  UINT32 col_cyan     = make_color_gop(gop, 56, 189, 248); /* #38BDF8 */
  UINT32 col_green    = make_color_gop(gop, 34, 197, 94);  /* #22C55E */
  UINT32 col_red      = make_color_gop(gop, 239, 68, 68);  /* #EF4444 */
  UINT32 col_white    = make_color_gop(gop, 255, 255, 255);
  UINT32 col_gray     = make_color_gop(gop, 148, 163, 184);/* #94A3B8 */
  UINT32 col_btn_bg   = make_color_gop(gop, 37, 99, 235);  /* #2563EB */
  UINT32 col_btn_hov  = make_color_gop(gop, 59, 130, 246); /* #3B82F6 */
  UINT32 col_exit_norm= make_color_gop(gop, 153, 27, 27);  /* #991B1B */
  UINT32 col_exit_hov = make_color_gop(gop, 220, 38, 38);  /* #DC2626 */

  /* Allocate 64 MB RAM test pool via UEFI BootServices AllocatePool */
  UINTN test_bytes = 64 * 1024 * 1024;
  void *mem_pool = NULL;
  if (bs && bs->AllocatePool) {
    if (EFI_ERROR(bs->AllocatePool(EFI_LOADER_DATA, test_bytes, &mem_pool))) {
      test_bytes = 16 * 1024 * 1024;
      bs->AllocatePool(EFI_LOADER_DATA, test_bytes, &mem_pool);
    }
  }

  RamTestState state;
  memset(&state, 0, sizeof(state));
  ram_test_init(&state, mem_pool, test_bytes);

  int top_back_x = 15, top_back_y = 10, top_back_w = 90, top_back_h = 32;
  int top_exit_x = (int)screen_w - 110, top_exit_y = 10, top_exit_w = 95, top_exit_h = 32;

  int card_w = 640, card_h = 420;
  int card_x = ((int)screen_w - card_w) / 2;
  int card_y = ((int)screen_h - card_h) / 2 + 10;

  int back_btn_x = card_x + (card_w - 200) / 2;
  int back_btn_y = card_y + card_h - 55;
  int back_btn_w = 200, back_btn_h = 40;

  for (;;) {
    UINT32 *draw_fb = back_buf ? back_buf : fb;
    BOOLEAN curr_left_btn = FALSE;

    /* Step the Zig RAM test engine if running */
    if (state.is_running && !state.is_complete) {
      ram_test_step(&state);
    }

    poll_pointer_inputs(screen_w, screen_h, cursor_x, cursor_y, &curr_left_btn);
    BOOLEAN click_event = (curr_left_btn && !(*prev_left_btn));
    *prev_left_btn = curr_left_btn;

    EFI_INPUT_KEY key = read_key_nonblocking();
    if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27 || key.UnicodeChar == L'b' || key.UnicodeChar == L'B') {
      break;
    }
    if (key.UnicodeChar == L'x' || key.UnicodeChar == L'X') {
      if (mem_pool && bs && bs->FreePool) bs->FreePool(mem_pool);
      return 1;
    }
    if (key.UnicodeChar == L'r' || key.UnicodeChar == L'R') {
      ram_test_init(&state, mem_pool, test_bytes);
    }

    if (key.ScanCode == SCAN_UP)    *cursor_y -= 15;
    if (key.ScanCode == SCAN_DOWN)  *cursor_y += 15;
    if (key.ScanCode == SCAN_LEFT)  *cursor_x -= 15;
    if (key.ScanCode == SCAN_RIGHT) *cursor_x += 15;

    if (*cursor_x < 0) *cursor_x = 0;
    if (*cursor_y < 0) *cursor_y = 0;
    if (*cursor_x >= (int)screen_w) *cursor_x = (int)screen_w - 1;
    if (*cursor_y >= (int)screen_h) *cursor_y = (int)screen_h - 1;

    int hov_back_top = point_in_rect(*cursor_x, *cursor_y, top_back_x, top_back_y, top_back_w, top_back_h);
    int hov_exit_top = point_in_rect(*cursor_x, *cursor_y, top_exit_x, top_exit_y, top_exit_w, top_exit_h);
    int hov_back_main= point_in_rect(*cursor_x, *cursor_y, back_btn_x, back_btn_y, back_btn_w, back_btn_h);

    if (click_event) {
      if (hov_back_top || hov_back_main) break;
      if (hov_exit_top) {
        if (mem_pool && bs && bs->FreePool) bs->FreePool(mem_pool);
        return 1;
      }
    }

    /* Draw UI Background */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col_bg);

    /* Header Bar */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, 50, col_topbar);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 50, screen_w, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 120, 14, "ZIG RAM INTEGRITY TEST", 2, col_cyan, 0, 0);

    /* Top Buttons */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h,
                 hov_back_top ? col_btn_hov : col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_back_x + 14, top_back_y + 8, "< Back", 1, col_white, 0, 0);

    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h,
                 hov_exit_top ? col_exit_hov : col_exit_norm);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h, 2, col_white);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_exit_x + 18, top_exit_y + 8, "[X] Exit", 1, col_white, 0, 0);

    /* Card Box */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, 2, col_border);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, 4, col_cyan);

    /* Status Title */
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 24, "MEMORY INTEGRITY BENCHMARK", 2, col_white, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 54, "Powered by Zig High-Performance Memory Engine", 1, col_gray, 0, 0);

    /* Progress Bar Box */
    int pb_x = card_x + 30, pb_y = card_y + 85, pb_w = card_w - 60, pb_h = 24;
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, pb_x, pb_y, pb_w, pb_h, col_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, pb_x, pb_y, pb_w, pb_h, 1, col_border);

    int fill_w = (int)((float)pb_w * (state.progress_pct / 100.0f));
    if (fill_w > pb_w) fill_w = pb_w;
    if (fill_w > 0) {
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, pb_x, pb_y, fill_w, pb_h, state.errors_found > 0 ? col_red : col_cyan);
    }

    char pct_str[32];
    uprintf_str(pct_str, sizeof(pct_str), "%u%% Complete", (UINT64)state.progress_pct);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, pb_x + pb_w / 2 - 40, pb_y + 4, pct_str, 1, col_white, 0, 0);

    /* Stats Labels */
    char buf_pattern[64], buf_tested[64], buf_errors[64], buf_eta[64], buf_speed[64];
    uprintf_str(buf_pattern, sizeof(buf_pattern), "Current Pattern : %s", state.pattern_name);
    uprintf_str(buf_tested,  sizeof(buf_tested),  "Memory Allocated: %u MB", (UINT64)state.total_mb);
    uprintf_str(buf_errors,  sizeof(buf_errors),  "Errors Detected : %u", (UINT64)state.errors_found);

    UINT32 eta_m = state.eta_seconds / 60;
    UINT32 eta_s = state.eta_seconds % 60;
    uprintf_str(buf_eta,   sizeof(buf_eta),   "Estimated ETA   : %u:%s%u", (UINT64)eta_m, eta_s < 10 ? "0" : "", (UINT64)eta_s);
    uprintf_str(buf_speed, sizeof(buf_speed), "Transfer Speed  : %u MB/s", (UINT64)state.speed_mbps);

    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 125, buf_pattern, 1, col_white, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 155, buf_tested,  1, col_white, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 185, buf_errors,  1, state.errors_found > 0 ? col_red : col_green, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 350, card_y + 155, buf_speed,  1, col_cyan, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 350, card_y + 185, buf_eta,    1, col_white, 0, 0);

    /* Test Status Badge */
    if (state.is_complete) {
      if (state.is_passed && state.errors_found == 0) {
        fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 230, card_w - 60, 40, col_green);
        fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 180, card_y + 242, "PASSED: RAM HEALTH OK", 2, col_white, 0, 0);
      } else {
        fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 230, card_w - 60, 40, col_red);
        fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 180, card_y + 242, "FAILED: MEMORY ERRORS DETECTED", 2, col_white, 0, 0);
      }
    } else {
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 230, card_w - 60, 40, col_border);
      fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 180, card_y + 242, "TESTING IN PROGRESS...", 2, col_cyan, 0, 0);
    }

    /* Back Button */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h,
                 hov_back_main ? col_btn_hov : col_btn_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h, 2, col_white);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, back_btn_x + 28, back_btn_y + 12, "Back to Suite", 1, col_white, 0, 0);

    /* Draw Pointer Cursor */
    fb_draw_cursor(draw_fb, stride, screen_w, screen_h, *cursor_x, *cursor_y, col_white, make_color_gop(gop, 0, 0, 0), curr_left_btn);

    if (back_buf) {
      memcpy((void*)fb, (const void*)back_buf, (UINTN)screen_h * stride * sizeof(UINT32));
    }
    if (bs && bs->Stall) bs->Stall(16000);
  }

  if (mem_pool && bs && bs->FreePool) bs->FreePool(mem_pool);
  return 0;
}

/* ========================================================================= */
/* Interactive Test Screens                                                  */
/* ========================================================================= */

static int run_keyboard_matrix_screen(
  EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
  UINT32 *fb, UINT32 *back_buf, UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn)
{
  UINT32 col_bg       = make_color_gop(gop, 15, 23, 42);   /* #0F172A */
  UINT32 col_card     = make_color_gop(gop, 30, 41, 59);   /* #1E293B */
  UINT32 col_topbar   = make_color_gop(gop, 15, 23, 42);
  UINT32 col_border   = make_color_gop(gop, 51, 65, 85);
  UINT32 col_cyan     = make_color_gop(gop, 56, 189, 248);
  UINT32 col_green    = make_color_gop(gop, 34, 197, 94);
  UINT32 col_white    = make_color_gop(gop, 255, 255, 255);
  UINT32 col_btn_bg   = make_color_gop(gop, 15, 23, 42);
  UINT32 col_btn_hov  = make_color_gop(gop, 30, 58, 138);
  UINT32 col_exit_norm= make_color_gop(gop, 153, 27, 27);
  UINT32 col_exit_hov = make_color_gop(gop, 220, 38, 38);

  KbdMatrixState kbd_state;
  kbd_matrix_init(&kbd_state);

  int top_back_x = 15, top_back_y = 10, top_back_w = 90, top_back_h = 32;
  int top_exit_x = (int)screen_w - 110, top_exit_y = 10, top_exit_w = 95, top_exit_h = 32;

  int card_w = 720, card_h = 420;
  int card_x = ((int)screen_w - card_w) / 2;
  int card_y = ((int)screen_h - card_h) / 2 + 10;

  int back_btn_x = card_x + (card_w - 200) / 2;
  int back_btn_y = card_y + card_h - 50;
  int back_btn_w = 200, back_btn_h = 38;

  for (;;) {
    UINT32 *draw_fb = back_buf ? back_buf : fb;
    BOOLEAN curr_left_btn = FALSE;

    poll_pointer_inputs(screen_w, screen_h, cursor_x, cursor_y, &curr_left_btn);
    BOOLEAN click_event = (curr_left_btn && !(*prev_left_btn));
    *prev_left_btn = curr_left_btn;

    EFI_INPUT_KEY key = read_key_nonblocking();
    if (key.ScanCode != 0 || key.UnicodeChar != 0) {
      kbd_matrix_register_key(&kbd_state, key.ScanCode, key.UnicodeChar);
      if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27) {
        break;
      }
    }

    if (key.ScanCode == SCAN_UP)    *cursor_y -= 15;
    if (key.ScanCode == SCAN_DOWN)  *cursor_y += 15;
    if (key.ScanCode == SCAN_LEFT)  *cursor_x -= 15;
    if (key.ScanCode == SCAN_RIGHT) *cursor_x += 15;

    if (*cursor_x < 0) *cursor_x = 0;
    if (*cursor_y < 0) *cursor_y = 0;
    if (*cursor_x >= (int)screen_w) *cursor_x = (int)screen_w - 1;
    if (*cursor_y >= (int)screen_h) *cursor_y = (int)screen_h - 1;

    int hov_back_top = point_in_rect(*cursor_x, *cursor_y, top_back_x, top_back_y, top_back_w, top_back_h);
    int hov_exit_top = point_in_rect(*cursor_x, *cursor_y, top_exit_x, top_exit_y, top_exit_w, top_exit_h);
    int hov_back_main= point_in_rect(*cursor_x, *cursor_y, back_btn_x, back_btn_y, back_btn_w, back_btn_h);

    if (click_event) {
      if (hov_back_top || hov_back_main) break;
      if (hov_exit_top) return 1;
    }

    /* Draw UI Background */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col_bg);

    /* Header Bar */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, 50, col_topbar);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 50, screen_w, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 120, 14, "KEYBOARD MATRIX VISUALIZER", 2, col_cyan, 0, 0);

    /* Top Buttons */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h, hov_back_top ? col_btn_hov : col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_back_x + 14, top_back_y + 8, "< Back", 1, col_white, 0, 0);

    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h, hov_exit_top ? col_exit_hov : col_exit_norm);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h, 2, col_white);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_exit_x + 18, top_exit_y + 8, "[X] Exit", 1, col_white, 0, 0);

    /* Center Card Box */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, 2, col_border);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, 4, col_cyan);

    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 25, card_y + 20, "KEYBOARD HARDWARE MATRIX", 2, col_white, 0, 0);

    char stats_str[80];
    uprintf_str(stats_str, sizeof(stats_str), "Keys Registered: %u | Last ScanCode: 0x%x | Last Char: '%c'",
                (UINT64)kbd_state.total_pressed, (UINT64)kbd_state.last_scancode,
                kbd_state.last_char >= 32 && kbd_state.last_char <= 126 ? (char)kbd_state.last_char : '?');
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 25, card_y + 50, stats_str, 1, col_cyan, 0, 0);

    /* Render On-Screen Key Grid */
    int kx = card_x + 30, ky = card_y + 90;
    const char *row1[] = {"ESC","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    int r1_codes[] = {SCAN_ESC, SCAN_F1, SCAN_F2, SCAN_F3, SCAN_F4, SCAN_F5, SCAN_F6, SCAN_F7, SCAN_F8, SCAN_F9, SCAN_F10, SCAN_F11, SCAN_F12};

    int idx;
    for (idx = 0; idx < 13; idx++) {
      int kw = (idx == 0) ? 55 : 44;
      int code = r1_codes[idx];
      BOOLEAN is_p = (code > 0 && code < 256) && (kbd_state.key_mask[code] != 0);

      fb_fill_rect(draw_fb, stride, screen_w, screen_h, kx, ky, kw, 34, is_p ? col_green : col_btn_bg);
      fb_draw_rect(draw_fb, stride, screen_w, screen_h, kx, ky, kw, 34, 1, col_border);
      fb_draw_text(draw_fb, stride, screen_w, screen_h, kx + 8, ky + 10, row1[idx], 1, is_p ? col_bg : col_white, 0, 0);
      kx += kw + 6;
    }

    /* QWERTY Row 1 (Numbers) */
    kx = card_x + 30; ky += 44;
    const char *row2 = "`1234567890-=";
    for (idx = 0; idx < 13; idx++) {
      char ch = row2[idx];
      BOOLEAN is_p = kbd_state.key_mask[(UINT8)ch] != 0;
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, kx, ky, 44, 34, is_p ? col_green : col_btn_bg);
      fb_draw_rect(draw_fb, stride, screen_w, screen_h, kx, ky, 44, 34, 1, col_border);
      char cstr[2] = {ch, 0};
      fb_draw_text(draw_fb, stride, screen_w, screen_h, kx + 16, ky + 10, cstr, 1, is_p ? col_bg : col_white, 0, 0);
      kx += 50;
    }

    /* QWERTY Row 2 (Q-P) */
    kx = card_x + 30; ky += 44;
    const char *row3 = "QWERTYUIOP[]";
    for (idx = 0; idx < 12; idx++) {
      char ch = row3[idx];
      char lower_ch = (ch >= 'A' && ch <= 'Z') ? (ch + 32) : ch;
      BOOLEAN is_p = (kbd_state.key_mask[(UINT8)ch] != 0) || (kbd_state.key_mask[(UINT8)lower_ch] != 0);
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, kx, ky, 44, 34, is_p ? col_green : col_btn_bg);
      fb_draw_rect(draw_fb, stride, screen_w, screen_h, kx, ky, 44, 34, 1, col_border);
      char cstr[2] = {ch, 0};
      fb_draw_text(draw_fb, stride, screen_w, screen_h, kx + 16, ky + 10, cstr, 1, is_p ? col_bg : col_white, 0, 0);
      kx += 50;
    }

    /* QWERTY Row 3 (A-L) */
    kx = card_x + 30; ky += 44;
    const char *row4 = "ASDFGHJKL;'";
    for (idx = 0; idx < 11; idx++) {
      char ch = row4[idx];
      char lower_ch = (ch >= 'A' && ch <= 'Z') ? (ch + 32) : ch;
      BOOLEAN is_p = (kbd_state.key_mask[(UINT8)ch] != 0) || (kbd_state.key_mask[(UINT8)lower_ch] != 0);
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, kx, ky, 44, 34, is_p ? col_green : col_btn_bg);
      fb_draw_rect(draw_fb, stride, screen_w, screen_h, kx, ky, 44, 34, 1, col_border);
      char cstr[2] = {ch, 0};
      fb_draw_text(draw_fb, stride, screen_w, screen_h, kx + 16, ky + 10, cstr, 1, is_p ? col_bg : col_white, 0, 0);
      kx += 50;
    }

    /* QWERTY Row 4 (Z-M) */
    kx = card_x + 30; ky += 44;
    const char *row5 = "ZXCVBNM,./";
    for (idx = 0; idx < 10; idx++) {
      char ch = row5[idx];
      char lower_ch = (ch >= 'A' && ch <= 'Z') ? (ch + 32) : ch;
      BOOLEAN is_p = (kbd_state.key_mask[(UINT8)ch] != 0) || (kbd_state.key_mask[(UINT8)lower_ch] != 0);
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, kx, ky, 44, 34, is_p ? col_green : col_btn_bg);
      fb_draw_rect(draw_fb, stride, screen_w, screen_h, kx, ky, 44, 34, 1, col_border);
      char cstr[2] = {ch, 0};
      fb_draw_text(draw_fb, stride, screen_w, screen_h, kx + 16, ky + 10, cstr, 1, is_p ? col_bg : col_white, 0, 0);
      kx += 50;
    }

    /* Back Button */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h, hov_back_main ? col_btn_hov : col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, back_btn_x + 20, back_btn_y + 11, "Back to Suite", 1, col_white, 0, 0);

    /* Draw Pointer Cursor */
    fb_draw_cursor(draw_fb, stride, screen_w, screen_h, *cursor_x, *cursor_y, col_white, make_color_gop(gop, 0, 0, 0), curr_left_btn);

    if (back_buf) {
      memcpy((void*)fb, (const void*)back_buf, (UINTN)screen_h * stride * sizeof(UINT32));
    }
    if (bs && bs->Stall) bs->Stall(16000);
  }

  return 0;
}

static int run_display_pixel_audit_screen(
  EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
  UINT32 *fb, UINT32 *back_buf, UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn)
{
  (VOID)cursor_x; (VOID)cursor_y; (VOID)prev_left_btn;

  int pattern_idx = 0;
  const int max_patterns = 6;

  for (;;) {
    UINT32 *draw_fb = back_buf ? back_buf : fb;

    EFI_INPUT_KEY key = read_key_nonblocking();
    if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27 || key.UnicodeChar == L'b' || key.UnicodeChar == L'B') {
      break;
    }
    if (key.UnicodeChar == L' ' || key.ScanCode == SCAN_RIGHT || key.ScanCode == SCAN_DOWN) {
      pattern_idx = (pattern_idx + 1) % max_patterns;
    } else if (key.ScanCode == SCAN_LEFT || key.ScanCode == SCAN_UP) {
      pattern_idx = (pattern_idx + max_patterns - 1) % max_patterns;
    }

    UINT32 col = 0;
    if (pattern_idx == 0)      col = make_color_gop(gop, 255, 0, 0);     /* Solid Red */
    else if (pattern_idx == 1) col = make_color_gop(gop, 0, 255, 0);     /* Solid Green */
    else if (pattern_idx == 2) col = make_color_gop(gop, 0, 0, 255);     /* Solid Blue */
    else if (pattern_idx == 3) col = make_color_gop(gop, 255, 255, 255); /* Solid White */
    else if (pattern_idx == 4) col = make_color_gop(gop, 0, 0, 0);       /* Solid Black */

    if (pattern_idx <= 4) {
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col);
    } else {
      /* Pattern 5: Alignment Grid Lines */
      UINT32 bg = make_color_gop(gop, 15, 23, 42);
      UINT32 line = make_color_gop(gop, 255, 255, 255);
      fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, bg);
      UINT32 x, y;
      for (x = 0; x < screen_w; x += 50) {
        fb_fill_rect(draw_fb, stride, screen_w, screen_h, x, 0, 2, screen_h, line);
      }
      for (y = 0; y < screen_h; y += 50) {
        fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, y, screen_w, 2, line);
      }
    }

    /* Hint text overlay */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 20, 20, 520, 40, make_color_gop(gop, 15, 23, 42));
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, 20, 20, 520, 40, 2, make_color_gop(gop, 56, 189, 248));
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 30, 32, "Press Space / Arrow Keys to cycle colors | Esc to return", 1, make_color_gop(gop, 255, 255, 255), 0, 0);

    if (back_buf) {
      memcpy((void*)fb, (const void*)back_buf, (UINTN)screen_h * stride * sizeof(UINT32));
    }
    if (bs && bs->Stall) bs->Stall(16000);
  }

  return 0;
}

static int run_pc_speaker_audio_screen(
  EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
  UINT32 *fb, UINT32 *back_buf, UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn)
{
  UINT32 col_bg       = make_color_gop(gop, 15, 23, 42);
  UINT32 col_card     = make_color_gop(gop, 30, 41, 59);
  UINT32 col_topbar   = make_color_gop(gop, 15, 23, 42);
  UINT32 col_border   = make_color_gop(gop, 51, 65, 85);
  UINT32 col_cyan     = make_color_gop(gop, 56, 189, 248);
  UINT32 col_amber    = make_color_gop(gop, 245, 158, 11);
  UINT32 col_white    = make_color_gop(gop, 255, 255, 255);
  UINT32 col_gray     = make_color_gop(gop, 148, 163, 184);
  UINT32 col_btn_bg   = make_color_gop(gop, 15, 23, 42);
  UINT32 col_btn_hov  = make_color_gop(gop, 30, 58, 138);
  UINT32 col_exit_norm= make_color_gop(gop, 153, 27, 27);
  UINT32 col_exit_hov = make_color_gop(gop, 220, 38, 38);

  UINT32 active_freq = 0;
  BOOLEAN is_sweeping = FALSE;
  UINT32 sweep_freq = 400;

  int top_back_x = 15, top_back_y = 10, top_back_w = 90, top_back_h = 32;
  int top_exit_x = (int)screen_w - 110, top_exit_y = 10, top_exit_w = 95, top_exit_h = 32;

  int card_w = 640, card_h = 420;
  int card_x = ((int)screen_w - card_w) / 2;
  int card_y = ((int)screen_h - card_h) / 2 + 10;

  int back_btn_x = card_x + (card_w - 200) / 2;
  int back_btn_y = card_y + card_h - 55;
  int back_btn_w = 200, back_btn_h = 40;

  for (;;) {
    UINT32 *draw_fb = back_buf ? back_buf : fb;
    BOOLEAN curr_left_btn = FALSE;

    if (is_sweeping) {
      sweep_freq += 25;
      if (sweep_freq > 2000) sweep_freq = 400;
      audio_play_tone(sweep_freq);
      active_freq = sweep_freq;
    }

    poll_pointer_inputs(screen_w, screen_h, cursor_x, cursor_y, &curr_left_btn);
    BOOLEAN click_event = (curr_left_btn && !(*prev_left_btn));
    *prev_left_btn = curr_left_btn;

    EFI_INPUT_KEY key = read_key_nonblocking();
    if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27 || key.UnicodeChar == L'b' || key.UnicodeChar == L'B') {
      audio_stop_tone();
      break;
    }
    if (key.UnicodeChar == L'x' || key.UnicodeChar == L'X') {
      audio_stop_tone();
      return 1;
    }
    if (key.UnicodeChar == L'1') { is_sweeping = FALSE; active_freq = 440; audio_play_tone(440); }
    if (key.UnicodeChar == L'2') { is_sweeping = FALSE; active_freq = 523; audio_play_tone(523); }
    if (key.UnicodeChar == L'3') { is_sweeping = FALSE; active_freq = 659; audio_play_tone(659); }
    if (key.UnicodeChar == L'4') { is_sweeping = FALSE; active_freq = 880; audio_play_tone(880); }
    if (key.UnicodeChar == L'5') { is_sweeping = TRUE; sweep_freq = 400; }
    if (key.UnicodeChar == L'0' || key.UnicodeChar == L' ') { is_sweeping = FALSE; active_freq = 0; audio_stop_tone(); }

    if (key.ScanCode == SCAN_UP)    *cursor_y -= 15;
    if (key.ScanCode == SCAN_DOWN)  *cursor_y += 15;
    if (key.ScanCode == SCAN_LEFT)  *cursor_x -= 15;
    if (key.ScanCode == SCAN_RIGHT) *cursor_x += 15;

    if (*cursor_x < 0) *cursor_x = 0;
    if (*cursor_y < 0) *cursor_y = 0;
    if (*cursor_x >= (int)screen_w) *cursor_x = (int)screen_w - 1;
    if (*cursor_y >= (int)screen_h) *cursor_y = (int)screen_h - 1;

    int hov_back_top = point_in_rect(*cursor_x, *cursor_y, top_back_x, top_back_y, top_back_w, top_back_h);
    int hov_exit_top = point_in_rect(*cursor_x, *cursor_y, top_exit_x, top_exit_y, top_exit_w, top_exit_h);
    int hov_back_main= point_in_rect(*cursor_x, *cursor_y, back_btn_x, back_btn_y, back_btn_w, back_btn_h);

    if (click_event) {
      if (hov_back_top || hov_back_main) { audio_stop_tone(); break; }
      if (hov_exit_top) { audio_stop_tone(); return 1; }
    }

    /* Draw UI Background */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col_bg);

    /* Header Bar */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, 50, col_topbar);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 50, screen_w, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 120, 14, "PC SPEAKER FREQUENCY GENERATOR", 2, col_cyan, 0, 0);

    /* Top Buttons */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h, hov_back_top ? col_btn_hov : col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_back_x + 14, top_back_y + 8, "< Back", 1, col_white, 0, 0);

    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h, hov_exit_top ? col_exit_hov : col_exit_norm);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h, 2, col_white);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_exit_x + 18, top_exit_y + 8, "[X] Exit", 1, col_white, 0, 0);

    /* Center Card Box */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, 2, col_border);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, 4, col_amber);

    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 24, "AUDIO HARDWARE GENERATOR", 2, col_white, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 54, "Direct 8254 PIT Port 0x42/0x43/0x61 Pulse Generator", 1, col_gray, 0, 0);

    char status_buf[64];
    uprintf_str(status_buf, sizeof(status_buf), "Active Tone Frequency: %u Hz %s",
                (UINT64)active_freq, is_sweeping ? "[SWEEP]" : (active_freq > 0 ? "[PLAYING]" : "[MUTED]"));
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 95, status_buf, 1, col_cyan, 0, 0);

    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 135,
                 "Press Number Keys to Play Frequency Tones:\n"
                 "  [1] 440 Hz (Concert A4)\n"
                 "  [2] 523 Hz (C5 Note)\n"
                 "  [3] 659 Hz (E5 Note)\n"
                 "  [4] 880 Hz (A5 Note)\n"
                 "  [5] Pitch Sweep Test (400Hz -> 2000Hz)\n"
                 "  [0] / [Space] Mute Audio", 1, col_white, 0, 0);

    /* Back Button */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h, hov_back_main ? col_btn_hov : col_btn_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, back_btn_x + 28, back_btn_y + 12, "Back to Suite", 1, col_white, 0, 0);

    /* Draw Pointer Cursor */
    fb_draw_cursor(draw_fb, stride, screen_w, screen_h, *cursor_x, *cursor_y, col_white, make_color_gop(gop, 0, 0, 0), curr_left_btn);

    if (back_buf) {
      memcpy((void*)fb, (const void*)back_buf, (UINTN)screen_h * stride * sizeof(UINT32));
    }
    if (bs && bs->Stall) bs->Stall(16000);
  }

  audio_stop_tone();
  return 0;
}

/* Render Redesigned Diagnostic Suite Screen */
static int render_diagnostic_suite_screen(
  EFI_HANDLE ImageHandle,
  EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
  UINT32 *fb, UINT32 *back_buf, UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn)
{
  (VOID)ImageHandle;
  UINT32 col_bg       = make_color_gop(gop, 15, 23, 42);   /* #0F172A */
  UINT32 col_card     = make_color_gop(gop, 30, 41, 59);   /* #1E293B */
  UINT32 col_topbar   = make_color_gop(gop, 15, 23, 42);   /* Top bar bg */
  UINT32 col_border   = make_color_gop(gop, 51, 65, 85);   /* #334155 */
  UINT32 col_cyan     = make_color_gop(gop, 56, 189, 248); /* #38BDF8 */
  UINT32 col_green    = make_color_gop(gop, 34, 197, 94);  /* #22C55E */
  UINT32 col_white    = make_color_gop(gop, 255, 255, 255);
  UINT32 col_gray     = make_color_gop(gop, 148, 163, 184);/* #94A3B8 */
  UINT32 col_btn_bg   = make_color_gop(gop, 15, 23, 42);
  UINT32 col_btn_hov  = make_color_gop(gop, 30, 58, 138);  /* Dark Blue */
  UINT32 col_exit_norm= make_color_gop(gop, 153, 27, 27);  /* #991B1B */
  UINT32 col_exit_hov = make_color_gop(gop, 220, 38, 38);  /* #DC2626 */

  int card_w = 700, card_h = 420;
  int card_x = ((int)screen_w - card_w) / 2;
  int card_y = ((int)screen_h - card_h) / 2 + 10;

  int top_back_x = 15, top_back_y = 10, top_back_w = 90, top_back_h = 32;
  int top_exit_x = (int)screen_w - 110, top_exit_y = 10, top_exit_w = 95, top_exit_h = 32;

  /* Grid Buttons (2x2 Grid) */
  int tile_w = 310, tile_h = 125;
  int tile0_x = card_x + 25,  tile0_y = card_y + 80;
  int tile1_x = card_x + 365, tile1_y = card_y + 80;
  int tile2_x = card_x + 25,  tile2_y = card_y + 225;
  int tile3_x = card_x + 365, tile3_y = card_y + 225;

  int back_btn_x = card_x + (card_w - 220) / 2;
  int back_btn_y = card_y + card_h - 50;
  int back_btn_w = 220, back_btn_h = 38;

  int selected_tile = 0;

  /* Flush key buffer */
  if (ST->ConIn) {
    EFI_INPUT_KEY dummy;
    while (!EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &dummy)));
  }

  for (;;) {
    UINT32 *draw_fb = back_buf ? back_buf : fb;
    BOOLEAN curr_left_btn = FALSE;

    poll_pointer_inputs(screen_w, screen_h, cursor_x, cursor_y, &curr_left_btn);
    BOOLEAN click_event = (curr_left_btn && !(*prev_left_btn));
    *prev_left_btn = curr_left_btn;

    EFI_INPUT_KEY key = read_key_nonblocking();
    if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27 || key.UnicodeChar == L'b' || key.UnicodeChar == L'B') {
      return 0;
    }
    if (key.UnicodeChar == L'x' || key.UnicodeChar == L'X') {
      return 1;
    }
    if (key.UnicodeChar == L'1') {
      selected_tile = 0;
      int rret = run_zig_ram_test_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
      if (rret == 1) return 1;
      continue;
    } else if (key.UnicodeChar == L'2') {
      selected_tile = 1;
      int kret = run_keyboard_matrix_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
      if (kret == 1) return 1;
      continue;
    } else if (key.UnicodeChar == L'3') {
      selected_tile = 2;
      int dret = run_display_pixel_audit_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
      if (dret == 1) return 1;
      continue;
    } else if (key.UnicodeChar == L'4') {
      selected_tile = 3;
      int aret = run_pc_speaker_audio_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
      if (aret == 1) return 1;
      continue;
    } else if (key.UnicodeChar == L'\r' || key.UnicodeChar == L' ') {
      if (selected_tile == 0) {
        int rret = run_zig_ram_test_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
        if (rret == 1) return 1;
        continue;
      } else if (selected_tile == 1) {
        int kret = run_keyboard_matrix_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
        if (kret == 1) return 1;
        continue;
      } else if (selected_tile == 2) {
        int dret = run_display_pixel_audit_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
        if (dret == 1) return 1;
        continue;
      } else if (selected_tile == 3) {
        int aret = run_pc_speaker_audio_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
        if (aret == 1) return 1;
        continue;
      }
    }

    if (key.ScanCode == SCAN_UP) {
      if (selected_tile >= 2) selected_tile -= 2;
      *cursor_y -= 15;
    } else if (key.ScanCode == SCAN_DOWN) {
      if (selected_tile < 2) selected_tile += 2;
      *cursor_y += 15;
    } else if (key.ScanCode == SCAN_LEFT) {
      if (selected_tile % 2 == 1) selected_tile -= 1;
      *cursor_x -= 15;
    } else if (key.ScanCode == SCAN_RIGHT) {
      if (selected_tile % 2 == 0) selected_tile += 1;
      *cursor_x += 15;
    }

    if (*cursor_x < 0) *cursor_x = 0;
    if (*cursor_y < 0) *cursor_y = 0;
    if (*cursor_x >= (int)screen_w) *cursor_x = (int)screen_w - 1;
    if (*cursor_y >= (int)screen_h) *cursor_y = (int)screen_h - 1;

    int hov_back_top = point_in_rect(*cursor_x, *cursor_y, top_back_x, top_back_y, top_back_w, top_back_h);
    int hov_exit_top = point_in_rect(*cursor_x, *cursor_y, top_exit_x, top_exit_y, top_exit_w, top_exit_h);
    int hov_tile0    = point_in_rect(*cursor_x, *cursor_y, tile0_x, tile0_y, tile_w, tile_h);
    int hov_tile1    = point_in_rect(*cursor_x, *cursor_y, tile1_x, tile1_y, tile_w, tile_h);
    int hov_tile2    = point_in_rect(*cursor_x, *cursor_y, tile2_x, tile2_y, tile_w, tile_h);
    int hov_tile3    = point_in_rect(*cursor_x, *cursor_y, tile3_x, tile3_y, tile_w, tile_h);
    int hov_back_main= point_in_rect(*cursor_x, *cursor_y, back_btn_x, back_btn_y, back_btn_w, back_btn_h);

    if (hov_tile0) selected_tile = 0;
    if (hov_tile1) selected_tile = 1;
    if (hov_tile2) selected_tile = 2;
    if (hov_tile3) selected_tile = 3;

    int is_sel0 = (selected_tile == 0);
    int is_sel1 = (selected_tile == 1);
    int is_sel2 = (selected_tile == 2);
    int is_sel3 = (selected_tile == 3);

    if (click_event) {
      if (hov_back_top || hov_back_main) return 0;
      if (hov_exit_top) return 1;
      if (hov_tile0) {
        int rret = run_zig_ram_test_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
        if (rret == 1) return 1;
        continue;
      }
      if (hov_tile1) {
        int kret = run_keyboard_matrix_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
        if (kret == 1) return 1;
        continue;
      }
      if (hov_tile2) {
        int dret = run_display_pixel_audit_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
        if (dret == 1) return 1;
        continue;
      }
      if (hov_tile3) {
        int aret = run_pc_speaker_audio_screen(bs, gop, fb, back_buf, stride, screen_w, screen_h, cursor_x, cursor_y, prev_left_btn);
        if (aret == 1) return 1;
        continue;
      }
    }

    /* Draw UI Background */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col_bg);

    /* Header Bar */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, 50, col_topbar);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 50, screen_w, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 120, 14, "HARDWARE DIAGNOSTIC SUITE", 2, col_white, 0, 0);

    /* Top Buttons */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h,
                 hov_back_top ? col_btn_hov : col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_back_x + 14, top_back_y + 8, "< Back", 1, col_white, 0, 0);

    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h,
                 hov_exit_top ? col_exit_hov : col_exit_norm);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h, 2, col_white);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_exit_x + 18, top_exit_y + 8, "[X] Exit", 1, col_white, 0, 0);

    /* Center Card Box */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, 2, col_border);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, 4, col_cyan);

    /* Card Header Titles */
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 25, card_y + 20, "SELECT DIAGNOSTIC BENCHMARK", 2, col_cyan, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 25, card_y + 50, "Use Arrow Keys + Enter or Keys 1-4 to select a benchmark", 1, col_gray, 0, 0);

    /* Tile 0: Quick RAM Test [ZIG] */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, tile0_x, tile0_y, tile_w, tile_h, is_sel0 ? col_btn_hov : col_btn_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, tile0_x, tile0_y, tile_w, tile_h, 2, is_sel0 ? col_cyan : col_border);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, tile0_x + 200, tile0_y + 12, 95, 20, col_cyan);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile0_x + 208, tile0_y + 16, "ZIG ENGINE", 1, col_topbar, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile0_x + 15, tile0_y + 14, "1. Quick RAM Test", 1, col_white, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile0_x + 15, tile0_y + 45,
                 "Multi-pattern RAM integrity\n"
                 "& real-time ETA engine", 1, col_gray, 0, 0);

    /* Tile 1: Keyboard Matrix [ZIG] */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, tile1_x, tile1_y, tile_w, tile_h, is_sel1 ? col_btn_hov : col_btn_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, tile1_x, tile1_y, tile_w, tile_h, 2, is_sel1 ? col_cyan : col_border);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, tile1_x + 200, tile1_y + 12, 95, 20, col_green);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile1_x + 208, tile1_y + 16, "INTERACTIVE", 1, col_topbar, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile1_x + 15, tile1_y + 14, "2. Keyboard Matrix", 1, col_white, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile1_x + 15, tile1_y + 45,
                 "Visual key layout tracker &\n"
                 "stuck key detector [Zig]", 1, col_gray, 0, 0);

    /* Tile 2: Display & Pixel Audit [C] */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, tile2_x, tile2_y, tile_w, tile_h, is_sel2 ? col_btn_hov : col_btn_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, tile2_x, tile2_y, tile_w, tile_h, 2, is_sel2 ? col_cyan : col_border);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, tile2_x + 200, tile2_y + 12, 95, 20, col_green);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile2_x + 208, tile2_y + 16, "INTERACTIVE", 1, col_topbar, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile2_x + 15, tile2_y + 14, "3. Display Pixel Audit", 1, col_white, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile2_x + 15, tile2_y + 45,
                 "Full-screen solid RGB,\n"
                 "dead pixel & grid test", 1, col_gray, 0, 0);

    /* Tile 3: PC Speaker Audio [ZIG] */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, tile3_x, tile3_y, tile_w, tile_h, is_sel3 ? col_btn_hov : col_btn_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, tile3_x, tile3_y, tile_w, tile_h, 2, is_sel3 ? col_cyan : col_border);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, tile3_x + 200, tile3_y + 12, 95, 20, col_green);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile3_x + 208, tile3_y + 16, "INTERACTIVE", 1, col_topbar, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile3_x + 15, tile3_y + 14, "4. PC Speaker Audio", 1, col_white, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, tile3_x + 15, tile3_y + 45,
                 "8254 PIT tone generator\n"
                 "& frequency sweep [Zig]", 1, col_gray, 0, 0);

    /* Back Button */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h,
                 hov_back_main ? col_btn_hov : col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, back_btn_x + 20, back_btn_y + 11, "Back to Main Menu", 1, col_white, 0, 0);

    /* Draw Pointer Cursor */
    fb_draw_cursor(draw_fb, stride, screen_w, screen_h, *cursor_x, *cursor_y, col_white, make_color_gop(gop, 0, 0, 0), curr_left_btn);

    if (back_buf) {
      memcpy((void*)fb, (const void*)back_buf, (UINTN)screen_h * stride * sizeof(UINT32));
    }
    if (bs && bs->Stall) bs->Stall(16000);
  }
}

/* Render Graphical Home Menu */
void run_graphical_home_menu(EFI_HANDLE ImageHandle, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop) {
  EFI_BOOT_SERVICES *bs = ST->BootServices;
  UINT32 *fb = (UINT32*)gop->Mode->FrameBufferBase;
  UINT32 stride = gop->Mode->Info->PixelsPerScanLine;
  UINT32 screen_w = gop->Mode->Info->HorizontalResolution;
  UINT32 screen_h = gop->Mode->Info->VerticalResolution;

  UINT32 *back_buf = NULL;
  UINTN buf_size = (UINTN)screen_h * stride * sizeof(UINT32);

  if (bs && bs->AllocatePool) {
    bs->AllocatePool(EFI_LOADER_DATA, buf_size, (VOID**)&back_buf);
  }

  /* Comprehensive Pointer Protocols & PS/2 Hardware Driver Discovery */
  init_pointer_protocols(ImageHandle, bs);
  {
    char init_dbg[100];
    uprintf_str(init_dbg, sizeof(init_dbg), "hwdiag: init_pointer_protocols -> simple=%u abs=%u\n",
                (UINT64)g_num_simple_pointers, (UINT64)g_num_abs_pointers);
    serial_write_str(init_dbg);
  }

  /* One-shot burst diagnostic: dump raw addresses and hammer GetState 100x
   * back-to-back with zero delay, before the normal 60fps loop even starts.
   * This tells us whether ANY call ever succeeds at all, independent of
   * frame timing, and whether the pointers themselves look sane. */
  if (g_num_abs_pointers > 0) {
    EFI_ABSOLUTE_POINTER_PROTOCOL *ap0 = g_abs_pointers[0];
    char adbg[220];
    uprintf_str(adbg, sizeof(adbg), "burst: ap=%u Reset=%u GetState=%u Mode=%u\n",
                (UINT64)(UINTN)ap0, (UINT64)(UINTN)(ap0 ? ap0->Reset : 0),
                (UINT64)(UINTN)(ap0 ? ap0->GetState : 0), (UINT64)(UINTN)(ap0 ? ap0->Mode : 0));
    serial_write_str(adbg);
    UINTN j; UINTN successes = 0;
    for (j = 0; j < 100 && ap0; j++) {
      EFI_ABSOLUTE_POINTER_STATE bstate = {0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF};
      EFI_STATUS bst = ap0->GetState(ap0, &bstate);
      if (bst == EFI_SUCCESS) {
        successes++;
        char sdbg[160];
        uprintf_str(sdbg, sizeof(sdbg), "burst: iter=%u SUCCESS x=%u y=%u btn=%u\n",
                    j, bstate.CurrentX, bstate.CurrentY, (UINT64)bstate.ActiveButtons);
        serial_write_str(sdbg);
      }
    }
    char rdbg[100];
    uprintf_str(rdbg, sizeof(rdbg), "burst: done, successes=%u/100\n", successes);
    serial_write_str(rdbg);
  }

  int cursor_x = (int)screen_w / 2;
  int cursor_y = (int)screen_h / 2;
  BOOLEAN prev_left_btn = FALSE;
  int selected_btn_idx = 0;

  /* Theme Palette */
  UINT32 col_bg        = make_color_gop(gop, 15, 23, 42);    /* #0F172A Dark Slate */
  UINT32 col_card_bg   = make_color_gop(gop, 30, 41, 59);    /* #1E293B Card Slate */
  UINT32 col_header_bg = make_color_gop(gop, 15, 23, 42);    /* Header bar */
  UINT32 col_card_brd  = make_color_gop(gop, 51, 65, 85);    /* #334155 Border */
  UINT32 col_accent_cyan= make_color_gop(gop, 56, 189, 248);  /* #38BDF8 Accent */
  UINT32 col_white     = make_color_gop(gop, 255, 255, 255);
  UINT32 col_gray      = make_color_gop(gop, 148, 163, 184); /* #94A3B8 Text */
  UINT32 col_black     = make_color_gop(gop, 0, 0, 0);

  /* Button Styling */
  UI_BUTTON buttons[3];
  int card_w = 540, card_h = 360;
  int card_x = ((int)screen_w - card_w) / 2;
  int card_y = ((int)screen_h - card_h) / 2 + 15;

  /* 1. Hardware Information Button */
  buttons[0].x = card_x + 30;
  buttons[0].y = card_y + 90;
  buttons[0].w = 480;
  buttons[0].h = 68;
  buttons[0].title = "1. Hardware Information";
  buttons[0].subtext = "View CPU, Memory Map, ACPI, SMBIOS & Storage";
  buttons[0].bg_normal = make_color_gop(gop, 15, 23, 42);
  buttons[0].bg_hover  = make_color_gop(gop, 30, 58, 138);  /* #1E3A8A Dark Blue */
  buttons[0].bg_active = make_color_gop(gop, 29, 78, 216);  /* #1D4ED8 Active Blue */
  buttons[0].border_normal = make_color_gop(gop, 71, 85, 105);
  buttons[0].border_hover  = make_color_gop(gop, 96, 165, 250);/* #60A5FA */
  buttons[0].fg_title = col_white;
  buttons[0].fg_subtext = col_gray;

  /* 2. Donut Easter Egg Button */
  buttons[1].x = card_x + 30;
  buttons[1].y = card_y + 175;
  buttons[1].w = 480;
  buttons[1].h = 68;
  buttons[1].title = "2. 3D Spinning Donut";
  buttons[1].subtext = "View 3D ASCII Donut & PC Speaker Riff";
  buttons[1].bg_normal = make_color_gop(gop, 15, 23, 42);
  buttons[1].bg_hover  = make_color_gop(gop, 88, 28, 135);  /* #581C87 Dark Purple */
  buttons[1].bg_active = make_color_gop(gop, 126, 34, 206); /* #7E22CE Active Purple */
  buttons[1].border_normal = make_color_gop(gop, 71, 85, 105);
  buttons[1].border_hover  = make_color_gop(gop, 192, 132, 252);/* #C084FC */
  buttons[1].fg_title = col_white;
  buttons[1].fg_subtext = col_gray;

  /* 3. Hardware Diagnostic Suite Button */
  buttons[2].x = card_x + 30;
  buttons[2].y = card_y + 260;
  buttons[2].w = 480;
  buttons[2].h = 68;
  buttons[2].title = "3. Hardware Diagnostic Suite";
  buttons[2].subtext = "Execute RAM Integrity Test [ZIG] & Hardware Tests";
  buttons[2].bg_normal = make_color_gop(gop, 15, 23, 42);
  buttons[2].bg_hover  = make_color_gop(gop, 120, 53, 15);  /* #78350F Dark Amber */
  buttons[2].bg_active = make_color_gop(gop, 180, 83, 9);   /* #B45309 Active Amber */
  buttons[2].border_normal = make_color_gop(gop, 71, 85, 105);
  buttons[2].border_hover  = make_color_gop(gop, 251, 191, 36);/* #FBBF24 */
  buttons[2].fg_title = col_white;
  buttons[2].fg_subtext = col_gray;

  /* Top Right Exit Button */
  int exit_x = (int)screen_w - 110, exit_y = 10, exit_w = 95, exit_h = 32;
  UINT32 col_exit_norm = make_color_gop(gop, 153, 27, 27);  /* #991B1B */
  UINT32 col_exit_hov  = make_color_gop(gop, 220, 38, 38);  /* #DC2626 */

  for (;;) {
    UINT32 *draw_fb = back_buf ? back_buf : fb;
    BOOLEAN curr_left_btn = FALSE;

    /* Poll Mouse/Tablet Input across all handles */
    poll_pointer_inputs(screen_w, screen_h, &cursor_x, &cursor_y, &curr_left_btn);

    BOOLEAN click_event = (curr_left_btn && !prev_left_btn);
    prev_left_btn = curr_left_btn;

    /* Keyboard Input Handling with Correct Standard UEFI ScanCodes */
    EFI_INPUT_KEY key = read_key_nonblocking();
    if (key.ScanCode == SCAN_UP) {
      selected_btn_idx = (selected_btn_idx + 2) % 3;
      cursor_y -= 15;
    } else if (key.ScanCode == SCAN_DOWN) {
      selected_btn_idx = (selected_btn_idx + 1) % 3;
      cursor_y += 15;
    } else if (key.ScanCode == SCAN_LEFT) {
      cursor_x -= 15;
    } else if (key.ScanCode == SCAN_RIGHT) {
      cursor_x += 15;
    } else if (key.UnicodeChar == L'1' || key.UnicodeChar == L'h' || key.UnicodeChar == L'H') {
      gather_hardware_info(ImageHandle);
      { int pret = run_pager(bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn); if (pret == 1) break; }
      continue;
    } else if (key.UnicodeChar == L'2' || key.UnicodeChar == L'd' || key.UnicodeChar == L'D') {
      { int dret = easter_egg_donut(bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn); if (dret == 1) break; }
      continue;
    } else if (key.UnicodeChar == L'3' || key.UnicodeChar == L'w' || key.UnicodeChar == L'W') {
      int ret = render_diagnostic_suite_screen(ImageHandle, bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn);
      if (ret == 1) break;
      continue;
    } else if (key.UnicodeChar == L'x' || key.UnicodeChar == L'X' || key.ScanCode == SCAN_ESC || key.UnicodeChar == 27) {
      break; /* Exit application */
    } else if (key.UnicodeChar == L'\r' || key.UnicodeChar == L' ') {
      if (selected_btn_idx == 0) {
        gather_hardware_info(ImageHandle);
        { int pret = run_pager(bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn); if (pret == 1) break; }
        continue;
      } else if (selected_btn_idx == 1) {
        { int dret = easter_egg_donut(bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn); if (dret == 1) break; }
        continue;
      } else if (selected_btn_idx == 2) {
        int ret = render_diagnostic_suite_screen(ImageHandle, bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn);
        if (ret == 1) break;
        continue;
      }
    }

    /* Clamp cursor bounds */
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x >= (int)screen_w) cursor_x = (int)screen_w - 1;
    if (cursor_y >= (int)screen_h) cursor_y = (int)screen_h - 1;

    /* Hover & Click Detection */
    int hover_exit = point_in_rect(cursor_x, cursor_y, exit_x, exit_y, exit_w, exit_h);
    int i;
    for (i = 0; i < 3; i++) {
      if (point_in_rect(cursor_x, cursor_y, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h)) {
        selected_btn_idx = i;
      }
    }

    if (click_event) {
      if (hover_exit) break; /* Exit application */
      if (selected_btn_idx == 0 && point_in_rect(cursor_x, cursor_y, buttons[0].x, buttons[0].y, buttons[0].w, buttons[0].h)) {
        gather_hardware_info(ImageHandle);
        { int pret = run_pager(bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn); if (pret == 1) break; }
        continue;
      } else if (selected_btn_idx == 1 && point_in_rect(cursor_x, cursor_y, buttons[1].x, buttons[1].y, buttons[1].w, buttons[1].h)) {
        { int dret = easter_egg_donut(bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn); if (dret == 1) break; }
        continue;
      } else if (selected_btn_idx == 2 && point_in_rect(cursor_x, cursor_y, buttons[2].x, buttons[2].y, buttons[2].w, buttons[2].h)) {
        int ret = render_diagnostic_suite_screen(ImageHandle, bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn);
        if (ret == 1) break;
        continue;
      }
    }

    /* ---- Draw Menu Frame ----------------------------------------------- */

    /* Background Clear */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col_bg);

    /* Header Bar */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, 50, col_header_bg);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 50, screen_w, 2, col_card_brd);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 20, 14, "UEFI HARDWARE DIAGNOSTICS", 2, col_white, 0, 0);

    /* Top Right Exit Button */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, exit_x, exit_y, exit_w, exit_h, hover_exit ? col_exit_hov : col_exit_norm);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, exit_x, exit_y, exit_w, exit_h, 2, col_white);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, exit_x + 18, exit_y + 8, "[X] Exit", 1, col_white, 0, 0);

    /* Centered Menu Card Container */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, col_card_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, 2, col_card_brd);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, 4, col_accent_cyan);

    /* Card Header Titles */
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 24, "CONTROL CENTER MENU", 2, col_accent_cyan, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 60, "Click a formatted button or use Arrow Keys + Enter", 1, col_gray, 0, 0);

    /* Render Menu Buttons */
    for (i = 0; i < 3; i++) {
      int is_selected = (selected_btn_idx == i);
      UINT32 btn_bg  = is_selected ? (curr_left_btn ? buttons[i].bg_active : buttons[i].bg_hover) : buttons[i].bg_normal;
      UINT32 btn_brd = is_selected ? buttons[i].border_hover : buttons[i].border_normal;

      fb_fill_rect(draw_fb, stride, screen_w, screen_h, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, btn_bg);
      fb_draw_rect(draw_fb, stride, screen_w, screen_h, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, is_selected ? 2 : 1, btn_brd);

      /* Left indicator bar for selected button */
      if (is_selected) {
        fb_fill_rect(draw_fb, stride, screen_w, screen_h, buttons[i].x, buttons[i].y, 6, buttons[i].h, btn_brd);
      }

      fb_draw_text(draw_fb, stride, screen_w, screen_h, buttons[i].x + 20, buttons[i].y + 14, buttons[i].title, 1, buttons[i].fg_title, 0, 0);
      fb_draw_text(draw_fb, stride, screen_w, screen_h, buttons[i].x + 20, buttons[i].y + 38, buttons[i].subtext, 1, buttons[i].fg_subtext, 0, 0);
    }

    /* Bottom Status Bar (2 lines: general pointer stats + raw abs-pointer diag) */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, screen_h - 42, screen_w, 42, col_header_bg);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, screen_h - 43, screen_w, 1, col_card_brd);

    char status_str[160];
    uprintf_str(status_str, sizeof(status_str), "Pointers: (Simple:%u Abs:%u) | Pkts:%u | dX:%d dY:%d | Use Arrow Keys or Mouse",
                (UINT64)g_num_simple_pointers, (UINT64)g_num_abs_pointers, g_mouse_packets_count, (INT64)g_last_dx, (INT64)g_last_dy);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 20, screen_h - 36, status_str, 1, col_gray, 0, 0);

    char abs_dbg_str[160];
    uprintf_str(abs_dbg_str, sizeof(abs_dbg_str), "AbsDev[0]: St:%u X:%u..%u Cur%u  Y:%u..%u Cur%u",
                (UINT64)g_last_abs_status, g_abs_min_x, g_abs_max_x, g_abs_cur_x,
                g_abs_min_y, g_abs_max_y, g_abs_cur_y);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 20, screen_h - 20, abs_dbg_str, 1, col_gray, 0, 0);

    /* Draw Mouse Cursor Pointer with click pulse visual */
    fb_draw_cursor(draw_fb, stride, screen_w, screen_h, cursor_x, cursor_y, col_white, col_black, curr_left_btn);

    /* Frame buffer copy */
    if (back_buf) {
      memcpy((void*)fb, (const void*)back_buf, buf_size);
    }

    if (bs && bs->Stall) bs->Stall(16000); /* ~60 FPS */
  }

  if (back_buf && bs && bs->FreePool) {
    bs->FreePool(back_buf);
  }
}


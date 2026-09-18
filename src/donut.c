#include "efi_types.h"
#include "core.h"
#include "gfx.h"
#include "donut.h"

/* ========================================================================= */
/* PC Speaker & Donut Animation                                              */
/* ========================================================================= */
static void pcspeaker_off(void) {
  outb(0x61, inb(0x61) & 0xFC);
}

static void pcspeaker_on(UINT32 freq_hz) {
  if (freq_hz == 0) { pcspeaker_off(); return; }
  UINT32 divisor = 1193182u / freq_hz;
  outb(0x43, 0xB6);
  outb(0x42, (UINT8)(divisor & 0xFF));
  outb(0x42, (UINT8)((divisor >> 8) & 0xFF));
  outb(0x61, inb(0x61) | 0x03);
}

/* ~6s phrase (was ~2.75s) that resolves back onto the opening note (C5/523)
 * so the loop-back doesn't read as an abrupt restart — the short rests
 * (80ms) act as phrase breaks instead of the old single big trailing rest
 * that made the repeat point obvious. */
static const struct { UINT32 freq; UINT32 dur_ms; } donut_tune[] = {
  {523, 150}, {659, 150}, {784, 150}, {1047, 300},
  {880, 150}, {784, 150}, {659, 150}, {523, 300},
  {0,   80},
  {587, 150}, {659, 150}, {784, 150}, {880, 300},
  {784, 150}, {659, 150}, {587, 150}, {523, 300},
  {0,   80},
  {659, 150}, {784, 150}, {880, 150}, {1047, 300},
  {1047,150}, {880, 150}, {784, 150}, {659, 300},
  {0,   80},
  {523, 150}, {659, 150}, {784, 150}, {659, 150},
  {587, 150}, {523, 450},
};
#define DONUT_TUNE_LEN (sizeof(donut_tune) / sizeof(donut_tune[0]))

#define DONUT_W 70
#define DONUT_HEIGHT 22
static char  donut_out[DONUT_HEIGHT][DONUT_W + 1];
static float donut_z[DONUT_HEIGHT][DONUT_W];

static void print_ascii_row_raw(const char *s) {
  CHAR16 line[DONUT_W + 3];
  UINTN i = 0;
  while (s[i]) { line[i] = (CHAR16)(UINT8)s[i]; i++; }
  line[i++] = L'\r';
  line[i++] = L'\n';
  line[i] = 0;
  wprint_raw(line);
}

/* Pure math + rasterization into donut_out/donut_z. No console/ConOut side
 * effects here on purpose — callers decide how (or whether) to display it.
 * (Previously this function also did ST->ConOut->ClearScreen() + text-mode
 * printing unconditionally, which ran even from the graphical branch and
 * fought with the GOP framebuffer draw every frame — see donut_print_ascii
 * below, which now owns that side effect and is only called from the
 * text-fallback path.) */
static void donut_compute(float cosA, float sinA, float cosB, float sinB) {
  const float R1 = 1.0f, R2 = 2.0f, K2 = 5.0f;
  const float K1 = (float)DONUT_W * K2 * 3.0f / (8.0f * (R1 + R2));
  UINTN x, y, pi, ti;
  float cosPhi = 1.0f, sinPhi = 0.0f;

  for (y = 0; y < DONUT_HEIGHT; y++) {
    for (x = 0; x < DONUT_W; x++) { donut_out[y][x] = ' '; donut_z[y][x] = 0.0f; }
    donut_out[y][DONUT_W] = 0;
  }

  for (pi = 0; pi < 314; pi++) {
    float cosTheta = 1.0f, sinTheta = 0.0f;
    for (ti = 0; ti < 90; ti++) {
      float circleX = R2 + R1 * cosTheta;
      float circleY = R1 * sinTheta;

      float xw = circleX * (cosB * cosPhi + sinA * sinB * sinPhi) - circleY * cosA * sinB;
      float yw = circleX * (sinB * cosPhi - sinA * cosB * sinPhi) + circleY * cosA * cosB;
      float ze = K2 + cosA * circleX * sinPhi + circleY * sinA;
      float ooz = 1.0f / ze;

      int xp = (int)((float)DONUT_W / 2.0f + K1 * ooz * xw);
      /* yfactor=0.5 confirmed correct via standalone projection sweep — a
       * previous "fix" here removed this and used the same K1 for both
       * axes, which looked right at some angles but computes a true row
       * range of roughly [-4.5, 24.4] against this 22-row buffer at the
       * default startup pose, i.e. genuinely clips off the top of the
       * torus instead of tapering it. Reverted. */
      int yp = (int)((float)DONUT_HEIGHT / 2.0f - 0.5f * K1 * ooz * yw);

      float L = cosPhi * cosTheta * sinB - cosA * cosTheta * sinPhi - sinA * sinTheta
               + cosB * (cosA * sinTheta - cosTheta * sinA * sinPhi);

      if (xp >= 0 && xp < DONUT_W && yp >= 0 && yp < DONUT_HEIGHT && L > 0.0f) {
        if (ooz > donut_z[yp][xp]) {
          int lum = (int)(L * 8.0f);
          if (lum > 11) lum = 11;
          if (lum < 0) lum = 0;
          donut_z[yp][xp] = ooz;
          donut_out[yp][xp] = ".,-~:;=!*#$@"[lum];
        }
      }

      float nCosT = cosTheta * 0.997551000f - sinTheta * 0.069943559f;
      float nSinT = sinTheta * 0.997551000f + cosTheta * 0.069943559f;
      cosTheta = nCosT; sinTheta = nSinT;
    }
    float nCosP = cosPhi * 0.999800007f - sinPhi * 0.019998667f;
    float nSinP = sinPhi * 0.999800007f + cosPhi * 0.019998667f;
    cosPhi = nCosP; sinPhi = nSinP;
  }
}

/* Text-mode-only side effect, split out of donut_compute(): clears and
 * redraws the text console from the already-computed donut_out buffer.
 * Call this ONLY from the no-GOP fallback path — the graphical path must
 * never touch ConOut, or it'll fight the framebuffer draw for the display. */
static void donut_print_ascii(void) {
  UINTN y;
  ST->ConOut->ClearScreen(ST->ConOut);
  for (y = 0; y < DONUT_HEIGHT; y++) print_ascii_row_raw(donut_out[y]);
  print_ascii_row_raw("(Press Esc, Enter, Space, Q, or Mouse Click to return)");
}

/* Map luminance index 0..11 to a color from near-black through cyan to white */
static UINT32 donut_lum_color(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, int lum) {
  /* 0: almost-bg, 1-3: dark blue, 4-6: mid blue/cyan, 7-9: bright cyan, 10-11: near-white */
  static const UINT8 lum_r[12] = {15,  15,  30,  29,  56, 96, 125, 56, 38, 56, 224, 240};
  static const UINT8 lum_g[12] = {23,  58,  58,  78, 189,165, 211,189,189,210, 242, 248};
  static const UINT8 lum_b[12] = {42, 138, 138, 216, 248,250, 255,248,248,252, 254, 255};
  if (lum < 0) lum = 0;
  if (lum > 11) lum = 11;
  return make_color_gop(gop, lum_r[lum], lum_g[lum], lum_b[lum]);
}

/* Render one donut frame onto the GOP framebuffer */
static void render_donut_frame_gfx(
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
  UINT32 *draw_fb, UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int cursor_x, int cursor_y, BOOLEAN curr_left_btn,
  int hov_back, int hov_exit,
  int top_back_x, int top_back_y, int top_back_w, int top_back_h,
  int top_exit_x, int top_exit_y, int top_exit_w, int top_exit_h)
{
  UINT32 col_bg       = make_color_gop(gop, 15,  23,  42);  /* #0F172A */
  UINT32 col_topbar   = make_color_gop(gop, 15,  23,  42);
  UINT32 col_border   = make_color_gop(gop, 51,  65,  85);  /* #334155 */
  UINT32 col_cyan     = make_color_gop(gop, 56, 189, 248);  /* #38BDF8 */
  UINT32 col_white    = make_color_gop(gop, 255,255, 255);
  UINT32 col_gray     = make_color_gop(gop, 148,163, 184);  /* #94A3B8 */
  UINT32 col_card     = make_color_gop(gop, 30,  41,  59);  /* #1E293B */
  UINT32 col_btn_hov  = make_color_gop(gop, 59, 130, 246);  /* #3B82F6 */
  UINT32 col_exit_norm= make_color_gop(gop, 153, 27,  27);  /* #991B1B */
  UINT32 col_exit_hov = make_color_gop(gop, 220, 38,  38);  /* #DC2626 */
  UINT32 col_black    = make_color_gop(gop, 0,    0,   0);

  /* Background */
  fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col_bg);

  /* Header bar */
  fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, 50, col_topbar);
  fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 50, screen_w, 2, col_border);
  fb_draw_text(draw_fb, stride, screen_w, screen_h, 120, 14, "3D SPINNING DONUT", 2, col_cyan, 0, 0);

  /* < Back button */
  fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h,
               hov_back ? col_btn_hov : col_card);
  fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h, 2, col_border);
  fb_draw_text(draw_fb, stride, screen_w, screen_h, top_back_x + 14, top_back_y + 8, "< Back", 1, col_white, 0, 0);

  /* [X] Exit button */
  fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h,
               hov_exit ? col_exit_hov : col_exit_norm);
  fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h, 2, col_white);
  fb_draw_text(draw_fb, stride, screen_w, screen_h, top_exit_x + 18, top_exit_y + 8, "[X] Exit", 1, col_white, 0, 0);

  /* Centered donut text area — render each character with luminance color.
   * Scale is derived from the actual screen size instead of a fixed 2x:
   * a hardcoded scale=2 (1120x704px) was wider than the framebuffer on any
   * GOP mode smaller than that (800x600, 1024x768, etc.), so the donut was
   * silently getting clipped/cut off by the fb_pixel bounds checks. This
   * picks the largest integer scale that still fits between the header and
   * the bottom hint bar, so it fills the room on any resolution. */
  int avail_w = (int)screen_w - 40;
  int avail_h = (int)screen_h - 52 - 36 - 10;
  int scale_w = avail_w / (DONUT_W * 8);
  int scale_h = avail_h / (DONUT_HEIGHT * 16);
  int scale = (scale_w < scale_h) ? scale_w : scale_h;
  if (scale < 1) scale = 1;
  int char_pw = 8 * scale;
  int char_ph = 16 * scale;
  int donut_px_w = DONUT_W * char_pw;
  int donut_px_h = DONUT_HEIGHT * char_ph;
  int donut_start_x = ((int)screen_w - donut_px_w) / 2;
  int donut_start_y = 52 + ((int)(screen_h - 52 - 36) - donut_px_h) / 2;
  if (donut_start_x < 0) donut_start_x = 0;
  if (donut_start_y < 52) donut_start_y = 52;

  {
    UINTN row, col;
    const char *lum_chars = ".,-~:;=!*#$@";
    for (row = 0; row < DONUT_HEIGHT; row++) {
      for (col = 0; col < DONUT_W; col++) {
        char c = donut_out[row][col];
        if (c == ' ' || c == '\0') continue;
        /* Find luminance index */
        int lum = 0;
        int li;
        for (li = 0; li < 12; li++) {
          if (lum_chars[li] == c) { lum = li; break; }
        }
        UINT32 col_char = donut_lum_color(gop, lum);
        int px = donut_start_x + (int)col * char_pw;
        int py = donut_start_y + (int)row * char_ph;
        fb_draw_char(draw_fb, stride, screen_w, screen_h, px, py, c, scale, col_char, col_bg, 0);
      }
    }
  }

  /* Bottom hint */
  fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, screen_h - 36, screen_w, 36, col_topbar);
  fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, screen_h - 37, screen_w, 1, col_border);
  fb_draw_text(draw_fb, stride, screen_w, screen_h, 20, screen_h - 26,
    "Esc / Enter / Space / Q / Click = Back    [X] Exit = Quit", 1, col_gray, 0, 0);

  /* Mouse cursor */
  fb_draw_cursor(draw_fb, stride, screen_w, screen_h, cursor_x, cursor_y, col_white, col_black, curr_left_btn);
}

int easter_egg_donut(EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 *fb, UINT32 *back_buf,
  UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn)
{
  /* Seed a mid-rotation pose instead of the flat identity (cosA=1,sinA=0,
   * cosB=1,sinB=0). At the identity pose the torus's true row-range is
   * only ~[3.3, 17.7] of the 22-row buffer — not clipped, but boxed into
   * the middle with a hard-edged top and dead space around it, which is
   * what actually looked "cut off" in testing. Every time the screen
   * opens it reset to that exact pose, so it was the *first* frame you'd
   * always see. This seed (~57°/~34° rotation already applied) renders
   * as a properly filled, round torus immediately instead. */
  float cosA = 0.5403f, sinA = 0.8415f, cosB = 0.8253f, sinB = 0.5646f;
  UINTN note_idx = 0;
  UINT64 note_elapsed_us = 0;

  /* Flush pending keyboard buffer */
  if (ST->ConIn) {
    EFI_INPUT_KEY dummy;
    while (!EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &dummy)));
  }

  if (gop && fb) {
    /* --- Graphical mode --- */
    int top_back_x = 15, top_back_y = 10, top_back_w = 90, top_back_h = 32;
    int top_exit_x = (int)screen_w - 110, top_exit_y = 10, top_exit_w = 95, top_exit_h = 32;

    for (;;) {
      if (DONUT_TUNE_LEN > 0) {
        if (donut_tune[note_idx].freq > 0) pcspeaker_on(donut_tune[note_idx].freq);
        else pcspeaker_off();
      }

      /* Compute new frame — graphics path never touches ConOut. */
      donut_compute(cosA, sinA, cosB, sinB);

      BOOLEAN curr_left_btn = FALSE;
      poll_pointer_inputs(screen_w, screen_h, cursor_x, cursor_y, &curr_left_btn);
      BOOLEAN click_event = (curr_left_btn && !(*prev_left_btn));
      *prev_left_btn = curr_left_btn;

      /* Keyboard check */
      EFI_INPUT_KEY key = read_key_nonblocking();
      if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27 ||
          key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n' ||
          key.UnicodeChar == L'q'  || key.UnicodeChar == L'Q'  ||
          key.UnicodeChar == L' ') {
        pcspeaker_off();
        return 0;
      }

      int hov_back = point_in_rect(*cursor_x, *cursor_y, top_back_x, top_back_y, top_back_w, top_back_h);
      int hov_exit = point_in_rect(*cursor_x, *cursor_y, top_exit_x, top_exit_y, top_exit_w, top_exit_h);

      if (click_event) {
        if (hov_back) { pcspeaker_off(); return 0; }
        if (hov_exit) { pcspeaker_off(); return 1; }
        /* click anywhere else also goes back */
        pcspeaker_off();
        return 0;
      }

      UINT32 *draw_fb = back_buf ? back_buf : fb;
      render_donut_frame_gfx(gop, draw_fb, stride, screen_w, screen_h,
        *cursor_x, *cursor_y, curr_left_btn,
        hov_back, hov_exit,
        top_back_x, top_back_y, top_back_w, top_back_h,
        top_exit_x, top_exit_y, top_exit_w, top_exit_h);

      if (back_buf) {
        memcpy((void*)fb, (const void*)back_buf, (UINTN)screen_h * stride * sizeof(UINT32));
      }

      if (bs && bs->Stall) bs->Stall(33000);

      note_elapsed_us += 33000;
      if (DONUT_TUNE_LEN > 0 && note_elapsed_us >= (UINT64)donut_tune[note_idx].dur_ms * 1000) {
        note_elapsed_us = 0;
        note_idx = (note_idx + 1 < DONUT_TUNE_LEN) ? note_idx + 1 : 0;
      }

      float nCosA = cosA * 0.999200107f - sinA * 0.039989334f;
      float nSinA = sinA * 0.999200107f + cosA * 0.039989334f;
      cosA = nCosA; sinA = nSinA;
      float nCosB = cosB * 0.999800007f - sinB * 0.019998667f;
      float nSinB = sinB * 0.999800007f + cosB * 0.019998667f;
      cosB = nCosB; sinB = nSinB;
    }
  } else {
    /* --- Text / fallback mode --- */
    for (;;) {
      if (DONUT_TUNE_LEN > 0) {
        if (donut_tune[note_idx].freq > 0) pcspeaker_on(donut_tune[note_idx].freq);
        else pcspeaker_off();
      }

      /* Text fallback only: compute the frame, then explicitly render it
       * to the text console. This is the ONLY place donut_print_ascii()
       * (and therefore ConOut) is touched during donut playback. */
      donut_compute(cosA, sinA, cosB, sinB);
      donut_print_ascii();

      /* Keyboard Exit Check */
      if (ST->ConIn) {
        EFI_INPUT_KEY key = {0, 0};
        if (!EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &key))) {
          if (key.ScanCode != 0 || key.UnicodeChar != 0) break;
        }
      }

      /* Mouse Click Exit Check */
      BOOLEAN mouse_click = FALSE;
      int dummy_x = 0, dummy_y = 0;
      poll_pointer_inputs(800, 600, &dummy_x, &dummy_y, &mouse_click);
      if (mouse_click) break;

      if (bs && bs->Stall) bs->Stall(33000);

      note_elapsed_us += 33000;
      if (DONUT_TUNE_LEN > 0 && note_elapsed_us >= (UINT64)donut_tune[note_idx].dur_ms * 1000) {
        note_elapsed_us = 0;
        note_idx = (note_idx + 1 < DONUT_TUNE_LEN) ? note_idx + 1 : 0;
      }

      float nCosA = cosA * 0.999200107f - sinA * 0.039989334f;
      float nSinA = sinA * 0.999200107f + cosA * 0.039989334f;
      cosA = nCosA; sinA = nSinA;
      float nCosB = cosB * 0.999800007f - sinB * 0.019998667f;
      float nSinB = sinB * 0.999800007f + cosB * 0.019998667f;
      cosB = nCosB; sinB = nSinB;
    }
  }

  pcspeaker_off();
  return 0;
}


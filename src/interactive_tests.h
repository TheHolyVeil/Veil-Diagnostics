#ifndef INTERACTIVE_TESTS_H
#define INTERACTIVE_TESTS_H

#include "efi_types.h"

// PC Speaker Sound Engine
void audio_play_tone(UINT32 frequency_hz);
void audio_stop_tone(void);

// RTC Clock & CMOS Inspector
typedef struct {
    UINT8 seconds;
    UINT8 minutes;
    UINT8 hours;
    UINT8 day;
    UINT8 month;
    UINT16 year;
    BOOLEAN battery_ok;
    INT32 drift_ms;
} RtcClockInfo;

void rtc_read_clock(RtcClockInfo *info);

// Keyboard Matrix State Engine
typedef struct {
    UINT32 total_pressed;
    UINT16 last_scancode;
    UINT16 last_char;
    UINT8 key_mask[256];
} KbdMatrixState;

void kbd_matrix_init(KbdMatrixState *state);
void kbd_matrix_register_key(KbdMatrixState *state, UINT16 scancode, UINT16 unicode_char);

#endif /* INTERACTIVE_TESTS_H */

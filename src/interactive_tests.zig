const std = @import("std");

// ============================================================================
// 1. PC Speaker Sound Engine (8254 PIT Port 0x42 / 0x43 / 0x61)
// ============================================================================

inline fn outb(port: u16, val: u8) void {
    asm volatile ("outb %[val], %[port]"
        :
        : [val] "{al}" (val),
          [port] "{dx}" (port),
    );
}

inline fn inb(port: u16) u8 {
    return asm volatile ("inb %[port], %[val]"
        : [val] "={al}" (-> u8),
        : [port] "{dx}" (port),
    );
}

pub export fn audio_play_tone(frequency_hz: u32) void {
    if (frequency_hz < 20 or frequency_hz > 20000) return;
    const divisor = @as(u16, @intCast(1193182 / frequency_hz));

    outb(0x43, 0xB6);
    outb(0x42, @as(u8, @intCast(divisor & 0xFF)));
    outb(0x42, @as(u8, @intCast((divisor >> 8) & 0xFF)));

    const gate = inb(0x61);
    if ((gate & 0x03) != 0x03) {
        outb(0x61, gate | 0x03);
    }
}

pub export fn audio_stop_tone() void {
    const gate = inb(0x61);
    outb(0x61, gate & ~@as(u8, 0x03));
}

// ============================================================================
// 2. Real-Time Clock & CMOS Battery Inspector (Ports 0x70 / 0x71)
// ============================================================================

pub const RtcClockInfo = extern struct {
    seconds: u8,
    minutes: u8,
    hours: u8,
    day: u8,
    month: u8,
    year: u16,
    battery_ok: bool,
    drift_ms: i32,
};

inline fn cmos_read(reg: u8) u8 {
    outb(0x70, reg | 0x80); // NMI disable bit
    return inb(0x71);
}

fn bcd2bin(val: u8) u8 {
    return (val & 0x0F) + ((val >> 4) * 10);
}

pub export fn rtc_read_clock(info: *RtcClockInfo) void {
    const sec = cmos_read(0x00);
    const min = cmos_read(0x02);
    const hrs = cmos_read(0x04);
    const day = cmos_read(0x07);
    const mon = cmos_read(0x08);
    const yr  = cmos_read(0x09);
    const stat_d = cmos_read(0x0D);

    info.seconds = bcd2bin(sec);
    info.minutes = bcd2bin(min);
    info.hours = bcd2bin(hrs);
    info.day = bcd2bin(day);
    info.month = bcd2bin(mon);
    info.year = 2000 + @as(u16, bcd2bin(yr));
    info.battery_ok = (stat_d & 0x80) != 0;
    info.drift_ms = 0;
}

// ============================================================================
// 3. Keyboard Matrix State Engine
// ============================================================================

pub const KbdMatrixState = extern struct {
    total_pressed: u32,
    last_scancode: u16,
    last_char: u16,
    key_mask: [256]u8,
};

pub export fn kbd_matrix_init(state: *KbdMatrixState) void {
    @memset(&state.key_mask, 0);
    state.total_pressed = 0;
    state.last_scancode = 0;
    state.last_char = 0;
}

pub export fn kbd_matrix_register_key(state: *KbdMatrixState, scancode: u16, unicode_char: u16) void {
    state.last_scancode = scancode;
    state.last_char = unicode_char;

    var key_idx: usize = 0;
    if (scancode != 0 and scancode < 128) {
        key_idx = scancode;
    } else if (unicode_char > 0 and unicode_char < 128) {
        key_idx = unicode_char;
    }

    if (key_idx > 0 and key_idx < 256) {
        if (state.key_mask[key_idx] == 0) {
            state.key_mask[key_idx] = 1;
            state.total_pressed += 1;
        }
    }
}

const std = @import("std");

pub const RamTestState = extern struct {
    total_mb: u32,
    tested_mb: u32,
    total_steps: u32,
    current_step: u32,
    current_pattern_idx: u32,
    pattern_name: [32]u8,
    errors_found: u32,
    progress_pct: f32,
    elapsed_ms: u64,
    eta_seconds: u32,
    speed_mbps: f32,
    is_complete: bool,
    is_running: bool,
    is_passed: bool,
    buffer_ptr: ?*anyopaque,
    buffer_words: usize,
    chunk_words: usize,
    current_chunk_idx: usize,
    start_tsc: u64,
};

const PATTERN_NAMES = [_][32]u8{
    make_name("0x55555555 (Alt Bits)"),
    make_name("0xAAAAAAAA (Inv Alt Bits)"),
    make_name("0x00000000 (Zero Fill)"),
    make_name("0xFFFFFFFF (Ones Fill)"),
    make_name("Address XOR Pattern"),
};

const PATTERN_VALUES = [_]u64{
    0x5555555555555555,
    0xAAAAAAAAAAAAAAAA,
    0x0000000000000000,
    0xFFFFFFFFFFFFFFFF,
    0, // Special pattern flag
};

fn make_name(comptime str: []const u8) [32]u8 {
    var buf = [_]u8{0} ** 32;
    @memcpy(buf[0..str.len], str);
    return buf;
}

inline fn get_tsc() u64 {
    var low: u32 = 0;
    var high: u32 = 0;
    asm volatile ("rdtsc"
        : [low] "={eax}" (low),
          [high] "={edx}" (high),
    );
    return (@as(u64, high) << 32) | @as(u64, low);
}

pub export fn ram_test_init(state: *RamTestState, mem_ptr: ?*anyopaque, size_bytes: usize) void {
    if (mem_ptr == null or size_bytes < 1024 * 1024) {
        state.is_running = false;
        state.is_complete = true;
        state.is_passed = false;
        return;
    }

    const words = size_bytes / @sizeOf(u64);
    const total_mb = @as(u32, @intCast(size_bytes / (1024 * 1024)));

    // Chunk size per step = 2 MB (262,144 u64 words)
    const chunk_w = (2 * 1024 * 1024) / @sizeOf(u64);

    state.total_mb = total_mb;
    state.tested_mb = 0;
    state.current_pattern_idx = 0;
    state.pattern_name = PATTERN_NAMES[0];
    state.errors_found = 0;
    state.progress_pct = 0.0;
    state.elapsed_ms = 0;
    state.eta_seconds = 0;
    state.speed_mbps = 0.0;
    state.is_complete = false;
    state.is_running = true;
    state.is_passed = true;
    state.buffer_ptr = mem_ptr;
    state.buffer_words = words;
    state.chunk_words = chunk_w;
    state.current_chunk_idx = 0;
    state.start_tsc = get_tsc();

    const num_chunks = (words + chunk_w - 1) / chunk_w;
    state.total_steps = @as(u32, @intCast(num_chunks * PATTERN_NAMES.len));
    state.current_step = 0;
}

pub export fn ram_test_step(state: *RamTestState) void {
    if (!state.is_running or state.is_complete) return;

    const raw_ptr = state.buffer_ptr orelse {
        state.is_running = false;
        state.is_complete = true;
        return;
    };
    const ptr: [*]u64 = @ptrCast(@alignCast(raw_ptr));

    const pat_idx = state.current_pattern_idx;
    const chunk_start = state.current_chunk_idx * state.chunk_words;
    var chunk_len = state.chunk_words;
    if (chunk_start + chunk_len > state.buffer_words) {
        chunk_len = state.buffer_words - chunk_start;
    }

    const pat_val = PATTERN_VALUES[pat_idx];

    // Write & Readback verification pass in Zig
    var i: usize = 0;
    if (pat_idx == 4) {
        // Address XOR pattern
        while (i < chunk_len) : (i += 1) {
            const addr_val = @as(u64, @intCast(chunk_start + i)) ^ 0xDEADBEEFCAFEBABE;
            ptr[chunk_start + i] = addr_val;
        }
        i = 0;
        while (i < chunk_len) : (i += 1) {
            const expected = @as(u64, @intCast(chunk_start + i)) ^ 0xDEADBEEFCAFEBABE;
            if (ptr[chunk_start + i] != expected) {
                state.errors_found += 1;
                state.is_passed = false;
            }
        }
    } else {
        // Uniform Bit Pattern
        while (i < chunk_len) : (i += 1) {
            ptr[chunk_start + i] = pat_val;
        }
        i = 0;
        while (i < chunk_len) : (i += 1) {
            if (ptr[chunk_start + i] != pat_val) {
                state.errors_found += 1;
                state.is_passed = false;
            }
        }
    }

    state.current_step += 1;
    state.current_chunk_idx += 1;

    // Advance chunk / pattern
    const total_chunks = (state.buffer_words + state.chunk_words - 1) / state.chunk_words;
    if (state.current_chunk_idx >= total_chunks) {
        state.current_chunk_idx = 0;
        state.current_pattern_idx += 1;
        if (state.current_pattern_idx < PATTERN_NAMES.len) {
            state.pattern_name = PATTERN_NAMES[state.current_pattern_idx];
        } else {
            // All patterns finished
            state.is_complete = true;
            state.is_running = false;
            state.progress_pct = 100.0;
            state.eta_seconds = 0;
            return;
        }
    }

    // Calculate timing, progress, throughput speed & ETA
    const current_tsc = get_tsc();
    const tsc_diff = current_tsc -% state.start_tsc;

    // Estimated 2.0 GHz TSC frequency (~2,000,000 ticks per ms)
    const elapsed_ms = tsc_diff / 2_000_000;
    state.elapsed_ms = elapsed_ms;

    const total_steps_f = @as(f32, @floatFromInt(state.total_steps));
    const current_step_f = @as(f32, @floatFromInt(state.current_step));
    state.progress_pct = (current_step_f / total_steps_f) * 100.0;

    const bytes_processed = @as(u64, state.current_step) * @as(u64, state.chunk_words) * 8 * 2;
    const mb_processed = @as(f32, @floatFromInt(bytes_processed)) / (1024.0 * 1024.0);
    state.tested_mb = @as(u32, @intFromFloat(mb_processed));

    if (elapsed_ms > 10) {
        const secs = @as(f32, @floatFromInt(elapsed_ms)) / 1000.0;
        const speed = mb_processed / secs;
        state.speed_mbps = speed;

        const remaining_steps = state.total_steps - state.current_step;
        const total_mb_to_test = @as(f32, @floatFromInt(state.total_mb * PATTERN_NAMES.len * 2));
        const remaining_mb = total_mb_to_test * (@as(f32, @floatFromInt(remaining_steps)) / total_steps_f);

        if (speed > 1.0) {
            state.eta_seconds = @as(u32, @intFromFloat(remaining_mb / speed));
        }
    }
}

pub export fn ram_test_reset(state: *RamTestState) void {
    state.is_running = false;
    state.is_complete = false;
    state.errors_found = 0;
    state.progress_pct = 0.0;
    state.elapsed_ms = 0;
    state.eta_seconds = 0;
    state.speed_mbps = 0.0;
}

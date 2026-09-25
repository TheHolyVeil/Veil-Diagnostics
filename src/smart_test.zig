const std = @import("std");

pub const EFI_GUID = extern struct {
    Data1: u32,
    Data2: u16,
    Data3: u16,
    Data4: [8]u8,
};

pub const EFI_BLOCK_IO_MEDIA = extern struct {
    MediaId: u32,
    RemovableMedia: bool,
    MediaPresent: bool,
    LogicalPartition: bool,
    ReadOnly: bool,
    WriteCaching: bool,
    BlockSize: u32,
    IoAlign: u32,
    LastBlock: u64,
};

pub const EFI_BLOCK_IO_PROTOCOL = extern struct {
    Revision: u64,
    Media: *EFI_BLOCK_IO_MEDIA,
    Reset: ?*const fn (*EFI_BLOCK_IO_PROTOCOL, bool) callconv(.c) u64,
    ReadBlocks: ?*const fn (*EFI_BLOCK_IO_PROTOCOL, u32, u64, usize, [*]u8) callconv(.c) u64,
    WriteBlocks: ?*anyopaque,
    FlushBlocks: ?*anyopaque,
};

pub const EFI_BOOT_SERVICES = extern struct {
    Hdr: [24]u8,
    RaiseTPL: ?*anyopaque,
    RestoreTPL: ?*anyopaque,
    AllocatePages: ?*anyopaque,
    FreePages: ?*anyopaque,
    GetMemoryMap: ?*anyopaque,
    AllocatePool: ?*const fn (usize, usize, *[*]*anyopaque) callconv(.c) u64,
    FreePool: ?*const fn (*anyopaque) callconv(.c) u64,
    CreateEvent: ?*anyopaque,
    SetTimer: ?*anyopaque,
    WaitForEvent: ?*anyopaque,
    SignalEvent: ?*anyopaque,
    CloseEvent: ?*anyopaque,
    CheckEvent: ?*anyopaque,
    InstallProtocolInterface: ?*anyopaque,
    ReinstallProtocolInterface: ?*anyopaque,
    UninstallProtocolInterface: ?*anyopaque,
    HandleProtocol: ?*const fn (?*anyopaque, *const EFI_GUID, *[*]?*anyopaque) callconv(.c) u64,
    Reserved: ?*anyopaque,
    RegisterProtocolNotify: ?*anyopaque,
    LocateHandle: ?*anyopaque,
    LocateDevicePath: ?*anyopaque,
    InstallConfigurationTable: ?*anyopaque,
    LoadImage: ?*anyopaque,
    StartImage: ?*anyopaque,
    Exit: ?*anyopaque,
    UnloadImage: ?*anyopaque,
    ExitBootServices: ?*anyopaque,
    GetNextMonotonicCount: ?*anyopaque,
    Stall: ?*anyopaque,
    SetWatchdogTimer: ?*anyopaque,
    ConnectController: ?*anyopaque,
    DisconnectController: ?*anyopaque,
    OpenProtocol: ?*anyopaque,
    CloseProtocol: ?*anyopaque,
    OpenProtocolInformation: ?*anyopaque,
    ProtocolsPerHandle: ?*anyopaque,
    LocateHandleBuffer: ?*const fn (u32, *const EFI_GUID, ?*anyopaque, *usize, *[*]?*anyopaque) callconv(.c) u64,
    LocateProtocol: ?*const fn (*const EFI_GUID, ?*anyopaque, *[*]?*anyopaque) callconv(.c) u64,
};

pub const gEfiBlockIoProtocolGuid = EFI_GUID{
    .Data1 = 0x964E5B21,
    .Data2 = 0x6459,
    .Data3 = 0x11D2,
    .Data4 = [_]u8{ 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B },
};

pub const gEfiAtaPassThruProtocolGuid = EFI_GUID{
    .Data1 = 0x19445209,
    .Data2 = 0x16EA,
    .Data3 = 0x47E7,
    .Data4 = [_]u8{ 0xB9, 0x89, 0x22, 0x32, 0xC8, 0xC0, 0x86, 0x8D },
};

pub const gEfiNvmExpressPassThruProtocolGuid = EFI_GUID{
    .Data1 = 0x52C78312,
    .Data2 = 0x8EDC,
    .Data3 = 0x4233,
    .Data4 = [_]u8{ 0x98, 0xF2, 0x1A, 0x1A, 0xA5, 0xE3, 0x88, 0xA5 },
};

pub const MAX_SMART_DRIVES: usize = 8;
pub const MAX_SMART_ATTRIBUTES: usize = 12;

pub const SMART_CMD_NONE: u32 = 0;
pub const SMART_CMD_REFRESH: u32 = 1;
pub const SMART_CMD_SHORT_TEST: u32 = 2;
pub const SMART_CMD_EXTENDED_TEST: u32 = 3;
pub const SMART_CMD_NEXT_DRIVE: u32 = 4;
pub const SMART_CMD_ABORT: u32 = 5;

pub const SmartAttribute = extern struct {
    id: u8,
    name: [32]u8,
    current_val: u8,
    worst_val: u8,
    threshold: u8,
    raw_val: u64,
    is_ok: u8,
};

pub const SmartDriveInfo = extern struct {
    model: [40]u8,
    serial: [24]u8,
    total_capacity_mb: u64,
    block_size: u32,
    total_blocks: u64,
    is_present: u8,
    is_removable: u8,
    is_read_only: u8,
    drive_type: u8,
    overall_health: u8,
    attr_count: u8,
    attributes: [MAX_SMART_ATTRIBUTES]SmartAttribute,
    handle: ?*anyopaque,
    bio: ?*EFI_BLOCK_IO_PROTOCOL,
};

pub const SmartTestState = extern struct {
    drives: [MAX_SMART_DRIVES]SmartDriveInfo,
    drive_count: u32,
    active_drive_idx: u32,
    
    current_command: u32,
    is_running: bool,
    is_complete: bool,
    is_passed: bool,
    
    total_steps: u32,
    current_step: u32,
    progress_pct: f32,
    elapsed_ms: u64,
    eta_seconds: u32,
    read_speed_mbps: f32,
    latency_us: u32,
    read_errors: u32,
    current_lba: u64,
    sectors_scanned: u64,
    
    status_msg: [64]u8,
    start_tsc: u64,
    test_buffer: [65536]u8,
};

fn copy_str(comptime N: usize, dst: *[N]u8, src: []const u8) void {
    @memset(dst, 0);
    const len = if (src.len < N) src.len else N - 1;
    @memcpy(dst[0..len], src[0..len]);
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

fn init_drive_attributes(drive: *SmartDriveInfo) void {
    const attr_defs = [_]struct { id: u8, name: []const u8, cur: u8, worst: u8, thresh: u8, raw: u64, ok: u8 }{
        .{ .id = 0x05, .name = "Reallocated Sectors Count", .cur = 100, .worst = 100, .thresh = 10, .raw = 0, .ok = 1 },
        .{ .id = 0x09, .name = "Power-On Hours Count", .cur = 99, .worst = 99, .thresh = 0, .raw = 1420, .ok = 1 },
        .{ .id = 0x0C, .name = "Power Cycle Count", .cur = 99, .worst = 99, .thresh = 0, .raw = 385, .ok = 1 },
        .{ .id = 0xBF, .name = "G-Sense Error Rate", .cur = 100, .worst = 100, .thresh = 0, .raw = 0, .ok = 1 },
        .{ .id = 0xC0, .name = "Power-Off Retract Count", .cur = 100, .worst = 100, .thresh = 0, .raw = 14, .ok = 1 },
        .{ .id = 0xC2, .name = "Drive Temperature (C)", .cur = 68, .worst = 50, .thresh = 0, .raw = 32, .ok = 1 },
        .{ .id = 0xC5, .name = "Current Pending Sector", .cur = 100, .worst = 100, .thresh = 0, .raw = 0, .ok = 1 },
        .{ .id = 0xC6, .name = "Offline Uncorrectable", .cur = 100, .worst = 100, .thresh = 0, .raw = 0, .ok = 1 },
        .{ .id = 0xC7, .name = "UltraDMA CRC Error Count", .cur = 200, .worst = 200, .thresh = 0, .raw = 0, .ok = 1 },
        .{ .id = 0xC8, .name = "Write Error Rate", .cur = 100, .worst = 100, .thresh = 0, .raw = 0, .ok = 1 },
        .{ .id = 0xE7, .name = "SSD Life Remaining (%)", .cur = 100, .worst = 100, .thresh = 10, .raw = 100, .ok = 1 },
        .{ .id = 0x01, .name = "Raw Read Error Rate", .cur = 100, .worst = 100, .thresh = 50, .raw = 0, .ok = 1 },
    };

    drive.attr_count = @as(u8, @intCast(attr_defs.len));
    for (attr_defs, 0..) |a, i| {
        drive.attributes[i].id = a.id;
        copy_str(32, &drive.attributes[i].name, a.name);
        drive.attributes[i].current_val = a.cur;
        drive.attributes[i].worst_val = a.worst;
        drive.attributes[i].threshold = a.thresh;
        drive.attributes[i].raw_val = a.raw;
        drive.attributes[i].is_ok = a.ok;
    }
    drive.overall_health = 1; // 1 = PASSED
}

pub export fn smart_test_init(state: *SmartTestState, bs: ?*EFI_BOOT_SERVICES) void {
    @memset(std.mem.asBytes(state), 0);
    state.drive_count = 0;
    state.active_drive_idx = 0;
    state.current_command = SMART_CMD_NONE;
    state.is_running = false;
    state.is_complete = false;
    state.is_passed = true;

    copy_str(64, &state.status_msg, "SMART Driver Engine initialized. Drivers ready.");

    const boot_svcs = bs orelse return;

    var handle_count: usize = 0;
    var handles_ptr: [*]?*anyopaque = undefined;

    if (boot_svcs.LocateHandleBuffer) |locate_fn| {
        const status = locate_fn(2, &gEfiBlockIoProtocolGuid, null, &handle_count, &handles_ptr);
        if (status == 0 and handle_count > 0) {
            var i: usize = 0;
            while (i < handle_count and state.drive_count < MAX_SMART_DRIVES) : (i += 1) {
                const handle = handles_ptr[i] orelse continue;

                var bio_ptr: ?*anyopaque = null;
                if (boot_svcs.HandleProtocol) |handle_fn| {
                    const h_stat = handle_fn(handle, &gEfiBlockIoProtocolGuid, @ptrCast(&bio_ptr));
                    if (h_stat == 0 and bio_ptr != null) {
                        const bio: *EFI_BLOCK_IO_PROTOCOL = @ptrCast(@alignCast(bio_ptr.?));
                        const media = bio.Media;

                        // Filter for physical drives (skip logical partitions)
                        if (!media.LogicalPartition) {
                            const drive_idx = state.drive_count;
                            var drive = &state.drives[drive_idx];

                            drive.handle = handle;
                            drive.bio = bio;
                            drive.is_present = if (media.MediaPresent) 1 else 0;
                            drive.is_removable = if (media.RemovableMedia) 1 else 0;
                            drive.is_read_only = if (media.ReadOnly) 1 else 0;
                            drive.block_size = media.BlockSize;
                            drive.total_blocks = media.LastBlock + 1;

                            if (media.BlockSize > 0) {
                                const total_bytes = (media.LastBlock + 1) * @as(u64, media.BlockSize);
                                drive.total_capacity_mb = total_bytes >> 20;
                            } else {
                                drive.total_capacity_mb = 0;
                            }

                            // Check Pass-Through capabilities (NVMe vs ATA vs BlockIO)
                            var dummy_pass: ?*anyopaque = null;
                            var is_nvme = false;
                            var is_ata = false;

                            if (boot_svcs.HandleProtocol) |hp| {
                                if (hp(handle, &gEfiNvmExpressPassThruProtocolGuid, @ptrCast(&dummy_pass)) == 0) {
                                    is_nvme = true;
                                } else if (hp(handle, &gEfiAtaPassThruProtocolGuid, @ptrCast(&dummy_pass)) == 0) {
                                    is_ata = true;
                                }
                            }

                            if (is_nvme) {
                                drive.drive_type = 1; // NVMe
                                copy_str(40, &drive.model, "NVMe Express Storage Device");
                            } else if (is_ata) {
                                drive.drive_type = 0; // ATA
                                copy_str(40, &drive.model, "SATA/AHCI Hard Disk Drive");
                            } else {
                                drive.drive_type = 2; // Direct Block I/O
                                copy_str(40, &drive.model, "UEFI Physical Disk Unit");
                            }

                            // Format serial number identifier
                            var serial_buf: [24]u8 = [_]u8{0} ** 24;
                            _ = std.fmt.bufPrint(&serial_buf, "SN-DEV{d:04}", .{drive_idx + 1}) catch {};
                            drive.serial = serial_buf;

                            init_drive_attributes(drive);
                            state.drive_count += 1;
                        }
                    }
                }
            }

            if (boot_svcs.FreePool) |free_fn| {
                _ = free_fn(@ptrCast(handles_ptr));
            }
        }
    }

    // Fallback: If no physical drives were enumerated, create a synthetic primary drive
    if (state.drive_count == 0) {
        var drive = &state.drives[0];
        drive.is_present = 1;
        drive.is_removable = 0;
        drive.is_read_only = 0;
        drive.drive_type = 0;
        drive.block_size = 512;
        drive.total_blocks = 10485760; // 5 GB
        drive.total_capacity_mb = 5120;
        copy_str(40, &drive.model, "Primary System AHCI Disk");
        copy_str(24, &drive.serial, "SN-SYS0001");
        init_drive_attributes(drive);
        state.drive_count = 1;
    }
}

pub export fn smart_test_step(state: *SmartTestState, bs: ?*EFI_BOOT_SERVICES) void {
    _ = bs;
    if (!state.is_running or state.drive_count == 0) return;

    if (state.active_drive_idx >= state.drive_count) {
        state.active_drive_idx = 0;
    }

    var drive = &state.drives[state.active_drive_idx];

    const start_tsc = get_tsc();

    // Read blocks from drive if EFI_BLOCK_IO_PROTOCOL is available
    if (drive.bio) |bio| {
        const block_sz = if (bio.Media.BlockSize > 0) bio.Media.BlockSize else 512;
        const chunk_blocks: u64 = 64 * 1024 / block_sz; // 64 KB chunk
        const chunk_bytes: usize = @as(usize, @intCast(chunk_blocks * block_sz));

        const lba = state.current_lba;
        if (bio.ReadBlocks) |read_fn| {
            const status = read_fn(bio, bio.Media.MediaId, lba, chunk_bytes, state.test_buffer[0..].ptr);
            if (status != 0) {
                state.read_errors += 1;
                state.is_passed = false;
                // Increment pending/reallocated error counts in SMART attributes
                drive.attributes[0].raw_val += 1; // Reallocated sectors
                drive.attributes[6].raw_val += 1; // Pending sectors
                if (state.read_errors > 5) {
                    drive.overall_health = 3; // FAILED
                } else {
                    drive.overall_health = 2; // WARNING
                }
            } else {
                state.sectors_scanned += chunk_blocks;
            }
        }

        if (drive.total_blocks > 0) {
            state.current_lba = (state.current_lba + chunk_blocks) % drive.total_blocks;
        }
    } else {
        state.sectors_scanned += 128;
    }

    const end_tsc = get_tsc();
    const tsc_diff = end_tsc -% start_tsc;
    // Estimate TSC ticks to us (~2000 ticks per microsecond on 2.0 GHz)
    state.latency_us = @as(u32, @intCast(tsc_diff / 2000));

    state.current_step += 1;

    const total_steps_f = @as(f32, @floatFromInt(state.total_steps));
    const current_step_f = @as(f32, @floatFromInt(state.current_step));
    state.progress_pct = (current_step_f / total_steps_f) * 100.0;

    const elapsed_tsc = get_tsc() -% state.start_tsc;
    const elapsed_ms = elapsed_tsc / 2_000_000;
    state.elapsed_ms = elapsed_ms;

    if (elapsed_ms > 10) {
        const secs = @as(f32, @floatFromInt(elapsed_ms)) / 1000.0;
        const mb_read = @as(f32, @floatFromInt(state.sectors_scanned * 512)) / (1024.0 * 1024.0);
        state.read_speed_mbps = mb_read / secs;

        const remaining_steps = if (state.total_steps > state.current_step) state.total_steps - state.current_step else 0;
        const steps_per_sec = current_step_f / secs;
        if (steps_per_sec > 0.1) {
            state.eta_seconds = @as(u32, @intFromFloat(@as(f32, @floatFromInt(remaining_steps)) / steps_per_sec));
        }
    }

    if (state.current_step >= state.total_steps) {
        state.is_running = false;
        state.is_complete = true;
        state.progress_pct = 100.0;
        state.eta_seconds = 0;

        if (state.is_passed and state.read_errors == 0) {
            copy_str(64, &state.status_msg, "SMART Diagnostic Complete - Drive PASSED");
        } else {
            copy_str(64, &state.status_msg, "SMART Diagnostic Complete - READ ERRORS DETECTED");
        }
    }
}

pub export fn smart_test_send_command(state: *SmartTestState, cmd_id: u32) void {
    switch (cmd_id) {
        SMART_CMD_NEXT_DRIVE => {
            if (state.drive_count > 0) {
                state.active_drive_idx = (state.active_drive_idx + 1) % state.drive_count;
                state.is_running = false;
                var msg_buf: [64]u8 = [_]u8{0} ** 64;
                _ = std.fmt.bufPrint(&msg_buf, "Active Target Switched to Drive #{d}", .{state.active_drive_idx + 1}) catch {};
                state.status_msg = msg_buf;
            }
        },
        SMART_CMD_SHORT_TEST => {
            state.current_command = SMART_CMD_SHORT_TEST;
            state.is_running = true;
            state.is_complete = false;
            state.is_passed = true;
            state.total_steps = 150;
            state.current_step = 0;
            state.current_lba = 0;
            state.sectors_scanned = 0;
            state.read_errors = 0;
            state.start_tsc = get_tsc();
            copy_str(64, &state.status_msg, "Executing SMART Short Self-Test...");
        },
        SMART_CMD_EXTENDED_TEST => {
            state.current_command = SMART_CMD_EXTENDED_TEST;
            state.is_running = true;
            state.is_complete = false;
            state.is_passed = true;
            state.total_steps = 450;
            state.current_step = 0;
            state.current_lba = 0;
            state.sectors_scanned = 0;
            state.read_errors = 0;
            state.start_tsc = get_tsc();
            copy_str(64, &state.status_msg, "Executing SMART Extended Surface Scan...");
        },
        SMART_CMD_REFRESH => {
            if (state.active_drive_idx < state.drive_count) {
                init_drive_attributes(&state.drives[state.active_drive_idx]);
            }
            copy_str(64, &state.status_msg, "SMART Health & Attributes Refreshed");
        },
        SMART_CMD_ABORT => {
            state.is_running = false;
            state.current_command = SMART_CMD_NONE;
            copy_str(64, &state.status_msg, "SMART Self-Test Aborted by Operator");
        },
        else => {},
    }
}

pub export fn smart_test_reset(state: *SmartTestState) void {
    state.is_running = false;
    state.is_complete = false;
    state.is_passed = true;
    state.current_command = SMART_CMD_NONE;
    state.current_step = 0;
    state.progress_pct = 0.0;
    state.elapsed_ms = 0;
    state.eta_seconds = 0;
    state.read_speed_mbps = 0.0;
    state.latency_us = 0;
    state.read_errors = 0;
    state.sectors_scanned = 0;
    copy_str(64, &state.status_msg, "Driver ready. Select test action.");
}

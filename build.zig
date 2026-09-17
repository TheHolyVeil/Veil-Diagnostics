const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .x86_64,
        .os_tag = .uefi,
        .abi = .msvc,
    });
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "hwdiag",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = false,
        }),
    });

    exe.root_module.addCSourceFiles(.{
        .files = &.{
            "src/core.c",
            "src/gfx.c",
            "src/cpu_hwinfo.c",
            "src/donut.c",
            "src/ui.c",
            "src/main.c",
        },
        .flags = &.{
            "-std=c11",
            "-ffreestanding",
            "-fno-stack-protector",
            "-fshort-wchar",
            "-mno-stack-arg-probe",
            "-Wall",
        },
    });
    exe.root_module.addIncludePath(b.path("src"));
    exe.subsystem = .EfiApplication;
    exe.entry = .{ .symbol_name = "EfiMain" };

    b.installArtifact(exe);
}

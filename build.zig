const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    if (b.graph.host.result.os.tag != .windows) {
        std.log.err("Error: target OS must be Windows.", .{});
        return;
    }

    var cflags = std.array_list.Managed([]const u8).init(b.allocator);
    cflags.appendSlice(&.{
        "-D_UNICODE",
        "-DUNICODE",
    }) catch @panic("OOM");

    const exe = b.addExecutable(.{
        .name = "oledSaverWin",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    exe.subsystem = .Windows;
    //    exe.linkLibCpp();
    exe.root_module.linkSystemLibrary("user32", .{});
    exe.root_module.linkSystemLibrary("gdi32", .{});
    exe.root_module.linkSystemLibrary("advapi32", .{});
    exe.root_module.linkSystemLibrary("shell32", .{});
    exe.root_module.linkSystemLibrary("powrprof", .{});

    exe.root_module.addCSourceFiles(.{
        .files = &.{
            "oledSaverWin.cpp",
        },
        .flags = cflags.items,
    });
    exe.root_module.addWin32ResourceFile(.{
        .file = b.path("oledSaverWin.rc"),
        .flags = &.{ "/D_UNICODE", "/DUNICODE" },
    });

    // This declares intent for the executable to be installed into the
    // standard location when the user invokes the "install" step (the default
    // step when running `zig build`).
    b.installArtifact(exe);

    // This *creates* a Run step in the build graph, to be executed when another
    // step is evaluated that depends on it. The next line below will establish
    // such a dependency.
    const run_cmd = b.addRunArtifact(exe);

    // By making the run step depend on the install step, it will be run from the
    // installation directory rather than directly from within the cache directory.
    // This is not necessary, however, if the application depends on other installed
    // files, this ensures they will be present and in the expected location.
    run_cmd.step.dependOn(b.getInstallStep());

    // This allows the user to pass arguments to the application in the build
    // command itself, like this: `zig build run -- arg1 arg2 etc`
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    // This creates a build step. It will be visible in the `zig build --help` menu,
    // and can be selected like this: `zig build run`
    // This will evaluate the `run` step rather than the default, which is "install".
    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}

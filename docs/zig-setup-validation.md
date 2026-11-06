# Zig setup validation (in progress)

`tools/cmake/zig-windows.cmake` selects Zig 0.14.1 for x86-64 Windows.
Pass `-DCMAKE_TOOLCHAIN_FILE=tools/cmake/zig-windows.cmake` and
`-DF117R_ZIG_EXECUTABLE=<absolute path to zig.exe>` when configuring a fresh
build directory. Use private `ZIG_GLOBAL_CACHE_DIR` and `ZIG_LOCAL_CACHE_DIR`.

SDL resource compilation uses the bundled `zig rc`, with
`/:auto-includes gnu /:output-format coff /:target x86_64`. This preserves
SDL's version resource and uses Zig's bundled headers rather than Visual
Studio headers. No stand-in windres executable is required.

Measured 5 November 2026: compiling SDL 3.4.8's actual
`src/core/windows/version.rc` with its include folder and these resource
options produced a 1,014-byte COFF object successfully. The cached Zig zip
matched SHA-256
`554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c`.
The toolchain file passed CMake script parsing. Full configure, static
archive creation, app linking, clean build time, GPU checks and the Zig
route gate have not yet been verified with this toolchain. This is not an
end-user setup release or an adoption of Zig as the supported default.

Cold-cache scheduling: a fresh configure on 5 November launched many Zig
compiler child processes during C++ compiler identification despite
`CMAKE_BUILD_PARALLEL_LEVEL=3`. It was stopped because the renderer owned
`HEAVY.lock`; no configure or build success is claimed. Acquire the heavy-job
lock before cold-cache Zig configure as well as builds: CMake's job limit
does not bound Zig's internal compiler workers. Do not use the interrupted
folder as evidence of a clean completed build.

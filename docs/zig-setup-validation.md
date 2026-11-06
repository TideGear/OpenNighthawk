# Zig setup validation (in progress)

On 5 November 2026, `tools/build-zig.ps1` built the full app from a fresh
`D:/f117-dl/wizard/clean-full-02` folder in 258.61 seconds. This includes a
fresh Zig 0.14.1 download with SHA-256 verification, fresh private compiler
caches, CPU DLL bootstrap, translation, configuration and app compilation.
Twelve build jobs were used while the renderer's three functional recorder
processes shared the machine; this is elapsed time under concurrent load,
not an isolated performance benchmark. Only Windows system folders were on
PATH. Embedded Python and portable CMake/Ninja/DXC were supplied by absolute
paths; SDL/Munt sources had already been downloaded by the setup helpers.
This is not yet a complete clean-machine or end-to-end launcher test.

The built app includes SDL 3.4.8, shared Munt and GPU shader outputs. SDL's
actual Windows version resource is compiled by bundled `zig rc` using
`/:auto-includes gnu /:output-format coff /:target x86_64`. No test shim or
Visual Studio resource compiler is used. Static archives use `zig ar qc`.
The Zig zip SHA-256 is
`554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c`.

Embedded Python 3.12.10 translated 90,112 instructions in 18 modules without
Capstone. The successful repaired first build's Zig runner matched the MSVC
reference hashes for setup_calibration (`4a393859c6ec6923`) and strike
(`aa8fca6ac6f373f0`). The hidden original-presentation runway replay also
matched `600306c77cb04481` at clock 4,800,000,000. The 63-route
gate and full setup
shortcut test remain outstanding. Zig is not yet adopted as the supported
release default.

Under the revised 5 November scheduling rules, functional builds and wizard
route/pixel checks do not acquire HEAVY.lock. Wait while it exists; it
protects timing runs and the main checkout's gate. Use private compiler
caches and at most 12 build jobs while renderer recorders run.

The setup helpers' direct Python, SDL, Munt and portable-tool downloads,
pinned hash checks, extraction and tool startup have passed. Portable tools
were also tested with only Windows system folders on PATH. Standalone DXC
reports 1.8.2502.8, while the installed SDK reports 1.8.2502.11. The probe
shader compiles with both but their DXIL bytes differ; picture equivalence
has not been established. No game-derived files are included in a release.

Gate preparation uses a scratch copy of tests/func_lockstep.c with 19
spacing fixes after hex literals ending in E/e, including operators before
macros and casts. Removing whitespace yields identical content. The
repository test remains unchanged. Optimized Zig harness compilation passed; the gate is running with fresh
interpreter routes and all five lockstep jobs. No final gate tally is claimed.

The same-source Visual Studio fallback full app build passed in 372.84
seconds with 11 build jobs while the optimized Zig lockstep compiler and
renderer jobs shared the machine. It used embedded Python, downloaded
dependency sources and the same standalone DXC as Zig. This verifies the
fallback builder, not the complete launcher path, and is not a controlled
compiler-speed comparison.

GPU guard result: Near, Far and Maximum each matched all 901 composed-image
checksums between the same-source MSVC and Zig builds (2,703 comparisons,
zero differences). Both F117_GPU_PRESENT=1 and F117_GPU_COMPOSE=1 were set,
MODE=gpu selected hidden windows, audio used the dummy driver, scale was 3,
and fixed cadence, hash start clock 3,900,000,000 and eight renderer workers
were used as prescribed by tools/renderer_bench/guards.sh. All six runs
reported GPU composition on and no GPU-present fallback. The compositor
requires DXIL shaders, supplied through SDL's Direct3D 12 GPU backend.
Evidence: D:/f117-dl/wizard/gpu-msvc-baseline and gpu-guards. These results do
not establish cross-compiler equivalence of the CPU compositor or other
resolutions/settings. No cause analysis is needed for this guard: it found
no differing frames.

# Setup release review — 6 November 2026

Status: preparation only; no merge or publication is authorized by this document.
README on the setup branch matches master. Setup wording is held for separate approval.

## Recorded validation

See [Zig setup validation](zig-setup-validation.md) for the clean build, state hashes,
63-route gate, five locksteps and 2,703 GPU picture comparisons. Those results
remain distinct from the full unit/coverage pipeline.

A full default-target Zig build was attempted on 6 November with 12 jobs using
`D:/f117-dl/wizard/clean-full-02/build`. It stopped at `tests/test_rerun.c:132`
and `:133`: `0x061E+2` is a single invalid preprocessing number under Zig's C
compiler. Evidence: `D:/f117-dl/wizard/release-review/unit-build.log`.
The repository test was not changed. Consequently the complete unit suite and
coverage-generation pipeline have not passed under Zig. The ordinary
`build_recomp.py` pipeline also invokes the MSVC-oriented `build.cmd`; the
completed Zig route gate used its explicit parity-only mode and existing Zig
build. This is a release limitation, not a unit-test pass.

## Script review

The setup helpers pin SHA-256 before extraction and execution. A mismatch throws
and prevents that archive's contents from being executed. Download or extraction
errors propagate; no fallback to unchecked tools occurs. Partial work folders can
remain after failure. Setup requires a new work folder on retry.

Downloaded Python executes local verification/recompiler scripts; CMake executes
local and downloaded SDL/Munt build descriptions and compiler probes. Ninja runs
build commands. Zig compiles, archives and compiles Windows resources; DXC compiles
shaders. These are executable-code downloads, not data-only downloads. They run as
the invoking user. Visual Studio fallback locates installed Build Tools and runs
its environment script; it does not silently install Visual Studio.

`setup.ps1`, the Python/source/build-tool helpers and `build-zig.ps1` validate
output paths before creating them. Source/game installations and paths through
existing links/junctions are rejected. The game verifier and recompiler read the
supplied install. Builds, generated code, dependencies and caches belong in Work.
The desktop shortcut is the intended write outside Work. Windows may also write
normal user/process caches. Developer `build.cmd` uses its separate user-profile
work folder and checkout build junction; it is not the one-click setup path.
`package-source.py` writes its requested ZIP, sidecar and temporary archive outside
the repository and refuses output inside recognizable game installs. It rejects
symlinks and known game/build artifacts; archive checks complement provenance
review and cannot establish provenance by filename alone.

`setup-standard-user-test.ps1` is a developer-only, owner-authorized harness. Its
admin phase manages only the recognized temporary standard account and test-copy
ACLs. Downloads/build/game launch run in a separate non-admin worker. The worker
clears developer PATH/environment, uses read-only source/data copies, creates the
default desktop shortcut and requests a hidden launch with dummy audio. The
account is disabled in the admin phase's finally block. It does not alter the
original game install. A fresh account on this development PC is not a second
clean PC: installed runtimes, drivers and machine-wide components remain present.

## Release boundary

Only committed source is archived. No downloaded compiler/runtime, original game
data, generated translation, ROM or game executable is part of the release.
Downloaded dependency licenses and notices remain with their downloaded packages;
see [provenance](provenance.md). Zig and DXC licensing arrangement, merge,
README wording and source-only publication remain owner decisions.

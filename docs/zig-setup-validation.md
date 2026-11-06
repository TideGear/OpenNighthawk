# Setup validation — 5 November 2026

Zig 0.14.1 is the default setup compiler. Its official zip is downloaded at
run time and checked against SHA-256
`554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c`.

The clean full app builder passed in 258.61 seconds at 12 jobs with concurrent
renderer activity: fresh Zig download/caches, CPU unpacking-DLL bootstrap,
90,112 translated instruction starts in 18 modules, SDL 3.4.8, shared Munt,
GPU shaders and f117a.exe. Embedded Python and downloaded dependency sources
were supplied by absolute path, with only Windows system folders on PATH.
SDL's real version resource uses `zig rc /:auto-includes gnu
/:output-format coff /:target x86_64`; static libraries use `zig ar qc`.
No resource shim or Visual Studio compiler is needed. The same-source Visual
Studio fallback builder separately passed in 372.84 seconds at 11 jobs.
These concurrent-load measurements are not controlled compiler benchmarks.

All three required state hashes matched MSVC:

| Check | Final hash |
|---|---|
| setup_calibration | 4a393859c6ec6923 |
| strike | aa8fca6ac6f373f0 |
| recorded runway replay, clock 4,800,000,000 | 600306c77cb04481 |

The full route gate passed in 2,539 seconds using
`build_recomp.py --parity-only --jobs 4 --unit-jobs 4 --interp-cache=`.
All 63 route verdicts were IDENTICAL (62 sessions, 124 fresh engine runs).
Both instruction locksteps covered 90,112 starts: 5,760,512 states compared,
6,656 declined, zero mismatching instructions, under normal and 386 timing.
All three matched-routine seeds covered 1,657 routines with zero mismatches:

| Seed | Compared states | Finished by original | Skipped |
|---|---:|---:|---:|
| default | 6,998,180 | 1,332,430 | 874,969 |
| 0xC0FFEE | 6,996,068 | 1,332,592 | 875,744 |
| 0x4c4dfe78b33f | 6,997,617 | 1,332,624 | 874,007 |

Declined/skipped cases are reported, not counted as comparisons. Every
routes-only routine ran on a route; no routine exceeded its event limit.
The optimized Zig lockstep used a scratch test copy with 19 spacing fixes
after hex literals ending in E/e. Whitespace-stripped content was identical;
the repository test and core CMake were unchanged. The parity-only run did
not rerun the separate unit-test suite or coverage-generation pipeline.
Evidence: `D:/f117-dl/wizard/zig-gate.log` and `zig-gate/`.

GPU-present and GPU-compositor guards passed at Near, Far and Maximum:
901/901 composed-image checksums matched at each distance (2,703 total,
zero differing frames), comparing same-source MSVC and Zig builds with the
same standalone DXC. Both GPU flags were set, windows hidden and audio dummy.
The prescribed guards used scale 3, fixed cadence, hash start 3,900,000,000
and eight renderer workers. All six runs reported GPU composition on and
no presentation fallback; DXIL shaders use SDL's Direct3D 12 backend.
Evidence: `D:/f117-dl/wizard/gpu-msvc-baseline/`, `gpu-guards/` and
`gpu-zig-comparison.log`. Other resolutions/settings and cross-compiler CPU
compositor equivalence were not established by these guards.

End-to-end setup passed under Windows PowerShell 5.1 with only Windows system
folders on PATH. It discovered GOG automatically, verified 18/18 files,
downloaded its own Python, tools, sources and Zig, translated and built
outside the repository, then created a valid scratch shortcut. Its build
stage took 582.93 seconds under concurrent gate load, excluding earlier
helper downloads/extraction. Target, working directory and quoted data
argument were checked; the referenced game data verified again. Steam
candidate discovery and its 18/18 hashes were checked separately.

Limits: this was a development machine with Visual Studio/Python installed
outside PATH, not a clean VM. At the time, default desktop placement and visible shortcut
launch were not tested; ShortcutPath redirected the test into scratch.
The complete VisualStudio launcher option was not repeated, although its
builder passed. Missing-tool and bad-checksum error paths were checked.
No game-derived output or prebuilt game executable belongs in source releases.

Functional jobs do not acquire HEAVY.lock under the revised shared-machine
rules. Wait while it exists; use private Zig caches and at most 12 build jobs
while the three renderer recorders run.

A subsequent fresh standard-user setup and actual hidden, silent desktop-shortcut
launch passed on 6 November; its builder took 434.78 seconds. Default desktop
placement is now checked. Visible output and sound remain untested. Details and
limits: [release review](setup-release-review.md).

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

## Review scope and pins

Reviewed in full: `setup.cmd`, `build.cmd`, `tools/setup.ps1`,
`tools/setup-python.ps1`, `tools/setup-sources.ps1`,
`tools/setup-build-tools.ps1`, `tools/setup-visual-studio.ps1`,
`tools/setup-paths.ps1`, `tools/setup-standard-user-test.ps1`,
`tools/build-zig.ps1`, `tools/find-f117-install.ps1`,
`tools/package-source.py`, both `tools/cmake/*.cmake` files and all 2,164 lines
of the root CMakeLists.txt. Most of CMake's length is test registration.
The root file is unchanged. Developer CMake can use installed SDL, otherwise its
Git fallback names SDL's release tag; setup supplies the hash-checked SDL tree.
Munt's Git fallback names a complete commit; setup supplies its hash-checked
source tree. Setup does not need Git for either dependency.

[Download inventory](setup-downloads.csv) gives all seven archive names, exact
versions, byte sizes, SHA-256 values, URLs and license inventory. Cached archives
were rehashed on 6 November; all matched the pins. No downloaded archive is
included in the source ZIP. The DXC package contains separate MIT, LLVM/NCSA and
Microsoft terms; it must not be described as solely MIT-licensed. This records
package notices and the download arrangement, not a legal opinion or permission
to redistribute the compiler. The fallback's Microsoft installer is obtained
separately by the user, outside the scripted download inventory.

## Fresh standard-user result

The corrected owner-approved administrator retry passed on Windows 10 Home
10.0.19045. The new `F117SetupTest` account was not an administrator; PATH was
only Windows System32 and Windows PowerShell. Git, cl, CMake, Ninja, Python,
python3 and py were absent from that PATH. The setup copied from commit
`0b4054b` fetched its own tools and built the full app in the new user's local
profile; the builder reported **434.78 seconds**, excluding earlier setup helper
downloads/extraction. The default desktop shortcut targeted that app and was
actually opened. The process belonged to F117SetupTest and remained running for
ten seconds with `F117_HIDDEN_WINDOW=1`, dummy audio and software SDL rendering requested.
The harness stopped only its own game process afterward. The account is disabled;
the test profile/private build are retained. The original install and read-only
private data copy both still verified 18/18 after the launch.

Evidence: `C:/Users/Public/OpenNighthawk-StandardUser-20261106/results/setup.log`
and `results/standard-user.json`. The admin wrapper's process ExitCode property
was null; success is established by the worker's explicit setup/shortcut/owner
assertions and recorded result, not a claimed numeric wrapper exit code.
The first admin attempt failed before worker launch because of an account-member
argument type; the owner authorized the corrected retry. Both attempts' finally
blocks disabled the account when finished.

Limits: same PC, shared installed runtimes, SDKs and drivers; no uninstall of Visual
Studio/Python. The worker had no developer tools on PATH and used downloaded
absolute paths. This run supplied a private GOG copy explicitly rather than
retesting discovery under the new account. Owner-account GOG and Steam discovery
were tested earlier. Hidden ten-second startup does not validate visible output,
interactive controls, sound or a full flight. The separate GPU picture guards
provide the recorded D3D12 evidence. This is not a second clean-PC result; follow
[the user guide](setup-user-guide.md) for that check.

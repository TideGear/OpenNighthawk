# Local setup guide

Obtain the source-only setup ZIP from the project owner and extract it to a normal
folder. Keep your own GOG or Steam F-117A installation installed. Double-click
`setup.cmd`. Setup downloads its private tools, verifies your game, and builds
OpenNighthawk locally. It needs internet access, disk space and several minutes.
Setup downloads its own tools. It passed with no developer tools on PATH; a PC
with neither Visual Studio nor Python installed has not yet been verified.

Before the first download setup lists the tools it will fetch and their licences
(including the Microsoft terms for the shader compiler) and asks you to type YES.
Scripted runs add `-AcceptLicences` to agree without being asked; without it a run
with no console stops before downloading anything.

When setup succeeds, open the OpenNighthawk desktop shortcut. Keep the setup work
folder: the shortcut points to the game built there. Your original installation
must remain available. Generated code and binaries are for your own machine and
must not be added to a source release.

If discovery fails, run Windows PowerShell in the extracted source folder:

```powershell
.\tools\setup.ps1 -Data 'D:\Games\F-117A'
```

Read the reported error if setup stops. A failed checksum must be investigated;
do not bypass it. After resolving the cause, rerun the same command: setup cleans
its recognized incomplete work and starts again. The default folder is
`%LOCALAPPDATA%\OpenNighthawk\setup-work`; an explicit `-Work` is reused on retry.
Completed builds, unrecognized folders, game installs and junctions are never
automatically cleaned. If Zig
cannot be used, install Microsoft's Visual Studio Build Tools with Desktop
development with C++ and a Windows SDK, then explicitly select the fallback:

```powershell
.\tools\setup.ps1 -Compiler VisualStudio -Data 'D:\Games\F-117A'
```

## Second clean-PC check

Use a Windows 10/11 x64 PC with no developer tools installed. Record Windows
version, CPU, available disk space and graphics driver. Install your own GOG or
Steam game normally. Verify the owner's source ZIP SHA-256, extract it, and run
`setup.cmd` as a standard user. Record discovery, all downloads, elapsed time,
errors, work folder and shortcut target. Launch the desktop shortcut and check
visible output, controls and sound. Confirm the original installation's hashes
are unchanged. Report any failure with the setup console output. Do not publish
the private generated folder or game data with the report.

## Owner smoke test — visible shortcut (one page)

Do this before approving publication. Use the source ZIP and SHA-256 supplied
with the release review. Run as your normal standard user. Keep the setup log
and record Windows version, graphics device/driver, compiler choice and ZIP hash.
Do not set hidden-window, dummy-audio or test replay options for this check.

- [ ] **Build:** extract the ZIP and run `setup.cmd`. Confirm the game installation
  is found and verification succeeds. Record elapsed time and the work folder.
  If setup stops, save the error; rerun after resolving it. Never bypass a hash
  check. Expect the OpenNighthawk desktop shortcut after success.
- [ ] **Launch visibly:** double-click that real shortcut. Confirm a visible game
  window opens and the original SETUP questions can be answered. Choose your
  normal input and sound device; AdLib is option 2. Record any blank window,
  missing DLL, unexpected prompt or error.
- [ ] **Start a short flight:** use a practice/training mission. Reach the cockpit,
  take off and fly for a few minutes. Check that the picture updates normally.
  Note the presentation options used; this smoke test does not prove pixel parity.
- [ ] **Controls:** check keyboard/menu navigation, throttle, pitch, roll, views
  and pause. If using a joystick, check calibration and flight control too.
  Record the device and any unresponsive, stuck or unexpected control.
- [ ] **Sound:** listen to music and effects at a comfortable volume. Confirm
  sound reaches the selected device without persistent crackling or dropouts.
  Record the selected driver. Roland/Munt additionally needs your own ROMs;
  do not mark an untested sound option as passed.
- [ ] **Quit:** return through the game/menu and quit normally. Confirm its
  process exits and the shortcut opens it again. Check the original installation
  still verifies with `tools/verify_install.py` using the private setup Python.
- [ ] **Report:** give pass/fail for each item, errors, build time, hardware and
  the ZIP SHA-256. Share logs only after checking them for private paths. Do not
  share game data, generated C, recordings or the privately built executable.

A pass is an owner's gameplay smoke test, not the full Zig unit/coverage pipeline,
a second clean-PC test, or resolution of the 92-of-901 cross-compiler Remastered
picture question. Recheck that question with the exact-raster branch once merged.
Recommend publishing the source-only release only after this checklist passes.

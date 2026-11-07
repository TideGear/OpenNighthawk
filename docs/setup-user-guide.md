# Local setup guide

Download the setup ZIP, extract it to a normal folder and double-click `setup.cmd`.
Keep your own GOG or Steam F-117A installation installed: setup finds it, checks it,
and builds OpenNighthawk from it on this PC. It needs internet access, about 1.5 GB of
free space while it builds, and about 5 to 15 minutes.

## What the wizard asks

1. **Licences.** The tools it downloads and their licences, including Microsoft's terms
   for the shader compiler; type yes to continue (any case; no stops setup).
2. **Install folder.** Default `%LOCALAPPDATA%\Programs\OpenNighthawk` (press Enter to
   accept). Only the game itself, about 35 MB, goes there.
3. **Shortcuts.** A desktop shortcut and a Start menu entry, each optional.
4. **Sound (optional).** AdLib music, effects and speech are the default and need
   nothing. If you have the two Roland MT-32 ROM files, give their folder (setup checks
   them by hash) for MT-32 music. If you have the Macintosh version of F-117A, give its
   StuffIt archive (`.sit`), a folder holding it, or an unpacked copy (disk images are not read):
   setup imports its 33 recorded sounds, music and speech words into the install
   folder's `mac-sounds`, and the game then uses them first (Mac, then Roland, then
   AdLib). Unpacking an archive uses The Unarchiver's `unar`, downloaded and checked
   like the other tools. A failed import does not stop setup; the game uses AdLib.
5. **Settings (optional).** Take the defaults, or choose the visuals (Original, Enhanced or
   Remastered), full screen, and whether the bug fixes are on. If you choose Remastered,
   setup also asks for the **Remastered view** (wide, which fills a widescreen window with
   extra view at the sides, or standard 4:3) and the **draw distance** (Original, Near,
   Far or Maximum: how far away buildings, lights and scenery are drawn; farther looks
   better and costs frame rate). Everything can be changed later in the game with the `
   key.

It then shows one line per stage and a live progress line while compiling. When it
finishes it removes its build files (the downloaded tools and the compiler's
intermediate files, about 1.4 GB) and keeps a log in the install folder's `logs`
folder; send the newest log when reporting a problem. If setup stops early, the
message names the log, which is then in `%TEMP%\OpenNighthawk-setup`.

Your saved pilots are kept outside the install folder (`%APPDATA%\F117Recomp\F-117A`)
and your settings in `f117a.ini` beside the game. Running setup again (for a new
version) finds the existing install, updates it in place and asks whether to keep
your settings; a build left by the first version of setup (in
`%LOCALAPPDATA%\OpenNighthawk\setup-work`) is replaced by an installed copy and its
settings are carried over. Setup never writes to your game installation. Generated
code and binaries are for your own machine and must not be shared.

**Uninstalling.** Use Windows Settings, Apps & features (OpenNighthawk), the Start
menu's "Uninstall OpenNighthawk", or double-click `uninstall.cmd` in the install folder
(`uninstall.ps1` is the script behind it; double-clicking a `.ps1` file opens it in
Notepad). It removes the install folder, its shortcuts and its Apps & features entry,
and asks before deleting your saved pilots; it removes only a folder setup marked as its
install and never touches the game installation. It closes File Explorer windows that
show the install folder; if Windows still reports the folder in use, close any other
window or program using it and run the uninstaller again.

**Windows SmartScreen and antivirus.** Setup downloads Zig, a build tool, and compiles
the game on your PC, so the program it produces is new to Windows and its reputation
checks. SmartScreen may warn the first time you run `setup.cmd` or the built game
("Windows protected your PC": choose More info, then Run anyway), and an antivirus may
scan the build for a while. Setup checks every download against a pinned checksum; if an
antivirus quarantines something, setup stops with a message rather than continuing.

## Options for scripted runs

`setup.cmd -AcceptLicences` asks nothing and takes the defaults. Further options:
`-Data FOLDER` (the game folder; if it does not hold a verified game, setup stops
rather than using another installation it finds), `-InstallDir FOLDER`,
`-NoDesktopShortcut`, `-StartMenu`, `-KeepBuildFiles`, `-Mt32Roms FOLDER`,
`-MacSource PATH`, `-Presentation original|enhanced|remastered`, `-RemasterView wide|4:3`,
`-DrawDistance original|near|far|maximum`, `-Work FOLDER` (the temporary build workspace, default
`%LOCALAPPDATA%\OpenNighthawk\setup-work`). Scripted runs keep existing settings and
merge these choices in. `uninstall.ps1 -Yes [-RemoveSaves]` uninstalls without asking.

A failed checksum must be investigated; do not bypass it. After fixing the cause, run
setup again: it cleans its own incomplete work and starts over. Unrecognized folders,
game installs and junctions are never cleaned. If Zig cannot be used, install
Microsoft's Visual Studio Build Tools with Desktop development with C++ and a Windows
SDK, then run `setup.cmd -Compiler VisualStudio`.

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

- [ ] **Build:** extract the ZIP and run `setup.cmd`; answer the wizard (defaults are fine). Confirm the game installation
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

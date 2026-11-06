# Local setup guide

Obtain the source-only setup ZIP from the project owner and extract it to a normal
folder. Keep your own GOG or Steam F-117A installation installed. Double-click
`setup.cmd`. Setup downloads its private tools, verifies your game, and builds
OpenNighthawk locally. It needs internet access, disk space and several minutes.
No Git, Python or Visual Studio installation is needed for the default Zig path.

When setup succeeds, open the OpenNighthawk desktop shortcut. Keep the setup work
folder: the shortcut points to the game built there. Your original installation
must remain available. Generated code and binaries are for your own machine and
must not be added to a source release.

If discovery fails, run Windows PowerShell in the extracted source folder:

```powershell
.\tools\setup.ps1 -Data 'D:\Games\F-117A'
```

Read the reported error if setup stops. A failed checksum must be investigated;
do not bypass it. Retry in a new work folder after resolving the cause. If Zig
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

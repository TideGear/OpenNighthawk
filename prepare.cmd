@echo off
rem For RecompLauncher (see recomplauncher.json): the same setup as setup.cmd, with no questions, no window pause and 'progress 0.4 Text' lines on standard output.
rem Not for double-clicking: use setup.cmd. Arguments are passed on to tools\setup-wizard.ps1 (-Data, -InstallDir, ...).
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0tools\setup-wizard.ps1" -LauncherProgress -NoDesktopShortcut -NoSystemEntries %*
exit /b %errorlevel%

@echo off
rem Double-click to uninstall OpenNighthawk (a .ps1 file opens in Notepad).
rem It hands over to a separate PowerShell window and exits at once: this file is deleted with the install folder, and a batch file that is
rem deleted while it runs makes the window print errors. Leaving the folder first also matters (Windows will not delete a current directory).
rem For scripts, run uninstall.ps1 directly (-Yes, -RemoveSaves).
cd /d "%TEMP%" & start "Uninstall OpenNighthawk" powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1" %* & exit

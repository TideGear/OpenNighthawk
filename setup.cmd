@echo off
setlocal
rem Run setup from this source-only release; no Git or installed Python needed.
rem The wizard asks a few questions, builds, installs, and keeps a log in the install folder's logs folder.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup-wizard.ps1" %*
if errorlevel 1 (
  echo.
  echo OpenNighthawk setup did not finish. Read the message above; the setup log path is shown there.
  echo Your original game files have not been changed.
  pause
  exit /b 1
)
pause

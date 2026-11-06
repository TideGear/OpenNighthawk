@echo off
setlocal
rem Run setup from this source-only release; no Git or installed Python needed.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup.ps1" %*
if errorlevel 1 (
  echo.
  echo OpenNighthawk setup did not finish. Read the error above.
  echo Your original game files have not been changed.
  pause
  exit /b 1
)
pause

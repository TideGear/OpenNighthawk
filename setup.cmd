@echo off
setlocal
rem Run setup from this source-only release; no Git or installed Python needed.
rem Everything setup prints is also written to %LOCALAPPDATA%\OpenNighthawk\logs\setup-DATE-TIME.log.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup-logged.ps1" %*
if errorlevel 1 (
  echo.
  echo OpenNighthawk setup did not finish. Read the error above.
  echo The full output is in %LOCALAPPDATA%\OpenNighthawk\logs ^(the newest setup-*.log^).
  echo Your original game files have not been changed.
  pause
  exit /b 1
)
pause

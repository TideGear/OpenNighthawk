@echo off
rem Configure and build with MSVC + Ninja. Usage: build.cmd [extra cmake -D args]
setlocal enabledelayedexpansion
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" ( echo ERROR: vcvars64.bat not found & exit /b 1 )
call "%VCVARS%" >nul
cd /d "%~dp0"
rem The working copy lives in a synced folder and build output does not
rem belong there: the sync client locks objects mid-build and uploads every
rem one. `build` is a junction to local disk, made here on first use.
if not exist build\ (
    if not exist "%USERPROFILE%\f117-recomp-local\build" mkdir "%USERPROFILE%\f117-recomp-local\build"
    mklink /J build "%USERPROFILE%\f117-recomp-local\build" >nul
)
set "LOG=%TEMP%\f117r-build.log"
del /q build\*.old.* 2>nul

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo %*
if errorlevel 1 exit /b 1

rem A failed build is retried while the failure is a locked file: the sync
rem client intermittently holds an object or output open. Any other
rem failure is reported at once. The log is scanned as well as the exit
rem code, which has been seen to report success after a failed link.
for /l %%A in (1,1,30) do (
  cmake --build build > "%LOG%" 2>&1
  set "RC=!ERRORLEVEL!"
  findstr /C:"ninja: build stopped" /C:"FAILED:" /C:"fatal error" "%LOG%" >nul
  if errorlevel 1 (set "SCAN=0") else (set "SCAN=1")
  if "!RC!"=="0" if "!SCAN!"=="0" (
    type "%LOG%"
    echo BUILD OK
    exit /b 0
  )
  findstr /C:"Permission denied" /C:"C1041" /C:"LNK1104" /C:"LNK1168" /C:"LNK1201" "%LOG%" >nul
  if errorlevel 1 (
    type "%LOG%"
    echo BUILD FAILED
    exit /b 1
  )
  echo [build] attempt %%A hit a locked file; retrying
  for /f "tokens=8" %%F in ('findstr /C:"LNK1168: cannot open" "%LOG%"') do (
    if exist "build\%%F" ren "build\%%F" "%%F.old.%%A" 2>nul
  )
  ping -n 11 127.0.0.1 >nul
)
type "%LOG%"
echo BUILD FAILED ^(still locked after 30 attempts^)
exit /b 1

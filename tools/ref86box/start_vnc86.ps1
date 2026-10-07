<#
 start_vnc86.ps1 PROFILE_DIR [LOG] - start the source-built 86Box (VNC renderer) with
 no window: Qt's offscreen platform, so nothing appears on any desktop. It waits
 for a VNC client (vnc86.py) on port 5900; stop it with Stop-Process -Name 86Box.
#>
param([string]$Profile = "D:\86box\vmk", [string]$Log = "D:\86box\vnc86.log",
      [string]$Exe = "D:\86box-src\build\src\86Box.exe", [string]$Roms = "D:\86box\app\roms")
$env:Path = "D:\msys64\ucrt64\bin;" + $env:Path
$env:QT_QPA_PLATFORM = "offscreen"
# Never audible: OpenAL gets its null device, on top of sound_muted = 1 in the profile.
$env:ALSOFT_DRIVERS = "null"
$env:SDL_AUDIODRIVER = "dummy"
Get-Process 86Box -ErrorAction SilentlyContinue | Stop-Process -Force
$p = Start-Process -FilePath $Exe -ArgumentList @("-P", $Profile, "-R", $Roms, "-N", "-L", $Log) `
     -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru
$p.PriorityClass = "BelowNormal"
"started pid $($p.Id)"

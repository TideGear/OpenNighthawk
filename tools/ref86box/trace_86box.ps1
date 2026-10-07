<#
 trace_86box.ps1 - a traced 86Box run: every displayed frame logged with its
 emulated time, keys injected at frame counts, no VNC client, no window, no
 sound. Needs the build with tools/ref86box/86box-trace.patch.

   powershell -File trace_86box.ps1 -Profile D:\86box\vmt -Out D:\86box\trace1 `
       -Stop 12000 [-Keys "3000:1:31,3003:0:31,3300:1:03,3303:0:03"] [-Ppm]

 Out\frames.csv: frame, tsc, emulated microseconds, width, height, hash. Keys are
 "frame:down:scancode" (86Box's set-1 scancodes, hex): N is 31, 2 is 03, Enter 1C.
 With -Ppm each new picture is also saved (320x200 for mode 13h). The process
 exits itself after -Stop frames; it is killed if it runs past -TimeoutSeconds.
#>
param([string]$Profile = "D:\86box\vmt", [string]$Out = "D:\86box\trace", [int]$Stop = 12000,
      [string]$Keys = "", [switch]$Ppm, [int]$TimeoutSeconds = 900,
      [string]$Exe = "D:\86box-src\build\src\86Box.exe", [string]$Roms = "D:\86box\app\roms")
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-ChildItem $Out -Filter "f*.ppm" -ErrorAction SilentlyContinue | ForEach-Object { $_.Delete() }
$env:Path = "D:\msys64\ucrt64\bin;" + $env:Path
$env:QT_QPA_PLATFORM = "offscreen"
$env:ALSOFT_DRIVERS = "null"
$env:SDL_AUDIODRIVER = "dummy"
$env:B86_TRACE = $Out
$env:B86_STOP = "$Stop"
if ($Keys) { $env:B86_KEYS = $Keys } else { Remove-Item Env:B86_KEYS -ErrorAction SilentlyContinue }
if ($Ppm) { $env:B86_PPM = "1" } else { Remove-Item Env:B86_PPM -ErrorAction SilentlyContinue }
# only a stale run of this very profile: other 86Box runs (other profiles) are left alone
Get-CimInstance Win32_Process -Filter "Name='86Box.exe'" | Where-Object { $_.CommandLine -like "*$Profile*" } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
$p = Start-Process -FilePath $Exe -ArgumentList @("-P", $Profile, "-R", $Roms, "-N") `
     -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru
$p.PriorityClass = "BelowNormal"
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { $p.Kill(); "timed out after $TimeoutSeconds s" } else { "exited $($p.ExitCode)" }
$csv = Join-Path $Out "frames.csv"
if (Test-Path $csv) { "frames logged: " + ((Get-Content $csv | Measure-Object -Line).Lines) }

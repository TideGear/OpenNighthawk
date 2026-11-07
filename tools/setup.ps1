# Build locally from the verified owner-supplied game installation.
# Run by tools/setup-wizard.ps1 (what setup.cmd starts), which asks the questions and shows the lines that begin with '## ';
# everything else this prints goes only to the log. It can also be run directly with -AcceptLicences.
[CmdletBinding()]
param([string]$Data,[string]$Work,[string]$ArchiveFolder,[ValidateSet('Zig','VisualStudio')][string]$Compiler='Zig',[string]$ShortcutPath,
      [string]$InstallDir,[switch]$NoDesktopShortcut,[switch]$StartMenu,[string]$MacSource,[switch]$NoSystemEntries,[switch]$AcceptLicences)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'     # Windows PowerShell's download progress bar slows Invoke-WebRequest many times over
. (Join-Path $PSScriptRoot 'setup-paths.ps1')
. (Join-Path $PSScriptRoot 'find-f117-install.ps1')
. (Join-Path $PSScriptRoot 'setup-retry.ps1')
. (Join-Path $PSScriptRoot 'setup-python.ps1')
. (Join-Path $PSScriptRoot 'setup-sources.ps1')
. (Join-Path $PSScriptRoot 'setup-build-tools.ps1')
. (Join-Path $PSScriptRoot 'setup-licences.ps1')
. (Join-Path $PSScriptRoot 'setup-mac.ps1')
if (-not $Work) { $Work = Join-Path $env:LOCALAPPDATA 'OpenNighthawk/setup-work' }
$Work = Assert-F117OutputPath -Path $Work -GameDirectory $Data
if ($ShortcutPath) { $ShortcutPath = Assert-F117OutputPath -Path $ShortcutPath -GameDirectory $Data }
if ($InstallDir) { $InstallDir = Assert-F117OutputPath -Path $InstallDir -GameDirectory $Data }

Confirm-F117Licences -Compiler $Compiler -Accept:$AcceptLicences
$workHandle = Open-F117SetupWork -Path $Work -GameDirectory $Data
try {
Write-Host '## Downloading Python (11 MB)'
$pythonArgs = @{ Destination=Join-Path $Work 'python' }
if ($ArchiveFolder) { $pythonArgs.Archive = Join-Path $ArchiveFolder 'python-3.12.10-embed-amd64.zip' }
$python = Initialize-F117Python @pythonArgs
Write-Host '## Checking your game files'
$accepted = $null
foreach ($candidate in Get-F117InstallCandidates -SelectedFolder $Data) {
    & $python (Join-Path $PSScriptRoot 'verify_install.py') --data $candidate
    if ($LASTEXITCODE -eq 0) { $accepted = $candidate; break }
}
if ($Data -and $accepted) {
    # A folder the user named is the only one to use: a mistyped or wrong -Data must not quietly build from another install that discovery found.
    $named = [IO.Path]::GetFullPath($Data).TrimEnd('\')
    if (-not ($accepted -ieq $named -or $accepted -ieq (Join-Path $named 'F-117A'))) { $accepted = $null }
}
if (-not $accepted -and $Data) { throw "The folder given with -Data does not hold a verified F-117A installation: $Data" }
if (-not $accepted) { throw 'No verified F-117A installation found. Supply -Data with your GOG or Steam game folder.' }
Write-Host "## Game files verified: $accepted"
Write-Host '## Downloading build tools: CMake, Ninja, shader compiler (75 MB)'
$tools = Initialize-F117BuildTools -Destination (Join-Path $Work 'tools') -ArchiveFolder $ArchiveFolder
Write-Host '## Downloading SDL and Munt source code (18 MB)'
$sources = Initialize-F117SourceDependencies -Destination (Join-Path $Work 'sources') -ArchiveFolder $ArchiveFolder
$buildArgs = @{
    Compiler=$Compiler; Data=$accepted; Work=Join-Path $Work 'game'; Python=$python
    Cmake=$tools.Cmake; Ninja=$tools.Ninja; Dxc=$tools.Dxc
    SdlSource=$sources.SdlSource; MuntSource=$sources.MuntSource
}
if ($ArchiveFolder) { $buildArgs.ZigArchive = Join-Path $ArchiveFolder 'zig.zip' }
& (Join-Path $PSScriptRoot 'build-zig.ps1') @buildArgs
$build = Join-Path $Work 'game/build'
$exe = Join-Path $build 'f117a.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build did not produce the game executable.' }

if ($InstallDir) {
    # Only what the game needs to run goes to the install folder (about 35 MB); the build workspace stays in Work for the caller to remove.
    Write-Host "## Installing to $InstallDir"
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $files = @(Get-ChildItem -LiteralPath $build -File | Where-Object { $_.Name -eq 'f117a.exe' -or $_.Extension -in '.dll','.dxil' })
    foreach ($file in $files) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $InstallDir $file.Name) -Force }
    $exe = Join-Path $InstallDir 'f117a.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Installing the game executable failed.' }
    # The marker the uninstaller requires before it deletes anything, and the uninstaller itself.
    [IO.File]::WriteAllText((Join-Path $InstallDir '.opennighthawk-install'), "OpenNighthawk install made by setup`r`n")
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'uninstall.ps1') -Destination (Join-Path $InstallDir 'uninstall.ps1') -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'uninstall.cmd') -Destination (Join-Path $InstallDir 'uninstall.cmd') -Force
}

if ($MacSource) {
    # Not fatal: without the Mac recordings the game plays its AdLib sound, which is the default anyway.
    Write-Host '## Importing the Mac sounds and music'
    $macOut = Join-Path (Split-Path $exe) 'mac-sounds'
    try {
        $count = Import-F117MacSounds -Source $MacSource -Python $python -Work $Work -Out $macOut -ArchiveFolder $ArchiveFolder
        Write-Host "## Mac sounds imported: $count recordings"
    } catch {
        Write-Host "## Mac sounds not imported: $($_.Exception.Message) The game will use its AdLib sound."
    }
}

$targets = @()
if ($ShortcutPath) { $targets += $ShortcutPath }
elseif (-not $NoDesktopShortcut) { $targets += Join-Path ([Environment]::GetFolderPath('Desktop')) 'OpenNighthawk.lnk' }
$menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'OpenNighthawk'
if ($StartMenu -and -not $NoSystemEntries) { New-Item -ItemType Directory -Force -Path $menu | Out-Null; $targets += Join-Path $menu 'OpenNighthawk.lnk' }
$shell = New-Object -ComObject WScript.Shell
foreach ($target in $targets) {
    $target = Assert-F117OutputPath -Path $target -GameDirectory $accepted
    if ([IO.Path]::GetExtension($target) -ne '.lnk') { throw 'Shortcut path must end in .lnk.' }
    $shortcut = $shell.CreateShortcut($target)
    $shortcut.TargetPath = $exe
    $shortcut.Arguments = '--data "' + $accepted + '"'
    $shortcut.WorkingDirectory = Split-Path $exe
    $shortcut.Save()
    Write-Host "## Shortcut: $target"
}
if ($InstallDir) {
    $uninstall = Join-Path $InstallDir 'uninstall.ps1'
    $uninstallCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $uninstall + '"'
    if ($StartMenu -and -not $NoSystemEntries) {
        $u = $shell.CreateShortcut((Join-Path $menu 'Uninstall OpenNighthawk.lnk'))
        $u.TargetPath = 'powershell.exe'
        $u.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $uninstall + '"'
        $u.WorkingDirectory = $env:TEMP
        $u.Save()
    }
    # Apps & features (per user, no administrator rights): name, size and the uninstaller.
    if (-not $NoSystemEntries) {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNighthawk'
    New-Item -Path $key -Force | Out-Null
    $kb = [int]((Get-ChildItem -LiteralPath $InstallDir -Recurse -File | Measure-Object Length -Sum).Sum / 1KB)
    $values = @{ DisplayName='OpenNighthawk'; DisplayVersion=(Get-Date -Format 'yyyy.MM.dd'); Publisher='OpenNighthawk (built on this PC)';
                 InstallLocation=$InstallDir; DisplayIcon=$exe; UninstallString=$uninstallCommand }
    foreach ($k in $values.Keys) { New-ItemProperty -Path $key -Name $k -Value $values[$k] -PropertyType String -Force | Out-Null }
    foreach ($k in 'NoModify','NoRepair') { New-ItemProperty -Path $key -Name $k -Value 1 -PropertyType DWord -Force | Out-Null }
    New-ItemProperty -Path $key -Name 'EstimatedSize' -Value $kb -PropertyType DWord -Force | Out-Null
    Write-Host '## Listed in Windows Apps & features (uninstall from there or the Start menu)'
    }
}
[IO.File]::WriteAllText((Join-Path $Work '.f117-setup-complete'),$exe)
Write-Host "## Build complete: $exe"
} finally { $workHandle.Dispose() }

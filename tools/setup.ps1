# Build locally from the verified owner-supplied game installation.
[CmdletBinding()]
param([string]$Data,[string]$Work,[string]$ArchiveFolder,[ValidateSet('Zig','VisualStudio')][string]$Compiler='Zig',[string]$ShortcutPath)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'setup-paths.ps1')
. (Join-Path $PSScriptRoot 'find-f117-install.ps1')
. (Join-Path $PSScriptRoot 'setup-python.ps1')
. (Join-Path $PSScriptRoot 'setup-sources.ps1')
. (Join-Path $PSScriptRoot 'setup-build-tools.ps1')
if (-not $Work) { $Work = Join-Path $env:LOCALAPPDATA ('OpenNighthawk/setup-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$Work = Assert-F117OutputPath -Path $Work -GameDirectory $Data
if ($ShortcutPath) { $ShortcutPath = Assert-F117OutputPath -Path $ShortcutPath -GameDirectory $Data }
if (Test-Path -LiteralPath $Work) { throw 'Choose a new setup work folder.' }
while (Test-Path -LiteralPath 'D:\f117-dl\HEAVY.lock') { Start-Sleep -Seconds 5 }
$pythonArgs = @{ Destination=Join-Path $Work 'python' }
if ($ArchiveFolder) { $pythonArgs.Archive = Join-Path $ArchiveFolder 'python-3.12.10-embed-amd64.zip' }
$python = Initialize-F117Python @pythonArgs
$accepted = $null
foreach ($candidate in Get-F117InstallCandidates -SelectedFolder $Data) {
    & $python (Join-Path $PSScriptRoot 'verify_install.py') --data $candidate
    if ($LASTEXITCODE -eq 0) { $accepted = $candidate; break }
}
if (-not $accepted) { throw 'No verified F-117A installation found. Supply -Data with your GOG or Steam game folder.' }
$tools = Initialize-F117BuildTools -Destination (Join-Path $Work 'tools') -ArchiveFolder $ArchiveFolder
$sources = Initialize-F117SourceDependencies -Destination (Join-Path $Work 'sources') -ArchiveFolder $ArchiveFolder
$buildArgs = @{
    Compiler=$Compiler; Data=$accepted; Work=Join-Path $Work 'game'; Python=$python
    Cmake=$tools.Cmake; Ninja=$tools.Ninja; Dxc=$tools.Dxc
    SdlSource=$sources.SdlSource; MuntSource=$sources.MuntSource
}
if ($ArchiveFolder) { $buildArgs.ZigArchive = Join-Path $ArchiveFolder 'zig.zip' }
& (Join-Path $PSScriptRoot 'build-zig.ps1') @buildArgs
$exe = Join-Path $Work 'game/build/f117a.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build did not produce the game executable.' }
$shell = New-Object -ComObject WScript.Shell
if (-not $ShortcutPath) { $ShortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'OpenNighthawk.lnk' }
$ShortcutPath = Assert-F117OutputPath -Path $ShortcutPath -GameDirectory $accepted
if ([IO.Path]::GetExtension($ShortcutPath) -ne '.lnk') { throw 'Shortcut path must end in .lnk.' }
$shortcut = $shell.CreateShortcut($ShortcutPath)
$shortcut.TargetPath = $exe
$shortcut.Arguments = '--data "' + $accepted + '"'
$shortcut.WorkingDirectory = Split-Path $exe
$shortcut.Save()
Write-Host 'Build complete. OpenNighthawk shortcut created.'

# OpenNighthawk uninstaller. Setup copies this into the install folder as uninstall.ps1; Apps & features and the Start menu run it.
# It removes only a folder setup marked as an OpenNighthawk install, the shortcuts that point into it, the Start menu folder and the
# Apps & features entry. Saved pilots and settings history (%APPDATA%\F117Recomp\F-117A) are kept unless the user says otherwise.
# The game installation (GOG or Steam) is never touched.
[CmdletBinding()]
param([string]$InstallDir,[switch]$Yes,[switch]$RemoveSaves,[switch]$FromTemp)
$ErrorActionPreference = 'Stop'
if (-not $InstallDir) { $InstallDir = $PSScriptRoot }
Set-Location -LiteralPath $env:TEMP      # never run from inside the folder being removed (Windows refuses to delete a current directory)
$InstallDir = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$marker = Join-Path $InstallDir '.opennighthawk-install'

# Run from a copy in %TEMP%, so the install folder (which holds this script) can be deleted.
if (-not $FromTemp) {
    $copy = Join-Path $env:TEMP ('opennighthawk-uninstall-' + [Guid]::NewGuid().ToString('N') + '.ps1')
    Copy-Item -LiteralPath $PSCommandPath -Destination $copy
    $a = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$copy,'-InstallDir',$InstallDir,'-FromTemp')
    if ($Yes) { $a += '-Yes' }
    if ($RemoveSaves) { $a += '-RemoveSaves' }
    & powershell.exe @a
    exit $LASTEXITCODE
}

try {
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) { throw "$InstallDir is not an OpenNighthawk install made by setup; nothing was removed." }
    Write-Host "Uninstall OpenNighthawk from $InstallDir"
    if (-not $Yes) {
        $a = Read-Host 'Remove OpenNighthawk? [y/N]'
        if ($a -notmatch '^(y|yes)$') { Write-Host 'Nothing was removed.'; exit 0 }
    }
    $saves = Join-Path $env:APPDATA 'F117Recomp\F-117A'
    $dropSaves = [bool]$RemoveSaves
    if (-not $Yes -and (Test-Path -LiteralPath $saves)) {
        Write-Host "Your saved pilots, screenshots and recorded flights are in:"
        Write-Host "  $saves"
        $a = Read-Host 'Delete them too? [y/N]'
        $dropSaves = $a -match '^(y|yes)$'
    }

    # Shortcuts that point into the install folder.
    $shell = New-Object -ComObject WScript.Shell
    $places = @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))
    foreach ($place in $places) {
        foreach ($lnk in Get-ChildItem -LiteralPath $place -Filter '*.lnk' -File -ErrorAction SilentlyContinue) {
            $target = $shell.CreateShortcut($lnk.FullName).TargetPath
            if ($target -and $target.StartsWith($InstallDir + '\', [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $lnk.FullName -Force }
        }
    }
    $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'OpenNighthawk'
    $menuLnk = Join-Path $menu 'OpenNighthawk.lnk'
    if (Test-Path -LiteralPath $menuLnk) {
        $menuTarget = $shell.CreateShortcut($menuLnk).TargetPath
        if ($menuTarget -and $menuTarget.StartsWith($InstallDir + '\', [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $menu -Recurse -Force }
    }

    # The Apps & features entry, if it is this install's.
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNighthawk'
    $entry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
    if ($entry -and $entry.InstallLocation -and ([IO.Path]::GetFullPath($entry.InstallLocation).TrimEnd('\') -ieq $InstallDir)) { Remove-Item -LiteralPath $key -Force }

    # An Explorer window showing the folder keeps Windows from deleting it: close only those windows, then retry a few times.
    try {
        foreach ($win in (New-Object -ComObject Shell.Application).Windows()) {
            $loc = try { $win.LocationURL } catch { $null }
            if ($loc -and $loc.StartsWith(([Uri]($InstallDir + '\')).AbsoluteUri.TrimEnd('/'), [StringComparison]::OrdinalIgnoreCase)) { try { $win.Quit() } catch {} }
        }
    } catch {}
    for ($try = 1; ; $try++) {
        try { Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction Stop; break }
        catch { if ($try -ge 5) { throw "$($_.Exception.Message) Close any window or program that is using the folder (File Explorer, a command prompt, Notepad) and run the uninstaller again." }; Start-Sleep -Seconds 2 }
    }
    if ($dropSaves -and (Test-Path -LiteralPath $saves)) {
        Remove-Item -LiteralPath $saves -Recurse -Force
        $parent = Split-Path $saves -Parent
        if (-not (Get-ChildItem -LiteralPath $parent -Force)) { Remove-Item -LiteralPath $parent -Force }
        Write-Host 'Saved pilots and settings history deleted.'
    }
    Write-Host 'OpenNighthawk has been removed. Your F-117A game installation was not changed.'
    $code = 0
} catch {
    Write-Host ('Uninstall stopped: ' + $_.Exception.Message)
    $code = 1
}
if (-not $Yes) { Write-Host 'Press Enter to close.'; [void][Console]::ReadLine() }
exit $code

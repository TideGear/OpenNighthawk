# OpenNighthawk uninstaller. Setup copies this into the install folder as uninstall.ps1; Apps & features and the Start menu run it.
# It removes only a folder setup marked as an OpenNighthawk install, the shortcuts that point into it, the Start menu folder and the
# Apps & features entry. Saved pilots and settings history (%APPDATA%\OpenNighthawk) are kept unless the user says otherwise.
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
    # The data folder is %APPDATA%\OpenNighthawk; before 10 November 2026 it was %APPDATA%\F117Recomp\F-117A (the game moves it on its first run, so it is only
    # still there if the game has not been run since).
    $saveDirs = @((Join-Path $env:APPDATA 'OpenNighthawk'), (Join-Path $env:APPDATA 'F117Recomp\F-117A'))
    $saves = @($saveDirs | Where-Object { Test-Path -LiteralPath $_ })
    $dropSaves = [bool]$RemoveSaves
    if (-not $Yes -and $saves.Count -gt 0) {
        Write-Host "Your saved pilots, screenshots and recorded flights are in:"
        foreach ($s in $saves) { Write-Host "  $s" }
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
    if ($dropSaves -and $saves.Count -gt 0) {
        foreach ($s in $saves) { Remove-Item -LiteralPath $s -Recurse -Force }
        # the old layout had a parent folder of its own (F117Recomp); remove it only when it is empty. The new folder's parent is Roaming itself: never touched.
        $oldParent = Join-Path $env:APPDATA 'F117Recomp'
        if ((Test-Path -LiteralPath $oldParent) -and -not (Get-ChildItem -LiteralPath $oldParent -Force)) { Remove-Item -LiteralPath $oldParent -Force }
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

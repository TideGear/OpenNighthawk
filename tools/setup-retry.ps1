# Retry only folders created by this launcher. Never clean arbitrary user folders.
function Open-F117SetupWork {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Path,[string]$GameDirectory)
    $root = Assert-F117OutputPath -Path $Path -GameDirectory $GameDirectory
    $marker = Join-Path $root '.f117-setup-work'
    $signature = 'OpenNighthawk setup work v1'
    $null = Assert-F117OutputPath -Path $marker -GameDirectory $GameDirectory
    $running = Assert-F117OutputPath -Path (Join-Path $root '.f117-setup-running') -GameDirectory $GameDirectory
    if (Test-Path -LiteralPath $root) {
        if (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or [IO.File]::ReadAllText($marker) -ne $signature) {
            throw 'This folder is not recognized setup work. Choose an empty new folder; nothing was removed.'
        }
    } else {
        New-Item -ItemType Directory -Path $root | Out-Null
        [IO.File]::WriteAllText($marker,$signature)
    }
    # Held for the entire setup. A second launcher must not clean a live run.
    try { $handle = [IO.File]::Open($running,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
    catch { throw 'Setup is already using this work folder. Wait for it to finish before retrying.' }
    try {
        if (Test-Path -LiteralPath (Join-Path $root '.f117-setup-complete')) {
            throw 'Setup already finished in this folder. Use its OpenNighthawk shortcut; choose another -Work folder for a separate build.'
        }
        $prefix = $root + '\'
        $active = Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
            $_.ProcessId -ne $PID -and $_.Name -match '^(zig|python|cmake|ninja|cl|link|rc|dxc)\.exe$' -and
            (($_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) -or
             ($_.CommandLine -and $_.CommandLine.IndexOf($root,[StringComparison]::OrdinalIgnoreCase) -ge 0))
        }
        if ($active) { throw 'A build tool is still using this folder. Wait for it to exit, then retry.' }
        $children = @(Get-ChildItem -LiteralPath $root -Force)
        foreach ($child in $children) {
            if (-not $child.FullName.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) {
                throw 'Cleanup target escaped the setup work folder. Nothing was removed.'
            }
            if ($child.Name -notin @('.f117-setup-work','.f117-setup-running','python','tools','sources','game') -and $child.Name -notmatch '^[0-9a-fA-F-]{36}\.zip$') {
                throw "Unrecognized file in setup work: $($child.Name). Nothing was removed."
            }
        }
        # Any link or junction inside the workspace stops the cleanup (nothing is followed or removed). One native directory listing of every reparse point
        # replaces a PowerShell check of each of the tens of thousands of files a failed build leaves, which took minutes.
        $links = @(& cmd.exe /d /c ('dir /a:l /s /b "' + $root + '" 2>nul'))
        if ($links.Count -gt 0 -and $links[0]) { throw "Output paths through links or junctions are not supported: $($links[0]). Nothing was removed." }
        foreach ($child in $children) {
            if ($child.Name -in @('.f117-setup-work','.f117-setup-running')) { continue }
            Write-Host "## Removing incomplete setup files: $($child.Name)"
            if ($child.PSIsContainer) {
                & cmd.exe /d /c ('rd /s /q "' + $child.FullName + '"')       # rd removes a link without following it; faster than Remove-Item -Recurse
                if (Test-Path -LiteralPath $child.FullName) { throw "Could not remove $($child.FullName). Close anything using it and run setup again." }
            } else { Remove-Item -LiteralPath $child.FullName -Force }
        }
        return $handle
    } catch { $handle.Dispose(); throw }
}

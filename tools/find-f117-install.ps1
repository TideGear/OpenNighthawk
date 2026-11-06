# Return candidate game folders. Every candidate must still pass verify_install.py.
function Get-F117InstallCandidates {
    [CmdletBinding()]
    param([string]$SelectedFolder)
    $candidates = [Collections.Generic.List[string]]::new()
    if ($SelectedFolder) { $candidates.Add($SelectedFolder) }
    foreach ($base in @('HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games','HKLM:\SOFTWARE\GOG.com\Games')) {
        if (Test-Path $base) {
            foreach ($key in Get-ChildItem $base -ErrorAction SilentlyContinue) {
                $entry = Get-ItemProperty $key.PSPath -ErrorAction SilentlyContinue
                if ($entry.gameName -match 'F.?117' -and $entry.path) { $candidates.Add([string]$entry.path) }
            }
        }
    }
    $steamRoots = [Collections.Generic.List[string]]::new()
    foreach ($key in @('HKCU:\Software\Valve\Steam','HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        $entry = Get-ItemProperty $key -ErrorAction SilentlyContinue
        foreach ($value in @($entry.SteamPath,$entry.InstallPath)) {
            if ($value) { $steamRoots.Add([string]$value) }
        }
    }
    if (${env:ProgramFiles(x86)}) { $steamRoots.Add((Join-Path ${env:ProgramFiles(x86)} 'Steam')) }
    $libraries = [Collections.Generic.List[string]]::new()
    foreach ($root in $steamRoots) {
        $libraries.Add($root)
        $vdf = Join-Path $root 'steamapps/libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf) {
            foreach ($line in Get-Content -LiteralPath $vdf) {
                if ($line -match '^\s*"path"\s+"([^"]+)"') {
                    $libraries.Add($Matches[1].Replace('\\','\'))
                }
            }
        }
    }
    foreach ($library in $libraries) {
        $manifest = Join-Path $library 'steamapps/appmanifest_328920.acf'
        if (Test-Path -LiteralPath $manifest) {
            foreach ($line in Get-Content -LiteralPath $manifest) {
                if ($line -match '^\s*"installdir"\s+"([^"]+)"') {
                    $name = $Matches[1]
                    if ($name -ne [IO.Path]::GetFileName($name) -or $name -in @('.','..')) { continue }
                    $candidates.Add((Join-Path $library "steamapps/common/$name"))
                }
            }
        }
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($candidate in $candidates) {
        foreach ($folder in @($candidate,(Join-Path $candidate 'F-117A'))) {
            if ((Test-Path -LiteralPath (Join-Path $folder 'VGAME.EXE')) -and $seen.Add([IO.Path]::GetFullPath($folder))) {
                [IO.Path]::GetFullPath($folder)
            }
        }
    }
}

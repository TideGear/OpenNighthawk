. (Join-Path $PSScriptRoot 'setup-paths.ps1')
# The Mac release's recorded sounds and music, imported from the user's own Mac copy (docs/setup-user-guide.md).
# SOURCE may be a StuffIt archive (.sit, as the Mac version is usually kept; other formats The Unarchiver reads also work), a folder holding an unpacked
# copy (or such archives), or the 'F117a Sound' file itself. Archives are unpacked with The Unarchiver's command-line
# tool, downloaded at run time from its publisher and checked like every other tool (LGPL-2.1, not redistributed).
function Import-F117MacSounds {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Source,[Parameter(Mandatory=$true)][string]$Python,
          [Parameter(Mandatory=$true)][string]$Work,[Parameter(Mandatory=$true)][string]$Out,[string]$ArchiveFolder)
    $ErrorActionPreference = 'Stop'
    $Source = $Source.Trim('"')
    if (-not (Test-Path -LiteralPath $Source)) { throw "Not found: $Source" }
    $importer = Join-Path $PSScriptRoot 'mac_sounds.py'
    $out = Assert-F117OutputPath -Path $Out
    function Try-Import([string]$from) {
        & $Python $importer $from --out $out | Out-Host
        return $LASTEXITCODE -eq 0
    }
    # An unpacked copy or the sound file itself needs nothing more.
    if (Try-Import $Source) { return @(Get-ChildItem -LiteralPath $out -Filter '*.wav').Count }

    # Otherwise unpack: the source itself if it is an archive, or the archives inside a folder.
    $archives = @()
    if (Test-Path -LiteralPath $Source -PathType Leaf) { $archives = @(Get-Item -LiteralPath $Source) }
    else { $archives = @(Get-ChildItem -LiteralPath $Source -Recurse -File | Where-Object { $_.Extension -in '.sit','.sitx','.sea','.hqx','.bin','.cpt','.zip' }) }
    if (-not $archives) { throw 'No Mac sound file or Mac archive was found there.' }

    $tools = Assert-F117OutputPath -Path (Join-Path $Work 'unar')
    if (-not (Test-Path -LiteralPath (Join-Path $tools 'unar.exe'))) {
        New-Item -ItemType Directory -Force -Path $tools | Out-Null
        $zip = Join-Path $tools 'unarWindows.zip'
        if ($ArchiveFolder -and (Test-Path -LiteralPath (Join-Path $ArchiveFolder 'unarWindows.zip'))) { Copy-Item -LiteralPath (Join-Path $ArchiveFolder 'unarWindows.zip') -Destination $zip }
        else { Invoke-WebRequest -UseBasicParsing -Uri 'https://cdn.theunarchiver.com/downloads/unarWindows.zip' -OutFile $zip }
        if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne '61A6B299606282F72F51C278801EAC11D3DCCFAC83E2D68BCCCE33539912E0DD') {
            throw 'The Unarchiver download checksum mismatch. Mac sounds not imported.'
        }
        Expand-Archive -LiteralPath $zip -DestinationPath $tools
        Remove-Item -LiteralPath $zip
    }
    $unar = Join-Path $tools 'unar.exe'
    $n = 0
    foreach ($archive in $archives) {
        $n++
        $unpacked = Assert-F117OutputPath -Path (Join-Path $Work "mac-unpacked-$n")
        if (Test-Path -LiteralPath $unpacked) { Remove-Item -LiteralPath $unpacked -Recurse -Force }
        & $unar -q -o $unpacked -k visible $archive.FullName | Out-Host
        if ($LASTEXITCODE -ne 0) { continue }
        if (Try-Import $unpacked) { return @(Get-ChildItem -LiteralPath $out -Filter '*.wav').Count }
        # An archive inside an archive: one more level. (Disk images are not read: The Unarchiver 1.8.1 does not open the
        # DiskCopy 4.2 floppy images the Mac updates came as, and the updates hold no sound file anyway.)
        foreach ($inner in Get-ChildItem -LiteralPath $unpacked -Recurse -File | Where-Object { $_.Extension -in '.sit','.sea','.hqx','.bin' }) {
            $n++
            $deeper = Assert-F117OutputPath -Path (Join-Path $Work "mac-unpacked-$n")
            & $unar -q -o $deeper -k visible $inner.FullName | Out-Host
            if ($LASTEXITCODE -eq 0 -and (Try-Import $deeper)) { return @(Get-ChildItem -LiteralPath $out -Filter '*.wav').Count }
        }
    }
    throw 'The Mac archives were unpacked but none holds the game''s sound file (F117a Sound).'
}

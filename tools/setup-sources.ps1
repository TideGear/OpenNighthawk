# Fetch dependency sources at the same revisions used by CMake, without Git.
function Initialize-F117SourceDependencies {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$Destination,
        [string]$ArchiveFolder
    )
    $ErrorActionPreference = 'Stop'
    $root = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination)
    $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
    if ($root -eq $repo -or $root.StartsWith($repo + '\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Dependency sources must be outside the repository.'
    }
    if (Test-Path -LiteralPath $root) { throw 'Choose a new dependency-source folder.' }
    New-Item -ItemType Directory -Path $root | Out-Null
    $items = @(
        @{ Name='sdl-3.4.8.zip'; Url='https://codeload.github.com/libsdl-org/SDL/zip/refs/tags/release-3.4.8'; Hash='9ff97fce9ffb64abd071de3e177a26fa6c9f1283a56ed166889345666ba3ef9b'; Folder='SDL-release-3.4.8' },
        @{ Name='munt.zip'; Url='https://codeload.github.com/munt/munt/zip/6e7c01fba7e1d50c8fa705834889fd0eac136075'; Hash='596a889bf204fe522228a414127a92890ccce13e9bef431c445ac325a5ff5944'; Folder='munt-6e7c01fba7e1d50c8fa705834889fd0eac136075' }
    )
    foreach ($item in $items) {
        $zip = Join-Path $root $item.Name
        if ($ArchiveFolder) { Copy-Item -LiteralPath (Join-Path $ArchiveFolder $item.Name) -Destination $zip }
        else { Invoke-WebRequest -UseBasicParsing -Uri $item.Url -OutFile $zip }
        if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $item.Hash) {
            throw "Dependency checksum mismatch: $($item.Name). Setup stopped."
        }
        Expand-Archive -LiteralPath $zip -DestinationPath $root
        if (-not (Test-Path -LiteralPath (Join-Path $root "$($item.Folder)/CMakeLists.txt"))) {
            throw "Dependency source layout is incomplete: $($item.Name)"
        }
        Remove-Item -LiteralPath $zip
    }
    return @{
        SdlSource = Join-Path $root $items[0].Folder
        MuntSource = Join-Path $root "$($items[1].Folder)/mt32emu"
    }
}

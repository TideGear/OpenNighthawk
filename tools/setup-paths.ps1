# Validate before any output is created. Never write inside source/game folders.
function Assert-F117OutputPath {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Path,[string]$GameDirectory)
    $absolute = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path).TrimEnd('\','/')
    $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
    foreach ($protected in @($repo,$GameDirectory)) {
        if (-not $protected) { continue }
        $root = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($protected).TrimEnd('\','/')
        if ($absolute -ieq $root -or $absolute.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Choose an output folder outside the source and game installation.'
        }
    }
    $ancestor = $absolute
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Output paths through links or junctions are not supported. Choose a direct folder.'
            }
            if ($item.PSIsContainer -and ((Test-Path -LiteralPath (Join-Path $ancestor 'VGAME.EXE')) -or (Test-Path -LiteralPath (Join-Path $ancestor 'F117.COM')) -or (Test-Path -LiteralPath (Join-Path $ancestor 'F-117A/VGAME.EXE')))) {
                throw 'Output inside a game installation is forbidden. Choose another folder.'
            }
        }
        $parent = Split-Path $ancestor -Parent
        if ($parent -eq $ancestor) { break }
        $ancestor = $parent
    }
    return $absolute
}

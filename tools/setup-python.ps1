. (Join-Path $PSScriptRoot 'setup-paths.ps1')
# Private Python runtime for setup; no system Python or package installation.
function Initialize-F117Python {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$Destination,
        [string]$Archive
    )
    $ErrorActionPreference = 'Stop'
    $destinationPath = (Assert-F117OutputPath -Path $Destination)
    $repoPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
    if ($destinationPath -eq $repoPath -or $destinationPath.StartsWith($repoPath + '\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Python runtime must be outside the source folder.'
    }
    if (Test-Path -LiteralPath $destinationPath) { throw 'Choose a new Python runtime folder.' }
    $parent = Split-Path $destinationPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $temporaryArchive = Join-Path $parent ([guid]::NewGuid().ToString() + '.zip')
    try {
        if ($Archive) { Copy-Item -LiteralPath $Archive -Destination $temporaryArchive }
        else {
            Invoke-WebRequest -UseBasicParsing -Uri 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip' -OutFile $temporaryArchive
        }
        $expected = '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'
        if ((Get-FileHash -LiteralPath $temporaryArchive -Algorithm SHA256).Hash -ne $expected) {
            throw 'Python download checksum mismatch. Setup stopped.'
        }
        Expand-Archive -LiteralPath $temporaryArchive -DestinationPath $destinationPath
        $executable = Join-Path $destinationPath 'python.exe'
        & $executable -I -c 'import sys; assert sys.version_info[:3] == (3, 12, 10)'
        if ($LASTEXITCODE -ne 0) { throw 'The private Python runtime could not start.' }
        return $executable
    } finally {
        if (Test-Path -LiteralPath $temporaryArchive) { Remove-Item -LiteralPath $temporaryArchive }
    }
}

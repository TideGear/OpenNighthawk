. (Join-Path $PSScriptRoot 'setup-paths.ps1')
# Portable build tools, downloaded at setup time; no machine-wide install.
function Initialize-F117BuildTools {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Destination,[string]$ArchiveFolder)
    $ErrorActionPreference = 'Stop'
    $root = (Assert-F117OutputPath -Path $Destination)
    $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
    if ($root -eq $repo -or $root.StartsWith($repo + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Build tools must be outside the repository.' }
    if (Test-Path -LiteralPath $root) { throw 'Choose a new build-tool folder.' }
    New-Item -ItemType Directory -Path $root | Out-Null
    $items = @(
        @{Name='cmake.zip';Url='https://github.com/Kitware/CMake/releases/download/v4.0.3/cmake-4.0.3-windows-x86_64.zip';Hash='b59a31dfbfa376a4aaea9ff560ff2b29f78ee5f9fb15447fc71ae7bf9fea9379';Folder='cmake'},
        @{Name='ninja.zip';Url='https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip';Hash='f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a';Folder='ninja'},
        @{Name='dxc.zip';Url='https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2502/dxc_2025_02_20.zip';Hash='70b1913a1bfce4a3e1a5311d16246f4ecdf3a3e613abec8aa529e57668426f85';Folder='dxc'}
    )
    foreach ($item in $items) {
        $zip = Join-Path $root $item.Name
        if ($ArchiveFolder) { Copy-Item -LiteralPath (Join-Path $ArchiveFolder $item.Name) -Destination $zip }
        else { Invoke-WebRequest -UseBasicParsing -Uri $item.Url -OutFile $zip }
        if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $item.Hash) { throw "Build-tool checksum mismatch: $($item.Name). Setup stopped." }
        Expand-Archive -LiteralPath $zip -DestinationPath (Join-Path $root $item.Folder)
        Remove-Item -LiteralPath $zip
    }
    $tools = @{
        Cmake = Join-Path $root 'cmake/cmake-4.0.3-windows-x86_64/bin/cmake.exe'
        Ninja = Join-Path $root 'ninja/ninja.exe'
        Dxc = Join-Path $root 'dxc/bin/x64/dxc.exe'
    }
    foreach ($executable in $tools.Values) {
        if (-not (Test-Path -LiteralPath $executable)) { throw "Missing build tool: $executable" }
        & $executable --version | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Build tool failed to start: $executable" }
    }
    return $tools
}

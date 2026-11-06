[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Data,
    [Parameter(Mandatory=$true)][string]$Work,
    [Parameter(Mandatory=$true)][string]$Python,
    [Parameter(Mandatory=$true)][string]$SdlSource,
    [Parameter(Mandatory=$true)][string]$MuntSource,
    [Parameter(Mandatory=$true)][string]$Dxc,
    [string]$Cmake = 'cmake',
    [string]$Ninja = 'ninja',
    [ValidateSet('Zig','VisualStudio')][string]$Compiler = 'Zig',
    [ValidateRange(1,12)][int]$Jobs = 12,
    [string]$ZigArchive
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
. (Join-Path $PSScriptRoot 'setup-paths.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$destination = (Assert-F117OutputPath -Path $Work -GameDirectory $Data)
if ($destination -eq $repo -or $destination.StartsWith($repo + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Choose a work folder outside the source folder.'
}
if (Test-Path -LiteralPath $destination) { throw 'Choose a new, unused work folder for a clean build.' }
foreach ($required in @($Python,$Dxc,$SdlSource,$MuntSource,$Data)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required path is missing: $required" }
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
$timer = [Diagnostics.Stopwatch]::StartNew()

$oldPath = $env:PATH
$oldGlobal = $env:ZIG_GLOBAL_CACHE_DIR
$oldLocal = $env:ZIG_LOCAL_CACHE_DIR
$oldCpuDll = $env:F117R_CPU_DLL
$compilerEnvironmentOld = @{}
try {
    New-Item -ItemType Directory -Path $destination | Out-Null
    if ($Compiler -eq 'Zig') {
    Write-Host '## Downloading the Zig compiler (82 MB)'
    $env:PATH = (($env:PATH -split ';' | Where-Object { $_ -notmatch 'Microsoft Visual Studio|Windows Kits' }) -join ';')
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $destination 'cache-global'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $destination 'cache-local'
    $zip = Join-Path $destination 'zig.zip'
    if ($ZigArchive) { Copy-Item -LiteralPath $ZigArchive -Destination $zip }
    else { Invoke-WebRequest -UseBasicParsing -Uri 'https://ziglang.org/download/0.14.1/zig-x86_64-windows-0.14.1.zip' -OutFile $zip }
    if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne '554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c') {
        throw 'Zig download checksum mismatch. Build stopped.'
    }
    Expand-Archive -LiteralPath $zip -DestinationPath $destination
    $zig = Join-Path $destination 'zig-x86_64-windows-0.14.1/zig.exe'
        $compilerOptions = @("-DCMAKE_TOOLCHAIN_FILE=$repo/tools/cmake/zig-windows.cmake","-DF117R_ZIG_EXECUTABLE=$zig")
    } else {
        . (Join-Path $PSScriptRoot 'setup-visual-studio.ps1')
        $visualStudio = Get-F117VisualStudioEnvironment
        foreach ($key in $visualStudio.Keys) {
            $compilerEnvironmentOld[$key] = [Environment]::GetEnvironmentVariable($key,'Process')
            [Environment]::SetEnvironmentVariable($key,$visualStudio[$key],'Process')
        }
        $compilerOptions = @('-DCMAKE_C_COMPILER=cl','-DCMAKE_CXX_COMPILER=cl')
    }
    Invoke-Checked $Python @((Join-Path $repo 'tools/verify_install.py'),'--data',$Data)
    Write-Host '## Preparing the translator'
    $bootstrap = Join-Path $destination 'bootstrap'
    $configureArgs = @('-S',$repo,'-B',$bootstrap,'-G','Ninja',
        "-DCMAKE_MAKE_PROGRAM=$Ninja",'-DCMAKE_BUILD_TYPE=Release',
        '-DF117R_BUILD_APP=OFF','-DF117R_BUILD_TESTS=ON')
    Invoke-Checked $Cmake ($configureArgs + $compilerOptions)
    Invoke-Checked $Cmake @('--build',$bootstrap,'--target','f117cpu_api','--parallel',"$Jobs")
    $cpuDlls = @(Get-ChildItem -LiteralPath $bootstrap -Filter '*f117cpu_api.dll' -File)
    if ($cpuDlls.Count -ne 1) { throw 'CPU unpacking library was not built.' }
    $env:F117R_CPU_DLL = $cpuDlls[0].FullName
    $gen = Join-Path $destination 'generated'
    Write-Host '## Translating the game''s code from your copy'
    Invoke-Checked $Python @((Join-Path $repo 'recompiler/recomp.py'),'--data',$Data,'--out',$gen,'--jobs','3')
    $build = Join-Path $destination 'build'
    $configureArgs = @('-S',$repo,'-B',$build,'-G','Ninja',
        "-DCMAKE_MAKE_PROGRAM=$Ninja",'-DCMAKE_BUILD_TYPE=Release',
        '-DF117R_BUILD_TESTS=OFF',"-DF117R_GEN_DIR=$gen",
        "-DFETCHCONTENT_SOURCE_DIR_SDL3=$SdlSource","-DF117R_MT32EMU_SOURCE=$MuntSource",
        "-DF117R_DXC=$Dxc")
    Write-Host '## Compiling OpenNighthawk (the longest step, several minutes)'
    Invoke-Checked $Cmake ($configureArgs + $compilerOptions)
    Invoke-Checked $Cmake @('--build',$build,'--target','f117a','--parallel',"$Jobs")
    foreach ($file in @('f117a.exe','gpu_polys.dxil','gpu_bands_fast.dxil','gpu_strokes.dxil','gpu_replace.dxil','gpu_present.dxil')) {
        if (-not (Test-Path -LiteralPath (Join-Path $build $file))) { throw "Build output missing: $file" }
    }
    $timer.Stop()
    "Build completed in $($timer.Elapsed.TotalSeconds.ToString('F2')) seconds: $build"
} finally {
    foreach ($key in $compilerEnvironmentOld.Keys) { [Environment]::SetEnvironmentVariable($key,$compilerEnvironmentOld[$key],'Process') }
    $env:PATH = $oldPath
    $env:ZIG_GLOBAL_CACHE_DIR = $oldGlobal
    $env:ZIG_LOCAL_CACHE_DIR = $oldLocal
    $env:F117R_CPU_DLL = $oldCpuDll
}

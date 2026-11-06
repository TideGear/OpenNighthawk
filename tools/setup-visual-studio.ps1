# Obtain a Visual Studio C++ build environment without machine-wide changes.
function Get-F117VisualStudioEnvironment {
    [CmdletBinding()]
    param([string]$VcVars)
    $ErrorActionPreference = 'Stop'
    if (-not $VcVars) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($LASTEXITCODE -eq 0 -and $installation) { $VcVars = Join-Path ($installation | Select-Object -First 1) 'VC/Auxiliary/Build/vcvars64.bat' }
        }
    }
    if (-not $VcVars -or -not (Test-Path -LiteralPath $VcVars)) {
        throw 'Visual Studio C++ tools were not found. The fallback needs Visual Studio Build Tools with Desktop development with C++, including a Windows SDK. Installation may need administrator access and several GB. Install it, then run setup again with the Visual Studio option.'
    }
    $oldMarker = $env:F117_SETUP_VCVARS
    try {
        $env:F117_SETUP_VCVARS = $VcVars
        $lines = & $env:ComSpec /d /s /c 'call "%F117_SETUP_VCVARS%" >nul && set'
        if ($LASTEXITCODE -ne 0) { throw 'Visual Studio could not prepare its C++ environment.' }
        $result = @{}
        foreach ($line in $lines) {
            $at = $line.IndexOf('=')
            if ($at -le 0) { continue }
            $key = $line.Substring(0,$at)
            if ($key -eq 'F117_SETUP_VCVARS') { continue }
            $value = $line.Substring($at+1)
            if ([Environment]::GetEnvironmentVariable($key,'Process') -cne $value) { $result[$key] = $value }
        }
        foreach ($required in @('INCLUDE','LIB')) {
            $value = if ($result.ContainsKey($required)) { $result[$required] } else { [Environment]::GetEnvironmentVariable($required,'Process') }
            if (-not $value) { throw "Visual Studio environment is missing $required." }
        }
        return $result
    } finally { $env:F117_SETUP_VCVARS = $oldMarker }
}

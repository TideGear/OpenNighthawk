# What setup.cmd runs: asks for the licence agreement here (a console is needed for that), then runs tools/setup.ps1 as a child process
# and shows every line it prints (its own messages and those of Python, CMake, Ninja, Zig and dxc, standard error included) while also
# writing them to a log file, so a failed setup leaves a record to read or send. setup.ps1 itself is unchanged and can still be run directly.
# (Piping setup.ps1's output inside one Windows PowerShell 5.1 process would turn any tool's warning on standard error into a fatal error.)
[CmdletBinding()]
param([string]$Data,[string]$Work,[string]$ArchiveFolder,[ValidateSet('Zig','VisualStudio')][string]$Compiler='Zig',[string]$ShortcutPath,[switch]$AcceptLicences)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'setup-paths.ps1')
. (Join-Path $PSScriptRoot 'setup-licences.ps1')

$logDir = Assert-F117OutputPath -Path (Join-Path $env:LOCALAPPDATA 'OpenNighthawk\logs') -GameDirectory $Data
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$log = Join-Path $logDir ('setup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$writer = New-Object IO.StreamWriter($log, $false, (New-Object Text.UTF8Encoding($false)))
$writer.AutoFlush = $true
function Write-Both([string]$line) { Write-Host $line; $writer.WriteLine($line) }

try {
    Write-Both ('OpenNighthawk setup, ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '; log: ' + $log)
    Write-Both ('Windows ' + [Environment]::OSVersion.Version + ', PowerShell ' + $PSVersionTable.PSVersion)
    if (-not $AcceptLicences) {
        Confirm-F117Licences -Compiler $Compiler
        $writer.WriteLine('Licence terms accepted at the prompt.')
    }

    # The child's arguments, each quoted (paths may contain spaces); -AcceptLicences because the question has been answered here.
    $quote = { param($v) '"' + ($v -replace '"', '\"') + '"' }
    $argsList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (& $quote (Join-Path $PSScriptRoot 'setup.ps1')), '-AcceptLicences', '-Compiler', $Compiler)
    foreach ($name in 'Data', 'Work', 'ArchiveFolder', 'ShortcutPath') {
        $value = Get-Variable -Name $name -ValueOnly
        if ($value) { $argsList += @("-$name", (& $quote $value)) }
    }
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $env:ComSpec
    # cmd joins standard error to standard output (2>&1), so one stream is read line by line: no deadlock, nothing lost.
    $psi.Arguments = '/d /s /c "powershell.exe ' + ($argsList -join ' ') + ' 2>&1"'
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.CreateNoWindow = $false
    $child = [Diagnostics.Process]::Start($psi)
    while ($null -ne ($line = $child.StandardOutput.ReadLine())) { Write-Both $line }
    $child.WaitForExit()
    $code = $child.ExitCode
    Write-Both ('Setup exit code ' + $code + '. Log: ' + $log)
    exit $code
} catch {
    Write-Both ('SETUP FAILED: ' + $_.Exception.Message)
    Write-Both ('Log: ' + $log)
    exit 1
} finally {
    $writer.Dispose()
}

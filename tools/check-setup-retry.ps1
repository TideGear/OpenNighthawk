# Developer check: creates only a fresh private scratch tree; never removes it.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Scratch)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'setup-paths.ps1')
. (Join-Path $PSScriptRoot 'setup-retry.ps1')
$Scratch=Assert-F117OutputPath -Path $Scratch
if(Test-Path -LiteralPath $Scratch){throw 'Choose an unused check folder'}
New-Item -ItemType Directory -Path $Scratch | Out-Null
function Expect-Rejection([scriptblock]$Action,[string]$Message) {
    try { & $Action } catch {
        if($_.Exception.Message -notlike $Message){throw}
        Write-Host "PASS: $Message"
        return
    }
    throw "Expected rejection: $Message"
}
$work=Join-Path $Scratch 'work'
$handle=Open-F117SetupWork -Path $work
try { Expect-Rejection {Open-F117SetupWork -Path $work} 'Setup is already using*' }
finally {$handle.Dispose()}
New-Item -ItemType Directory -Path (Join-Path $work 'python') | Out-Null
Set-Content (Join-Path $work 'python/partial') 'interrupted'
$handle=Open-F117SetupWork -Path $work
$handle.Dispose()
if(Test-Path (Join-Path $work 'python')){throw 'Partial output was retained'}
Write-Host 'PASS: incomplete output cleaned'
Set-Content (Join-Path $work 'owner-file.txt') 'keep'
Expect-Rejection {Open-F117SetupWork -Path $work} 'Unrecognized file*'
if((Get-Content (Join-Path $work 'owner-file.txt')) -ne 'keep'){throw 'Owner file changed'}
$linkWork=Join-Path $Scratch 'linked-work'
$handle=Open-F117SetupWork -Path $linkWork
$handle.Dispose()
New-Item -ItemType Directory -Path (Join-Path $linkWork 'tools') | Out-Null
$target=Join-Path $Scratch 'link-target'
New-Item -ItemType Directory -Path $target | Out-Null
Set-Content (Join-Path $target 'sentinel') 'keep'
New-Item -ItemType Junction -Path (Join-Path $linkWork 'tools/nested-link') -Target $target | Out-Null
Expect-Rejection {Open-F117SetupWork -Path $linkWork} 'Output paths through links*'
if((Get-Content (Join-Path $target 'sentinel')) -ne 'keep'){throw 'Junction target changed'}
$lockWork=Join-Path $Scratch 'linked-lock'
$handle=Open-F117SetupWork -Path $lockWork
$handle.Dispose()
Remove-Item -LiteralPath (Join-Path $lockWork '.f117-setup-running')
New-Item -ItemType Junction -Path (Join-Path $lockWork '.f117-setup-running') -Target $target | Out-Null
Expect-Rejection {Open-F117SetupWork -Path $lockWork} 'Output paths through links*'
$game=Join-Path $Scratch 'game-install'
New-Item -ItemType Directory -Path $game | Out-Null
Set-Content (Join-Path $game 'VGAME.EXE') 'path guard fixture only'
Expect-Rejection {Open-F117SetupWork -Path (Join-Path $game 'work')} 'Output inside a game installation*'
if(Test-Path (Join-Path $game 'work')){throw 'Game install output created'}
$done=Join-Path $Scratch 'completed'
$handle=Open-F117SetupWork -Path $done
$handle.Dispose()
Set-Content (Join-Path $done '.f117-setup-complete') 'retained app'
Expect-Rejection {Open-F117SetupWork -Path $done} 'Setup already finished*'
Write-Host 'All setup retry safety checks passed.'

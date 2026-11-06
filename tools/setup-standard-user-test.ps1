# Developer validation only. Admin mode creates one temporary standard account;
# downloaded tools and the game run exclusively in Worker mode under that user.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('Admin','Worker')][string]$Mode,
      [Parameter(Mandatory=$true)][string]$TestRoot,[string]$ExpectedExistingSid)
$ErrorActionPreference='Stop'
$account='F117SetupTest'
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin=([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if($Mode -eq 'Worker') {
    if($isAdmin -or $identity.Name.Split('\')[-1] -ne $account){throw 'Worker must run as the standard test account'}
    $profile=[Environment]::GetFolderPath('UserProfile')
    if(-not $profile){throw 'Windows did not load the test user profile'}
    $env:USERPROFILE=$profile
    $env:LOCALAPPDATA=Join-Path $profile 'AppData/Local'
    $env:APPDATA=Join-Path $profile 'AppData/Roaming'
    $env:TEMP=Join-Path $env:LOCALAPPDATA 'Temp';$env:TMP=$env:TEMP
    New-Item -ItemType Directory -Force -Path $env:TEMP|Out-Null
    $env:PATH="$env:SystemRoot\System32;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    foreach($name in @('INCLUDE','LIB','LIBPATH','CC','CXX','CMAKE_PREFIX_PATH','CMAKE_TOOLCHAIN_FILE','VSINSTALLDIR','VCINSTALLDIR','VCToolsInstallDir','WindowsSdkDir','WindowsSDKVersion','PYTHONHOME','PYTHONPATH','F117R_CPU_DLL')) {
        [Environment]::SetEnvironmentVariable($name,$null,'Process')
    }
    $found=@(Get-Command git,cl,cmake,ninja,python,python3,py -ErrorAction SilentlyContinue)
    if($found.Count){throw 'Developer tools are present on the test PATH'}
    $report=@{user=$identity.Name;sid=$identity.User.Value;administrator=$isAdmin;profile=$profile;path=$env:PATH;developerToolsOnPath=@();setupPassed=$false;shortcutLaunched=$false}
    $resultFile=Join-Path $TestRoot 'results/standard-user.json'
    try {
        $work=Join-Path $env:LOCALAPPDATA ('OpenNighthawk/standard-test-'+(Get-Date -Format yyyyMMdd-HHmmss))
        & (Join-Path $TestRoot 'source/tools/setup.ps1') -Data (Join-Path $TestRoot 'data') -Work $work -AcceptLicences *> (Join-Path $TestRoot 'results/setup.log')
        $exe=Join-Path $work 'game/build/f117a.exe'
        if(-not(Test-Path -LiteralPath $exe)){throw 'Setup did not produce the app'}
        $report.setupPassed=$true;$report.work=$work
        $shortcut=Join-Path ([Environment]::GetFolderPath('Desktop')) 'OpenNighthawk.lnk'
        if(-not(Test-Path -LiteralPath $shortcut)){throw 'Default desktop shortcut missing'}
        $shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($shortcut)
        if($link.TargetPath -ine $exe){throw 'Wrong shortcut target'}
        $env:F117_HIDDEN_WINDOW='1';$env:SDL_AUDIODRIVER='dummy';$env:SDL_RENDER_DRIVER='software'
        $process=Start-Process -FilePath $shortcut -PassThru -WindowStyle Hidden
        try {
            Start-Sleep -Seconds 10
            if($process.HasExited){throw "Shortcut app exited early: $($process.ExitCode)"}
            $owner=Invoke-CimMethod -InputObject (Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)") -MethodName GetOwner
            if($owner.User -ne $account){throw 'Game did not run under the standard account'}
            $report.shortcutLaunched=$true;$report.gameProcessId=$process.Id;$report.gameOwner=$owner.User
            $report.hiddenWindowRequested=$true;$report.audioDriver=$env:SDL_AUDIODRIVER
        } finally {if($process -and -not $process.HasExited){$process.Kill();$process.WaitForExit()}}
    } catch {$report.error=$_.Exception.Message;throw}
    finally {$report|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $resultFile -Encoding UTF8}
    exit 0
}
if(-not $isAdmin){throw 'Admin mode requires the owner-approved Windows administrator prompt'}
$existing=Get-LocalUser -Name $account -ErrorAction SilentlyContinue
if($existing -and ($existing.Enabled -or $existing.SID.Value -ne $ExpectedExistingSid -or $existing.Description -ne 'Temporary OpenNighthawk standard-user validation')){throw 'Refusing to modify an unrecognized or enabled account'}
$created=$false
try {
    $bytes=New-Object byte[] 32;$rng=[Security.Cryptography.RandomNumberGenerator]::Create();$rng.GetBytes($bytes);$rng.Dispose()
    $password=ConvertTo-SecureString ('F117test!'+[Convert]::ToBase64String($bytes)) -AsPlainText -Force
    if($existing){Set-LocalUser -Name $account -Password $password;Enable-LocalUser -Name $account;$user=Get-LocalUser -Name $account}
    else {$user=New-LocalUser -Name $account -Password $password -AccountNeverExpires -Description 'Temporary OpenNighthawk standard-user validation'}
    $created=$true
    if (-not (Get-LocalGroupMember -SID ([Security.Principal.SecurityIdentifier]'S-1-5-32-545') | Where-Object SID -eq $user.SID)) { Add-LocalGroupMember -SID ([Security.Principal.SecurityIdentifier]'S-1-5-32-545') -Member $user }
    if(Get-LocalGroupMember -SID ([Security.Principal.SecurityIdentifier]'S-1-5-32-544')|Where-Object SID -eq $user.SID){throw 'Test account unexpectedly has administrator membership'}
    foreach($item in @(@('source','ReadAndExecute'),@('data','ReadAndExecute'),@('results','Modify'))) {
        $folder=Join-Path $TestRoot $item[0]
        $acl=Get-Acl -LiteralPath $folder
        $acl.SetAccessRuleProtection($true,$false)
        foreach($sid in @('S-1-5-18','S-1-5-32-544')){$acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]$sid,'FullControl','ContainerInherit,ObjectInherit','None','Allow'))}
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($user.SID,$item[1],'ContainerInherit,ObjectInherit','None','Allow'))
        $ownerSid=(Get-Acl -LiteralPath $TestRoot).GetOwner([Security.Principal.SecurityIdentifier])
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($ownerSid,'FullControl','ContainerInherit,ObjectInherit','None','Allow'))
        Set-Acl -LiteralPath $folder -AclObject $acl
    }
    $credential=[PSCredential]::new("$env:COMPUTERNAME\$account",$password)
    $arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Mode Worker -TestRoot "'+$TestRoot+'"'
    $worker=Start-Process -FilePath "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -ArgumentList $arguments -Credential $credential -LoadUserProfile -WindowStyle Hidden -PassThru
    $worker.WaitForExit()
    @{workerExitCode=$worker.ExitCode;account=$account;created=$true}|ConvertTo-Json|Set-Content (Join-Path $TestRoot 'admin-result.json')
} catch {@{error=$_.Exception.Message;accountCreated=$created}|ConvertTo-Json|Set-Content (Join-Path $TestRoot 'admin-result.json');throw}
finally {if($created){Disable-LocalUser -Name $account}}

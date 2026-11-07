# What setup.cmd runs. Asks the questions (install folder, shortcuts, optional presets and sound), runs tools/setup.ps1 as a child process,
# shows only its stage lines ('## ...') and a live progress line, writes everything to a log, installs the settings, and removes the build
# workspace. A Windows PowerShell 5.1 pipeline inside one process would turn any tool's warning on standard error into a fatal error, so the
# build runs in a child whose combined output is read line by line.
# Scripted use: -AcceptLicences asks nothing and takes the defaults (or -InstallDir, -NoDesktopShortcut, -StartMenu, -KeepBuildFiles).
[CmdletBinding()]
param([string]$Data,[string]$Work,[string]$ArchiveFolder,[ValidateSet('Zig','VisualStudio')][string]$Compiler='Zig',
      [string]$InstallDir,[switch]$NoDesktopShortcut,[switch]$StartMenu,[switch]$KeepBuildFiles,[string]$MacSource,[string]$Mt32Roms,
      [switch]$AcceptLicences)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'setup-paths.ps1')
. (Join-Path $PSScriptRoot 'setup-licences.ps1')

$interactive = -not $AcceptLicences
$started = [Diagnostics.Stopwatch]::StartNew()
$logLines = [Collections.Generic.List[string]]::new()      # kept in memory until the log's folder is known
$logPath = $null
$writer = $null
function Log([string]$line) {
    if ($writer) { $writer.WriteLine($line) } else { $logLines.Add($line) }
}
# Colours: the CGA's palette 1 (cyan, magenta, white): headings and success cyan, questions and anything to notice magenta, text white,
# hints light grey. Text is wrapped at word boundaries to the window (Write-F117Text, setup-licences.ps1).
function Say([string]$line, [string]$color = 'White') { Write-F117Text $line $color; Log $line }
function Open-Log([string]$folder) {
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $script:logPath = Join-Path $folder ('setup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
    $script:writer = New-Object IO.StreamWriter($script:logPath, $false, (New-Object Text.UTF8Encoding($false)))
    $script:writer.AutoFlush = $true
    foreach ($l in $logLines) { $script:writer.WriteLine($l) }
    $logLines.Clear()
}
# A question with a free answer: the question in magenta, what Enter does in grey, the answer typed after '>'.
function Ask([string]$question, [string]$default, [string]$enter) {
    Write-F117Text $question 'Magenta'
    if ($enter) { Write-F117Text "  $enter" 'Gray' }
    Write-Host '  > ' -ForegroundColor Magenta -NoNewline
    $answer = Read-Host
    Log "$question -> $answer"
    if ([string]::IsNullOrWhiteSpace($answer)) { return $default }
    return $answer.Trim()
}
# A short question answered on the same line: [y/n, Enter = yes].
function Ask-Inline([string]$question, [string]$hint) {
    Write-Host "$question " -ForegroundColor Magenta -NoNewline
    Write-Host "[$hint] " -ForegroundColor Gray -NoNewline
    $answer = Read-Host
    Log "$question [$hint] -> $answer"
    return $answer.Trim()
}
function Ask-YesNo([string]$question, [bool]$default) {
    while ($true) {
        $a = Ask-Inline $question $(if ($default) { 'y/n, Enter = yes' } else { 'y/n, Enter = no' })
        if (-not $a) { return $default }
        if ($a -match '^(y|yes)$') { return $true }
        if ($a -match '^(n|no)$') { return $false }
        Write-Host '  Please answer y or n.' -ForegroundColor Magenta
    }
}
function Ask-Pick([string]$question, [string[]]$labels, [int]$default) {
    Write-F117Text $question 'Magenta'
    for ($i = 0; $i -lt $labels.Count; $i++) { Write-F117Text ("  {0}. {1}" -f ($i + 1), $labels[$i]) }
    while ($true) {
        $a = Ask-Inline '  Your choice' ("1-$($labels.Count), Enter = $($default + 1)")
        if (-not $a) { return $default }
        $n = 0
        if ([int]::TryParse($a, [ref]$n) -and $n -ge 1 -and $n -le $labels.Count) { return $n - 1 }
        Write-Host "  Please type a number from 1 to $($labels.Count)." -ForegroundColor Magenta
    }
}
# Delete a folder in a background job, showing a moving line meanwhile (deleting about 1.4 GB takes several seconds).
function Remove-FolderVisibly([string]$folder, [string]$what) {
    Write-Host ("[{0:mm\:ss}] " -f $started.Elapsed) -ForegroundColor DarkCyan -NoNewline
    Write-Host $what -ForegroundColor White
    Log "$what ($folder)"
    $job = Start-Job -ScriptBlock { param($p) Remove-Item -LiteralPath $p -Recurse -Force } -ArgumentList $folder
    $spin = '|/-\'; $k = 0; $clock = [Diagnostics.Stopwatch]::StartNew(); $live = $true
    try { $null = [Console]::WindowWidth; if ([Console]::IsOutputRedirected) { $live = $false } } catch { $live = $false }
    while ($job.State -eq 'Running' -or $job.State -eq 'NotStarted') {
        if ($live) { [Console]::Write(("`r  {0} deleting  {1:mm\:ss}   " -f $spin[$k % 4], $clock.Elapsed)) }
        $k++; Start-Sleep -Milliseconds 250
    }
    if ($live) { [Console]::Write("`r" + (' ' * 30) + "`r") }
    Receive-Job -Job $job -ErrorAction Stop | Out-Null
    Remove-Job -Job $job
}
# A typed or pasted path: surrounding quotes and spaces removed, made absolute, and a trailing \ or / dropped (except a drive's root).
function Normalize-Path([string]$p) {
    $p = $p.Trim().Trim('"').Trim()
    if (-not $p) { return $null }
    try { $full = [IO.Path]::GetFullPath($p) } catch { return $null }        # characters a path cannot hold: $null, so the question is asked again
    if ($full.Length -gt 3) { $full = $full.TrimEnd('\', '/') }
    return $full
}
# MT-32 ROMs are recognised by SHA-1 (docs/mt32-roms.md): a usable folder holds at least one control ROM and one PCM ROM.
$mt32Control = @('b083518fffb7f66b03c23b7eb4f868e62dc5a987','a553481f4e2794c10cfe597fef154eef0d8257de','e17a3a6d265bf1fa150312061134293d2b58288c',
    '5a5cb5a77d7d55ee69657c2f870416daed52dea7','7b8c2a5ddb42fd0732e2f22b3340dcf5360edf92','47b52adefedaec475c925e54340e37673c11707c',
    '2869cf4c235d671668cfcb62415e2ce8323ad4ed','2c16432b6c73dd2a3947cba950a0f4c19d6180eb','5837064c9df4741a55f7c4d8787ac158dff2d3ce',
    'a439fbb390da38cada95a7cbb1d6ca199cd66ef8','73683d585cd6948cc19547942ca0e14a0319456d','dc1c5b1b90a4646d00f7daf3679733c7badc7077')
$mt32Pcm = @('f6b1eebc4b2d200ec6d3d21d51325d5b48c60252','289cc298ad532b702461bfc738009d9ebe8025ea')
function Test-Mt32Folder([string]$folder) {
    if (-not (Test-Path -LiteralPath $folder -PathType Container)) { return 'not a folder' }
    $hashes = @(Get-ChildItem -LiteralPath $folder -File | Where-Object { $_.Length -le 2MB } |
        ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA1).Hash.ToLowerInvariant() })
    $control = @($hashes | Where-Object { $_ -in $mt32Control }).Count
    $pcm = @($hashes | Where-Object { $_ -in $mt32Pcm }).Count
    if ($control -and $pcm) { return $null }
    if (-not $control -and -not $pcm) { return 'no MT-32 ROM files recognised there' }
    if (-not $control) { return 'the control ROM is missing (the PCM ROM was found)' }
    return 'the PCM ROM is missing (the control ROM was found)'
}

$exitCode = 1
try {
    Say 'OpenNighthawk Setup Wizard' 'Cyan'
    Say 'This builds OpenNighthawk on this PC from your own copy of F-117A (GOG or Steam). It takes about 5 to 15 minutes.'
    Log ('Started ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '; Windows ' + [Environment]::OSVersion.Version + ', PowerShell ' + $PSVersionTable.PSVersion)

    # Where things go. The build workspace is temporary; the install folder holds only what the game needs.
    if (-not $Work) { $Work = Join-Path $env:LOCALAPPDATA 'OpenNighthawk\setup-work' }
    $Work = Assert-F117OutputPath -Path $Work -GameDirectory $Data
    $defaultInstall = Join-Path $env:LOCALAPPDATA 'Programs\OpenNighthawk'

    # An earlier install: one setup made (listed in Apps & features) is updated in place; a build left in the workspace by the
    # first version of setup (no install folder) is replaced, and its settings carried over.
    $existing = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNighthawk' -ErrorAction SilentlyContinue).InstallLocation
    if ($existing -and (Test-Path -LiteralPath (Join-Path $existing 'f117a.exe'))) {
        $defaultInstall = $existing
        Say "OpenNighthawk is already installed in $existing; setup will update it. Saved pilots are kept."
    }
    $oldIni = $null
    $oldBuild = Join-Path $Work 'game\build'
    if ((Test-Path -LiteralPath (Join-Path $Work '.f117-setup-complete')) -and (Test-Path -LiteralPath (Join-Path $oldBuild 'f117a.exe'))) {
        if (Test-Path -LiteralPath (Join-Path $oldBuild 'f117a.ini')) { $oldIni = [IO.File]::ReadAllLines((Join-Path $oldBuild 'f117a.ini')) }
        Say "An earlier OpenNighthawk build was found in $oldBuild; setup replaces it with an installed copy$(if ($oldIni) { ' and keeps its settings' })."
    }

    # Disk space: the build needs room for its tools and its intermediate files, most of which are removed at the end.
    $drive = [IO.Path]::GetPathRoot($Work)
    $free = (New-Object IO.DriveInfo($drive)).AvailableFreeSpace
    Log ("Free space on {0}: {1:N1} GB" -f $drive, ($free / 1GB))
    if ($free -lt 2GB) {
        throw ("Setup needs about 1.5 GB free on {0} while it builds, and {1:N1} GB is free. The build tools (about 250 MB) and the compiler's intermediate files for the translated game and its libraries (about 1.2 GB) are kept until the build finishes, then removed; the installed game is about 35 MB." -f $drive, ($free / 1GB))
    }

    if ($interactive) {
        Confirm-F117Licences -Compiler $Compiler
        Log 'Licence terms accepted.'
        Write-F117Heading 'Install location'
        Write-F117Text 'While it builds, setup needs about 1.5 GB of free space for the build tools and the compiler''s intermediate files. It deletes them at the end; the installed game is about 35 MB.'
        Write-F117Text 'The default is the folder Windows provides for programs installed for your user only: no administrator rights are needed, and the game can keep its settings file beside itself. Any folder you can write to works too, for example D:\Games\OpenNighthawk (not inside your F-117A game folder).' 'Gray'
        $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
        $given = $InstallDir
        while ($true) {
            $typed = if ($given) { $given } else { Ask 'Install folder' $defaultInstall "Press Enter to use $defaultInstall, or type another folder." }
            $given = $null
            $candidate = Normalize-Path $typed
            $why = $null
            if (-not $candidate) { $why = 'that is not a valid folder path' }
            elseif ($candidate -ieq $repo -or $candidate.StartsWith($repo + '\', [StringComparison]::OrdinalIgnoreCase)) { $why = 'it is inside this source folder' }
            elseif ($candidate -ieq $Work -or $candidate.StartsWith($Work + '\', [StringComparison]::OrdinalIgnoreCase)) { $why = 'it is inside the temporary build workspace' }
            else { try { $null = Assert-F117OutputPath -Path $candidate -GameDirectory $Data } catch { $why = $_.Exception.Message } }
            if (-not $why) { $InstallDir = $candidate; break }
            Write-F117Text "  That folder cannot be used: $why. Please choose another." 'Magenta'
        }
        if (-not $NoDesktopShortcut) { $NoDesktopShortcut = -not (Ask-YesNo 'Add a desktop shortcut?' $true) }
        if (-not $StartMenu) { $StartMenu = Ask-YesNo 'Add OpenNighthawk to the Start menu?' $true }
    } elseif (-not $InstallDir) { $InstallDir = $defaultInstall }
    $InstallDir = Assert-F117OutputPath -Path $InstallDir -GameDirectory $Data
    $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
    if ($InstallDir -ieq $repo -or $InstallDir.StartsWith($repo + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Choose an install folder outside the source folder.' }
    if ($InstallDir -ieq $Work -or $InstallDir.StartsWith($Work + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Choose an install folder outside the build workspace.' }
    Open-Log (Join-Path $InstallDir 'logs')
    Log "Install folder: $InstallDir"

    # Sound and optional settings. Whatever is chosen, the in-game menu (the ` key) changes all of it later.
    $ini = Join-Path $InstallDir 'f117a.ini'
    $settings = [ordered]@{}
    $keep = $false
    if (-not $interactive) {
        # Scripted: keep any existing settings; the options given are merged in.
        $keep = [bool]((Test-Path -LiteralPath $ini) -or $oldIni)
        if ($Mt32Roms) {
            $problem = Test-Mt32Folder $Mt32Roms
            if ($problem) { throw "-Mt32Roms: $problem." }
            $settings['sound'] = 'roland'; $settings['roland'] = 'munt'; $settings['mt32-roms'] = Normalize-Path $Mt32Roms
        }
        if ($MacSource) {
            if (-not (Test-Path -LiteralPath $MacSource)) { throw "-MacSource: not found: $MacSource" }
            $MacSource = Normalize-Path $MacSource
        }
    }
    if ($interactive) {
        Write-F117Heading 'Sound'
        Write-F117Text 'The game plays its AdLib music, effects and speech, the sound most players know. Two optional extras come from your own files: the music as a Roland MT-32 played it (needs the two MT-32 ROM files), and the Macintosh version''s recorded sounds, music and speech (needs your Mac copy of F-117A). You can skip both if you are happy with AdLib.'
        if (Ask-YesNo 'Do you have MT-32 ROM files to use?' $false) {
            while ($true) {
                $folder = Ask 'Folder holding the two ROM files' 'skip' 'Type or paste the folder, or press Enter to skip.'
                if ($folder -eq 'skip') { break }
                $folder = Normalize-Path $folder
                $problem = if ($folder) { Test-Mt32Folder $folder } else { 'that is not a valid folder path' }
                if (-not $problem) {
                    $settings['sound'] = 'roland'; $settings['roland'] = 'munt'; $settings['mt32-roms'] = $folder
                    Say '  MT-32 ROMs recognised.' 'Cyan'
                    break
                }
                Write-F117Text "  Not usable: $problem." 'Magenta'
            }
        }
        if (Ask-YesNo 'Do you have the Mac version of F-117A?' $false) {
            while ($true) {
                $m = Ask 'Your Mac copy: its StuffIt archive (.sit), a folder holding it, or an unpacked copy' 'skip' 'Type or paste the path, or press Enter to skip.'
                if ($m -eq 'skip') { break }
                $m = Normalize-Path $m
                if ($m -and (Test-Path -LiteralPath $m)) { $MacSource = $m; Say '  The Mac sounds will be imported during setup.' 'Cyan'; break }
                Write-F117Text '  That file or folder does not exist.' 'Magenta'
            }
        }
        Write-F117Heading 'Settings'
        if ((Test-Path -LiteralPath $ini) -or $oldIni) { $keep = Ask-YesNo 'Keep your current OpenNighthawk settings?' $true }
        if (-not $keep) {
            Write-F117Text 'Choose your settings now, or take the defaults. Either way you can change everything later in the game: press the ` key (left of 1) for the settings menu.'
            if (Ask-YesNo 'Choose settings now?' $false) {
                $v = Ask-Pick 'Visuals' @('Original - the 1991 picture, pixel for pixel',
                    'Enhanced - the original picture, smoother and sharper at higher resolutions',
                    'Remastered - the 3-D world redrawn at your screen''s resolution, widescreen, longer draw distance') 0
                $settings['presentation'] = @('original','enhanced','remastered')[$v]
                if (Ask-YesNo 'Start in full screen?' $false) { $settings['fullscreen'] = 'yes' }
                $f = Ask-Pick 'Fixes: OpenNighthawk can repair bugs in the original game (crashes, mistakes in missions and scoring).' @('Recommended fixes on (the default)','Original behaviour, bugs included') 0
                if ($f -eq 1) { $settings['fixes'] = 'original' }
            }
        }
    }

    # The build, in a child process: its stage lines are shown, everything goes to the log.
    # Windows command-line quoting: backslashes before a quote (including the closing one) are doubled, so "C:\folder\" stays one argument.
    $quote = { param($v) '"' + (($v -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"' }
    $argsList = @('-NoProfile','-ExecutionPolicy','Bypass','-File',(& $quote (Join-Path $PSScriptRoot 'setup.ps1')),'-AcceptLicences',
        '-Compiler',$Compiler,'-InstallDir',(& $quote $InstallDir),'-Work',(& $quote $Work))
    if ($Data) { $argsList += @('-Data',(& $quote $Data)) }
    if ($ArchiveFolder) { $argsList += @('-ArchiveFolder',(& $quote $ArchiveFolder)) }
    if ($NoDesktopShortcut) { $argsList += '-NoDesktopShortcut' }
    if ($StartMenu) { $argsList += '-StartMenu' }
    if ($MacSource) { $argsList += @('-MacSource',(& $quote $MacSource)) }

    # A finished workspace from an earlier run is removed first, so a rerun (an update) builds fresh. Only a folder setup made is touched.
    $marker = Join-Path $Work '.f117-setup-work'
    Write-F117Heading 'Building'
    if ((Test-Path -LiteralPath (Join-Path $Work '.f117-setup-complete')) -and (Test-Path -LiteralPath $marker) -and
        [IO.File]::ReadAllText($marker) -eq 'OpenNighthawk setup work v1') {
        Remove-FolderVisibly $Work 'Removing the earlier build (its files, about 1.4 GB)'
    }

    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $env:ComSpec
    $psi.Arguments = '/d /s /c "powershell.exe ' + ($argsList -join ' ') + ' 2>&1"'
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.CreateNoWindow = $true
    $child = [Diagnostics.Process]::Start($psi)
    $spin = '|/-\'; $tick = 0; $status = ''; $recent = [Collections.Generic.Queue[string]]::new()
    $stage = ''; $stageClock = [Diagnostics.Stopwatch]::StartNew(); $progress = $null
    $hasConsole = $true
    try { $null = [Console]::WindowWidth; if ([Console]::IsOutputRedirected) { $hasConsole = $false } } catch { $hasConsole = $false }
    function Line-Width { if ($hasConsole) { [Math]::Max(20, [Console]::WindowWidth - 1) } else { 79 } }
    function Show-Status([string]$text) {
        if (-not $hasConsole) { return }       # redirected output (a scripted run): no moving line, the stage lines suffice
        $width = Line-Width
        if ($text.Length -gt $width) { $text = $text.Substring(0, $width) }
        [Console]::Write("`r" + $text.PadRight($width))
        $script:status = $text
    }
    function Clear-Status { if ($script:status) { [Console]::Write("`r" + (' ' * (Line-Width)) + "`r"); $script:status = '' } }
    $reader = $child.StandardOutput
    $task = $reader.ReadLineAsync()
    while ($true) {
        if (-not $task.Wait(250)) {
            # Still working: keep a moving line so the window never looks stalled.
            $tick++
            $elapsed = $stageClock.Elapsed
            Show-Status ("  {0} {1}  {2:mm\:ss}" -f $spin[$tick % 4], $(if ($progress) { $progress } else { 'working' }), $elapsed)
            continue
        }
        $line = $task.Result
        if ($null -eq $line) { break }
        $task = $reader.ReadLineAsync()
        Log $line
        $recent.Enqueue($line); if ($recent.Count -gt 40) { [void]$recent.Dequeue() }
        if ($line.StartsWith('## ')) {
            Clear-Status
            $progress = $null
            $stage = $line.Substring(3)
            $stageClock.Restart()
            Write-Host ("[{0:mm\:ss}] " -f $started.Elapsed) -ForegroundColor DarkCyan -NoNewline
            Write-Host $stage -ForegroundColor $(if ($stage -like '*not imported*') { 'Magenta' } else { 'White' })
        } elseif ($line -match '^\[(\d+)/(\d+)\]') {
            $progress = "compiled $($Matches[1]) of $($Matches[2]) files"
        }
    }
    $child.WaitForExit()
    Clear-Status
    $exitCode = $child.ExitCode
    Log "Build exit code $exitCode"
    if ($exitCode -ne 0) {
        Write-Host ''
        Write-Host 'Setup stopped. The last lines of the build output:' -ForegroundColor Magenta
        foreach ($l in $recent) { if ($l.Trim()) { Write-Host "  $l" -ForegroundColor Gray } }
        throw "The build failed (exit code $exitCode)."
    }

    # The Mac recordings, if they were imported, are used by default (the game's own order: Mac, then Roland, then AdLib).
    $macDir = Join-Path $InstallDir 'mac-sounds'
    if ($MacSource -and (Test-Path -LiteralPath $macDir) -and @(Get-ChildItem -LiteralPath $macDir -Filter '*.wav').Count) { $settings['mac-sounds'] = $macDir }

    # Settings: kept (this install's f117a.ini, or the earlier build's) when the user said so, with the sound choices merged in;
    # otherwise written from the choices. Lines are 'key = value'.
    $lines = [Collections.Generic.List[string]]::new()
    if ($keep -and (Test-Path -LiteralPath $ini)) { $lines.AddRange([string[]][IO.File]::ReadAllLines($ini)) }
    elseif ($keep -and $oldIni) { $lines.AddRange([string[]]$oldIni) }
    else { $lines.Add('; OpenNighthawk settings, written by setup. Change them in the game with the ` key.') }
    foreach ($k in $settings.Keys) {
        $set = "$k = $($settings[$k])"
        $i = -1
        for ($j = 0; $j -lt $lines.Count; $j++) { if ($lines[$j] -match ('^\s*' + [regex]::Escape($k) + '\s*=')) { $i = $j } }
        if ($i -ge 0) { $lines[$i] = $set } else { $lines.Add($set) }
    }
    if ($settings.Count -or -not $keep -or -not (Test-Path -LiteralPath $ini)) {
        [IO.File]::WriteAllLines($ini, $lines)
        Log ('Settings written: ' + (($lines | Where-Object { $_ -notmatch '^\s*;' }) -join ', '))
    }

    # A desktop shortcut left by the earlier build points into the workspace being removed: drop it unless setup just replaced it.
    $desk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'OpenNighthawk.lnk'
    if (Test-Path -LiteralPath $desk) {
        $t = (New-Object -ComObject WScript.Shell).CreateShortcut($desk).TargetPath
        if ($t -and $t.StartsWith($Work + '\', [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $desk -Force; Log 'Removed the earlier build''s desktop shortcut.' }
    }

    # Clean up the build workspace (about 1.4 GB) unless asked to keep it.
    if ($KeepBuildFiles) { Say "Build files kept in $Work" }
    else {
        if ((Test-Path -LiteralPath $marker) -and [IO.File]::ReadAllText($marker) -eq 'OpenNighthawk setup work v1') {
            Remove-FolderVisibly $Work 'Removing the build files (about 1.4 GB)'
            $parent = Split-Path $Work -Parent
            if ((Split-Path $parent -Leaf) -eq 'OpenNighthawk' -and -not (Get-ChildItem -LiteralPath $parent -Force)) { Remove-Item -LiteralPath $parent -Force }
        }
        Log 'Build workspace removed.'
    }

    Write-Host ''
    Say ("OpenNighthawk is installed in {0} ({1:mm\:ss})." -f $InstallDir, $started.Elapsed) 'Cyan'
    if ($NoDesktopShortcut -and -not $StartMenu) { Say ('Start it with ' + (Join-Path $InstallDir 'f117a.exe') + '. Press the ` key in the game for settings.') }
    else { Say 'Start it from the shortcut. Press the ` key in the game for settings.' }
    Say "Setup log: $logPath" 'Gray'
    $exitCode = 0
} catch {
    Write-Host ''
    Say ('Setup did not finish: ' + $_.Exception.Message) 'Magenta'
    if (-not $writer) { Open-Log (Join-Path $env:TEMP 'OpenNighthawk-setup') }
    Say "Setup log: $logPath" 'Gray'
    $exitCode = 1
} finally {
    if ($writer) { $writer.Dispose() }
}
exit $exitCode

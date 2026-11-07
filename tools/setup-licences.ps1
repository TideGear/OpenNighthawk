# Show the licence terms of the tools setup downloads and ask for agreement before the first download.
# A source release contains none of these tools; each is fetched from its publisher onto this PC, used for the build, and removed with the
# build workspace afterwards. Also the wizard's text helpers (word wrap to the window, colours), shared by setup-wizard.ps1.

# Text wrapped at word boundaries to the console's width, continuation lines indented like the first. Redirected output: 100 columns.
function Write-F117Text([string]$Text, [string]$Color = 'White') {
    $width = 100
    try { if (-not [Console]::IsOutputRedirected) { $width = [Console]::WindowWidth - 1 } } catch {}
    $width = [Math]::Max(40, $width)
    $lead = ($Text -replace '^(\s*).*$', '$1')
    $line = $lead
    foreach ($word in ($Text.Trim() -split ' ')) {
        if ($line.Trim().Length -and ($line.Length + 1 + $word.Length) -gt $width) {
            Write-Host $line -ForegroundColor $Color
            $line = $lead + '  ' + $word
        } elseif ($line.Trim().Length) { $line += ' ' + $word }
        else { $line += $word }
    }
    Write-Host $line -ForegroundColor $Color
}
function Write-F117Heading([string]$Text) { Write-Host ''; Write-Host $Text -ForegroundColor Cyan }

function Confirm-F117Licences {
    [CmdletBinding()]
    param([string]$Compiler = 'Zig',[switch]$Accept)
    $ErrorActionPreference = 'Stop'
    Write-F117Heading 'Licences'
    Write-F117Text 'Setup downloads these tools from their publishers, checks each one, builds with them, and then deletes them:'
    $tools = 'Python (PSF licence), CMake (BSD-3), Ninja (Apache-2.0), SDL (zlib), Munt (LGPL-2.1), '
    if ($Compiler -eq 'Zig') { $tools += 'Zig (MIT), ' }
    Write-F117Text ('  ' + $tools + 'and the DirectX Shader Compiler (LLVM, MIT and Microsoft terms).')
    Write-F117Text '  Only if you import the Mac version''s sounds: The Unarchiver''s unar (LGPL-2.1), to unpack the Mac archive.'
    Write-F117Text 'Microsoft''s terms for the shader compiler: Windows only, it may send usage data to Microsoft, no redistribution.'
    Write-F117Text 'Full licence texts: docs\provenance.md in this folder, and each publisher''s download.' 'Gray'
    if ($Accept) { Write-Host 'Licences accepted with -AcceptLicences.'; return }
    if (-not [Environment]::UserInteractive -or [Console]::IsInputRedirected) {
        throw 'Setup needs your agreement to these licence terms. Rerun with -AcceptLicences to agree without being asked.'
    }
    Write-Host 'Type YES to agree and continue: ' -ForegroundColor Magenta -NoNewline
    $answer = Read-Host
    if ($answer -cne 'YES') { throw 'Licence terms not accepted. Nothing was downloaded.' }
}

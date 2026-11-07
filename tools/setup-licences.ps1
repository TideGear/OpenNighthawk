# Show the licence terms of the tools setup downloads and ask for agreement before the first download.
# A source release contains none of these tools; each is fetched from its publisher onto this PC, used for the build, and removed with the
# build workspace afterwards.
function Confirm-F117Licences {
    [CmdletBinding()]
    param([string]$Compiler = 'Zig',[switch]$Accept)
    $ErrorActionPreference = 'Stop'
    $lines = @(
        'Setup downloads these tools from their publishers, checks each one, builds with them, and then deletes them:',
        '  Python (PSF licence), CMake (BSD-3), Ninja (Apache-2.0), SDL (zlib), Munt (LGPL-2.1),'
    )
    if ($Compiler -eq 'Zig') { $lines += '  Zig (MIT), and the DirectX Shader Compiler (LLVM, MIT and Microsoft terms).' }
    else { $lines += '  and the DirectX Shader Compiler (LLVM, MIT and Microsoft terms).' }
    $lines += @(
        '  Only if you import the Mac version''s sounds: The Unarchiver''s unar (LGPL-2.1), to unpack the Mac archive.',
        'Microsoft''s terms for the shader compiler: Windows only, it may send usage data to Microsoft, no redistribution.',
        'Full licence texts: docs/provenance.md in this folder, and each publisher''s download.'
    )
    $lines | ForEach-Object { Write-Host $_ }
    if ($Accept) { Write-Host 'Licences accepted with -AcceptLicences.'; return }
    if (-not [Environment]::UserInteractive -or [Console]::IsInputRedirected) {
        throw 'Setup needs your agreement to these licence terms. Rerun with -AcceptLicences to agree without being asked.'
    }
    $answer = Read-Host 'Type YES to agree and continue'
    if ($answer -cne 'YES') { throw 'Licence terms not accepted. Nothing was downloaded.' }
}

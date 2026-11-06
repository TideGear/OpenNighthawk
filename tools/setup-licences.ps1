# Show the licence terms of the tools setup downloads and ask for agreement before the first download.
# A source release contains none of these tools; each is fetched from its publisher onto this PC.
function Confirm-F117Licences {
    [CmdletBinding()]
    param([string]$Compiler = 'Zig',[switch]$Accept)
    $ErrorActionPreference = 'Stop'
    $lines = @(
        '',
        'OpenNighthawk setup downloads these tools from their publishers onto this PC',
        '(each file is checked against a fixed SHA-256 before it is used; none is part of',
        'the OpenNighthawk source release):',
        '',
        '  Python 3.12.10 (embeddable)    Python Software Foundation licence',
        '  CMake 4.0.3, Ninja 1.12.1      BSD-3-Clause, Apache-2.0',
        '  SDL 3.4.8 (source)             zlib licence',
        '  Munt mt32emu (source)          LGPL-2.1-or-later',
        '  DirectX Shader Compiler 1.8.2502   LLVM/NCSA, MIT and the Microsoft Software',
        '                                 Licence Terms for the DirectX Shader Compiler'
    )
    if ($Compiler -eq 'Zig') { $lines += '  Zig 0.14.1                     MIT licence' }
    $lines += @(
        '',
        'The Microsoft terms apply to the shader compiler: it is licensed for use on Windows',
        'only, may send usage data to Microsoft, and may not be shared or redistributed.',
        'By using it you accept those terms. The full texts are in the downloaded packages',
        '(for the shader compiler: LICENSE-LLVM.txt, LICENSE-MIT.txt and LICENSE-MS.txt in',
        'the dxc folder of the setup work folder) and in docs/provenance.md.',
        ''
    )
    $lines | ForEach-Object { Write-Host $_ }
    if ($Accept) { Write-Host 'Licences accepted with -AcceptLicences.'; return }
    if (-not [Environment]::UserInteractive -or [Console]::IsInputRedirected) {
        throw 'Setup needs your agreement to these licence terms. Rerun with -AcceptLicences to agree without being asked.'
    }
    $answer = Read-Host 'Type YES to agree and continue (anything else stops setup)'
    if ($answer -cne 'YES') { throw 'Licence terms not accepted. Nothing was downloaded.' }
}

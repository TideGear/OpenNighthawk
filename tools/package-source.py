#!/usr/bin/env python3
"""Package a committed source tree, refusing known game/build artifacts.

Developer tool only: end users extract the resulting zip without Git.
"""
import argparse
import hashlib
import pathlib
import stat
import subprocess
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME_NAMES = {
    'f117.com', 'setup.exe', 'mps_logo.exe', 'player.exe', 'dswap.exe',
    'start.exe', 'vgame.exe', 'end.exe', 'mgraphic.exe', 'misc.exe',
    'asound.117', 'isound.117', 'rsound.117', 'nsound.117',
    'asound.log', 'isound.log', 'rsound.log', 'roster.fil',
}


def check_archive(path):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        for entry in archive.infolist():
            if stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError(f'Symlink forbidden in source archive: {entry.filename}')
        for name in names:
            p = pathlib.PurePosixPath(name)
            lower = p.name.lower()
            if p.is_absolute() or '..' in p.parts:
                raise ValueError(f'Unsafe archive path: {name}')
            if (lower in GAME_NAMES or lower.startswith('gen_') and lower.endswith('.c')
                    or lower in {'recomp_gen.c', 'recomp_gen.h'} and 'src/recomp' not in str(p)
                    or p.suffix.lower() in {'.exe', '.dll', '.obj', '.lib', '.rom', '.ppm', '.dxil'}
                    or any(part.lower() in {'private', 'generated', 'build', 'sessions', 'save-start'} for part in p.parts)):
                raise ValueError(f'Forbidden game/build artifact: {name}')
        required = {'README.md', 'LICENSE', 'CMakeLists.txt', 'setup.cmd', 'tools/verify_install.py', 'recompiler/recomp.py',
                    'tools/setup.ps1', 'tools/setup-wizard.ps1', 'tools/setup-mac.ps1', 'tools/mac_sounds.py', 'tools/uninstall.ps1', 'tools/uninstall.cmd'}
        missing = required - set(names)
        if missing:
            raise ValueError(f'Missing source files: {sorted(missing)}')
        return len(names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ref', default='HEAD')
    parser.add_argument('--out', required=True, type=pathlib.Path)
    args = parser.parse_args()
    out = args.out.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error('Release output must be outside the repository')
    for parent in out.parents:
        if any((parent / name).is_file() for name in ('VGAME.EXE', 'F117.COM', 'F-117A/VGAME.EXE')):
            parser.error('Release output must be outside game installations')
    if out.exists() or out.with_suffix(out.suffix + '.sha256').exists():
        parser.error('Choose an unused release output path')
    commit = subprocess.check_output(['git', 'rev-parse', '--verify', args.ref + '^{commit}'], cwd=ROOT, text=True).strip()
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out.parent) as scratch:
        candidate = pathlib.Path(scratch) / 'source.zip'
        subprocess.run(['git', 'archive', '--format=zip', '--output=' + str(candidate), commit], cwd=ROOT, check=True)
        count = check_archive(candidate)
        digest = hashlib.sha256(candidate.read_bytes()).hexdigest()
        candidate.rename(out)
    out.with_suffix(out.suffix + '.sha256').write_text(f'{digest}  {out.name}\ncommit {commit}\n', encoding='utf-8')
    print(f'{count} source archive entries; commit {commit}; SHA-256 {digest}')
    print('Artifact checks do not replace provenance review. This command does not publish a release.')


if __name__ == '__main__':
    main()

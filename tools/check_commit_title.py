#!/usr/bin/env python3
"""Reject commit titles without the current progress prefix; --install enables the local hook."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MARKER = '# F117 scored commit title hook'
HOOK = '''#!/bin/sh
# F117 scored commit title hook
checker=tools/check_commit_title.py
if ! test -f "$checker"; then
    checker=@CHECKER@
fi
if command -v py >/dev/null 2>&1; then
    exec py "$checker" "$1"
fi
exec python3 "$checker" "$1"
'''


def validate(message, expected):
    title = message.splitlines()[0] if message.splitlines() else ''
    if not title.startswith(expected + ' ') or not title[len(expected):].strip():
        raise ValueError(f'Commit title must start with the current score, followed by a description:\n{expected} DESCRIPTION')


def current_prefix(root):
    result = subprocess.run([sys.executable, str(root/'tools/progress.py'), '--title'],
                            cwd=root, capture_output=True, text=True, check=True)
    prefix = result.stdout.strip()
    if not re.fullmatch(r'\(P1 \d+\.\d{2}%, P2 \d+\.\d{2}%, P3 \d+\.\d{2}%, All \d+\.\d{2}%\)', prefix):
        raise ValueError('Could not obtain a valid score from tools/progress.py --title')
    return prefix


def install():
    configured = subprocess.run(['git', 'config', '--get', 'core.hooksPath'], cwd=ROOT,
                                capture_output=True, text=True)
    if configured.returncode not in (0, 1) or configured.stdout.strip():
        raise ValueError('An existing core.hooksPath requires manual integration; it was not changed')
    result = subprocess.run(['git', 'rev-parse', '--git-path', 'hooks/commit-msg'], cwd=ROOT,
                            capture_output=True, text=True, check=True)
    path = Path(result.stdout.strip())
    if not path.is_absolute():
        path = ROOT/path
    if path.exists() and MARKER not in path.read_text():
        raise ValueError(f'Existing hook was not replaced: {path}')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(HOOK.replace('@CHECKER@', shlex.quote((ROOT/'tools/check_commit_title.py').as_posix())),
                    encoding='utf-8', newline='\n')
    path.chmod(path.stat().st_mode | 0o111)
    print(f'Enabled scored commit titles: {path}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('message', type=Path, nargs='?')
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    if args.install == bool(args.message):
        parser.error('choose a message file or --install')
    try:
        if args.install:
            install()
        else:
            # Hooks are shared by worktrees. Always score the tree committing,
            # even if the installed checker is loaded from another worktree.
            root = Path(subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True,
                                       text=True, check=True).stdout.strip())
            validate(args.message.read_text(encoding='utf-8-sig'), current_prefix(root))
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

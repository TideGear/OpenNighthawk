#!/usr/bin/env python3
"""A player's saved roster must not prevent setup from verifying their game."""
import contextlib
import hashlib
import importlib.util
import io
import pathlib
import sys
import tempfile
from unittest import mock

VERIFY_INSTALL_PATH = pathlib.Path(__file__).resolve().parents[1] / 'tools' / 'verify_install.py'
spec = importlib.util.spec_from_file_location('verify_install', VERIFY_INSTALL_PATH)
verify_install = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify_install)


def verify(data, expected, output):
    with mock.patch.object(verify_install, 'EXPECTED', expected), \
            mock.patch.object(verify_install, 'GOG_DOSBOX', []), \
            mock.patch.object(verify_install, 'PLAYER_DATA', ('ROSTER.FIL',)), \
            mock.patch.object(sys, 'argv', ['verify_install.py', '--data', str(data)]), \
            contextlib.redirect_stdout(output):
        try:
            verify_install.main()
        except SystemExit as error:
            return error.code
    raise AssertionError('Verifier did not exit')


def test_missing_or_changed_roster_is_accepted():
    game_hash = hashlib.sha256(b'x').hexdigest()
    roster_hash = hashlib.sha256(b'base').hexdigest()
    expected = [('VGAME.EXE', 1, game_hash), ('ROSTER.FIL', 4, roster_hash)]

    with tempfile.TemporaryDirectory() as temp:
        data = pathlib.Path(temp)
        (data / 'VGAME.EXE').write_bytes(b'x')
        missing_output = io.StringIO()
        assert verify(data, expected, missing_output) == 0
        assert 'MISSING    ROSTER.FIL (not needed: player data)' in missing_output.getvalue()

        (data / 'ROSTER.FIL').write_bytes(b'save')
        changed_output = io.StringIO()
        assert verify(data, expected, changed_output) == 0
        assert 'ROSTER.FIL (player data or a setup log' in changed_output.getvalue()

        (data / 'VGAME.EXE').write_bytes(b'y')
        assert verify(data, expected, io.StringIO()) == 1


if __name__ == '__main__':
    test_missing_or_changed_roster_is_accepted()
    print('Saved-roster verification checks passed')

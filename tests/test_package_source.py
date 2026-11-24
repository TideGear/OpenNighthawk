#!/usr/bin/env python3
"""Regression checks for the RecompLauncher manifest in source release archives."""
import importlib.util
import json
import pathlib
import sys
import tempfile
import zipfile

PACKAGE_SOURCE_PATH = pathlib.Path(__file__).resolve().parents[1] / 'tools' / 'package-source.py'
sys.path.insert(0, str(PACKAGE_SOURCE_PATH.parent))
spec = importlib.util.spec_from_file_location('package_source', PACKAGE_SOURCE_PATH)
package_source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_source)


def archive_with_manifest(path, name, contents):
    with zipfile.ZipFile(path, 'w') as archive:
        for required in package_source.REQUIRED_SOURCE_FILES:
            archive.writestr(required, b'')
        archive.writestr(name, contents)


def test_root_manifest_is_required_and_valid():
    manifest = json.dumps({'format': 1}).encode('utf-8')
    with tempfile.TemporaryDirectory() as temp:
        archive = pathlib.Path(temp) / 'release.zip'
        archive_with_manifest(archive, 'recomplauncher.json', manifest)
        assert package_source.check_archive(archive) > len(package_source.REQUIRED_SOURCE_FILES)

        archive_with_manifest(archive, 'OpenNighthawk/recomplauncher.json', manifest)
        try:
            package_source.check_archive(archive)
        except ValueError as error:
            assert 'at the release archive root' in str(error)
        else:
            raise AssertionError('Nested launcher manifest was accepted')


def test_invalid_manifest_is_rejected():
    with tempfile.TemporaryDirectory() as temp:
        archive = pathlib.Path(temp) / 'release.zip'
        archive_with_manifest(archive, 'recomplauncher.json', b'{"format": 2}')
        try:
            package_source.check_archive(archive)
        except ValueError as error:
            assert 'format 1' in str(error)
        else:
            raise AssertionError('Unsupported launcher manifest format was accepted')


if __name__ == '__main__':
    test_root_manifest_is_required_and_valid()
    test_invalid_manifest_is_rejected()
    print('RecompLauncher release packaging checks passed')

#!/usr/bin/env python3
"""Select native distro dependencies and reject incompatible editor bundles.

A Debian build and an Ubuntu build are distinct products: identical library
names do not guarantee identical SONAMEs, symbol versions, or Qt versions.
"""
import argparse
import json
from pathlib import Path
import platform
import shlex
import sys

COMMON = ['libqt6core6t64', 'libqt6gui6', 'libpng16-16t64', 'libtiff6',
          'libwebp7', 'libwebpmux3', 'libheif1', 'liblcms2-2', 'libstdc++6', 'zlib1g',
          'python3', 'python3-pil', 'python3-venv', 'ca-certificates', 'util-linux']
JPEG = {('ubuntu', '24.04'): 'libjpeg-turbo8', ('debian', '13'): 'libjpeg62-turbo'}


def read_os_release(path=Path('/etc/os-release')):
    values = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.lstrip().startswith('#') or '=' not in line:
            continue
        key, raw = line.split('=', 1)
        parts = shlex.split(raw, comments=True)
        if len(parts) == 1:
            values[key] = parts[0]
    return values


def runtime_packages(os_id, version_id, arch):
    if arch != 'x86_64' or (os_id, version_id) not in JPEG:
        raise ValueError(f'Unsupported platform {os_id} {version_id} {arch}. '
                         'Use Ubuntu 24.04 x86_64 or Debian 13 x86_64.')
    return COMMON + [JPEG[(os_id, version_id)]]


def validate_bundle(manifest, os_id, version_id, arch):
    runtime_packages(os_id, version_id, arch)
    expected = (os_id, version_id, arch)
    actual = tuple(manifest.get(key) for key in ('os_id', 'version_id', 'arch'))
    if actual != expected:
        raise ValueError(f'Bundle target {actual} does not match this host {expected}. '
                         'Download the matching distro-native archive. '
                         'Do not alias libjpeg.so.8 to libjpeg.so.62.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['identify', 'packages', 'check-bundle'])
    parser.add_argument('manifest', nargs='?', type=Path)
    args = parser.parse_args()
    try:
        release = read_os_release()
        os_id, version_id, arch = release.get('ID', ''), release.get('VERSION_ID', ''), platform.machine()
        packages = runtime_packages(os_id, version_id, arch)
        if args.command == 'packages':
            print('\n'.join(packages))
        elif args.command == 'identify':
            print(f'{os_id}{version_id}-x86_64')
        else:
            if args.manifest is None:
                raise ValueError('check-bundle requires runtime-target.json')
            manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
            if not isinstance(manifest, dict):
                raise ValueError('runtime-target.json must contain an object')
            validate_bundle(manifest, os_id, version_id, arch)
    except (OSError, ValueError) as error:
        print(f'Runtime preflight: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())

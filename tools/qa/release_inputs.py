#!/usr/bin/env python3
"""Offline configure-time input validation and explicit test inventory."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

from release_report import source_identity, _write_json, file_sha256, inputs_fingerprint, inventory_fingerprint

COFFEE_SHA256 = 'cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7'
OPTIONAL_TESTS = {
    'F05.NativeTransport': ('WindowsUI', 'Windows native named-pipe transport is unavailable'),
    'F06': ('WindowsUI', 'Windows desktop Qt/D3D11 runtime is unavailable'),
    'U01.Layers': ('WindowsUI', 'Windows desktop with Qt Test is unavailable'),
    'R00.BuildFreshness': ('WindowsUI', 'desktop executable and build token are unavailable'),
    'C02.LCMS': ('OptionalDependency', 'Little CMS headers/library are unavailable'),
    'M03.Coffee': ('RealWindowsIntegration', 'ONNX Runtime or real coffee package/image/official tensor reference is unavailable'),
    'M03.CoffeeAutomatic': ('RealWindowsIntegration', 'Windows/ONNX Runtime/official automatic coffee reference is unavailable'),
    'U04.RealImageReview': ('RealWindowsIntegration', 'Windows/Qt Test/ONNX Runtime/real coffee image/package is unavailable'),
    'E07.RealReconstruction': ('RealWindowsIntegration', 'Windows desktop/real coffee image is unavailable'),
    'E08.FullScene': ('RealWindowsIntegration', 'Windows desktop/real coffee image/full scene reference is unavailable'),
    'M02.Runtime': ('RuntimeFixture', 'ONNX Runtime or developer identity fixture is unavailable'),
    'M02.SamRuntime': ('RuntimeFixture', 'ONNX Runtime or developer encoder/decoder fixtures are unavailable'),
    'M05.Runtime': ('RuntimeFixture', 'ONNX Runtime or developer adapter probe fixtures are unavailable'),
}


def enabled(value):
    return str(value).upper() in ('1', 'ON', 'TRUE', 'YES')


def input_files(dependencies):
    paths = set()
    for variable in ('DRAWBORAD_TEST_COFFEE_IMAGE', 'DRAWBORAD_TEST_ONNX_MODEL',
                     'DRAWBORAD_TEST_SAM_ENCODER', 'DRAWBORAD_TEST_SAM_DECODER',
                     'DRAWBORAD_TEST_PROBE_ENCODER', 'DRAWBORAD_TEST_PROBE_DECODER',
                     'DRAWBORAD_ORT_LIBRARY', 'DRAWBORAD_ORT_RUNTIME'):
        value = dependencies.get(variable, '')
        if value and Path(value).is_file():
            paths.add(Path(value).resolve())
    package = dependencies.get('DRAWBORAD_TEST_COFFEE_PACKAGE', '')
    if package:
        paths.update((Path(package) / name).resolve() for name in ('manifest.json', 'encoder.onnx', 'decoder.onnx'))
    sdk = dependencies.get('DRAWBORAD_ONNXRUNTIME_ROOT', '')
    if sdk:
        paths.update((Path(sdk) / 'include' / name).resolve() for name in ('onnxruntime_cxx_api.h', 'onnxruntime_c_api.h', 'onnxruntime_float16.h'))
    for variable in ('DRAWBORAD_TEST_COFFEE_REFERENCE', 'DRAWBORAD_TEST_COFFEE_AUTO_REFERENCE',
                     'DRAWBORAD_TEST_FULL_SCENE_REFERENCE'):
        value = dependencies.get(variable, '')
        if value:
            directories = [Path(value)]
            if variable == 'DRAWBORAD_TEST_FULL_SCENE_REFERENCE':
                directories.append(Path(value).parent / 'reviewed-scene')
            for directory in directories:
                paths.update(path.resolve() for path in directory.iterdir() if path.is_file() and path.suffix.lower() in ('.json', '.u8', '.f32', '.png'))
    return [{'path': str(path), 'sha256': file_sha256(path), 'size': path.stat().st_size}
            for path in sorted(paths) if path.is_file()]


def validate_inputs(facts):
    dependencies = facts['dependencies']
    profile = facts['profile']
    if profile not in ('CorePortable', 'RealWindowsIntegration'):
        return ['DRAWBORAD_TEST_PROFILE must be CorePortable or RealWindowsIntegration']
    strict = profile == 'RealWindowsIntegration'
    blockers = []
    if strict:
        if not enabled(dependencies.get('WIN32')):
            blockers.append('RealWindowsIntegration requires native Windows 11 x64 (current host is not Windows)')
        elif tuple(int(part) for part in (facts['host_version'].split('.') + ['0'] * 3)[:3]) < (10, 0, 22000):
            blockers.append('RealWindowsIntegration requires Windows 11 build 22000 or later')
        if not enabled(dependencies.get('DRAWBORAD_DESKTOP')):
            blockers.append('DRAWBORAD_DESKTOP=ON and Qt 6.12 / Qt Test are required')
        if dependencies.get('CMAKE_SIZEOF_VOID_P') != '8':
            blockers.append('x64 build architecture is required')
        if not enabled(dependencies.get('MSVC')):
            blockers.append('MSVC 2022 is required for RealWindowsIntegration')

    def require_file(variable, suffix=None, size=None):
        value = dependencies.get(variable, '')
        path = Path(value) / suffix if value and suffix else Path(value) if value else None
        if path is None or not path.is_file():
            blockers.append(f'{variable}: required file missing' + (f' ({suffix})' if suffix else ''))
            return None
        if size is not None and path.stat().st_size != size:
            blockers.append(f'{variable}: wrong byte size for {path.name}; expected {size}')
            return None
        return path

    ort = dependencies.get('DRAWBORAD_ONNXRUNTIME_ROOT', '')
    if strict or ort:
        for header in ('onnxruntime_cxx_api.h', 'onnxruntime_c_api.h', 'onnxruntime_float16.h'):
            require_file('DRAWBORAD_ONNXRUNTIME_ROOT', 'include/' + header)
        if enabled(dependencies.get('WIN32')):
            require_file('DRAWBORAD_ONNXRUNTIME_ROOT', 'lib/onnxruntime.lib')
            dll = require_file('DRAWBORAD_ONNXRUNTIME_ROOT', 'lib/onnxruntime.dll')
            if dll:
                try:
                    with dll.open('rb') as stream:
                        header = stream.read(64)
                        if header[:2] != b'MZ' or len(header) != 64:
                            raise ValueError('missing PE DOS header')
                        stream.seek(struct.unpack_from('<I', header, 0x3c)[0])
                        pe = stream.read(6)
                        if pe[:4] != b'PE\0\0' or struct.unpack_from('<H', pe, 4)[0] != 0x8664:
                            raise ValueError('not an x64 PE DLL')
                except (OSError, ValueError, struct.error) as error:
                    blockers.append(f'DRAWBORAD_ONNXRUNTIME_ROOT: {error}')
    image = dependencies.get('DRAWBORAD_TEST_COFFEE_IMAGE', '')
    if strict or image:
        path = require_file('DRAWBORAD_TEST_COFFEE_IMAGE')
        if path and hashlib.sha256(path.read_bytes()).hexdigest() != COFFEE_SHA256:
            blockers.append('DRAWBORAD_TEST_COFFEE_IMAGE: SHA-256 mismatch; cannot substitute another image')
    package = dependencies.get('DRAWBORAD_TEST_COFFEE_PACKAGE', '')
    if strict or package:
        for name in ('manifest.json', 'encoder.onnx', 'decoder.onnx'):
            require_file('DRAWBORAD_TEST_COFFEE_PACKAGE', name)
    reference = dependencies.get('DRAWBORAD_TEST_COFFEE_REFERENCE', '')
    if strict or reference:
        require_file('DRAWBORAD_TEST_COFFEE_REFERENCE', 'image.f32', 3 * 1024 * 1024 * 4)
        for i in range(3):
            for suffix, size in (('low.f32', 3 * 256 * 256 * 4), ('scores.f32', 12), ('masks.u8', 3 * 600 * 400)):
                require_file('DRAWBORAD_TEST_COFFEE_REFERENCE', f'case{i}-{suffix}', size)
    auto = dependencies.get('DRAWBORAD_TEST_COFFEE_AUTO_REFERENCE', '')
    if strict or auto:
        for i in range(8):
            require_file('DRAWBORAD_TEST_COFFEE_AUTO_REFERENCE', f'mask0{i}.png')
    scene = dependencies.get('DRAWBORAD_TEST_FULL_SCENE_REFERENCE', '')
    if strict or scene:
        path = require_file('DRAWBORAD_TEST_FULL_SCENE_REFERENCE', 'report.json')
        if path:
            try:
                data = json.loads(path.read_text(encoding='utf-8-sig'))
                cases = [case for case in data['cases'] if case['name'].endswith('reviewed-points')]
                if not cases or 'cup-reviewed-points' not in {case['name'] for case in cases}:
                    raise ValueError('no cup-reviewed-points oracle case; empty loops are not evidence')
                if data.get('source_sha256') != COFFEE_SHA256:
                    raise ValueError('full-scene reference source hash mismatch')
                for case in cases:
                    for i in range(3):
                        require_file('DRAWBORAD_TEST_FULL_SCENE_REFERENCE', f'{case["name"]}-{i}.png')
                reviewed = path.parent.parent / 'reviewed-scene'
                manifest = json.loads((reviewed / 'review.json').read_text(encoding='utf-8-sig'))
                if manifest.get('source_sha256') != COFFEE_SHA256 or not manifest.get('regions'):
                    raise ValueError('reviewed-scene reference manifest/source missing')
                for region in manifest['regions']:
                    mask = (reviewed / region['mask']).read_bytes()
                    if len(mask) != 240000 or hashlib.sha256(mask).hexdigest() != region['sha256']:
                        raise ValueError('reviewed-scene mask size/hash mismatch')
            except (OSError, KeyError, ValueError, TypeError) as error:
                blockers.append(f'DRAWBORAD_TEST_FULL_SCENE_REFERENCE: {error}')
    if facts.get('stage') == 'inventory' and strict:
        for dependency in ('Qt6_FOUND', 'Qt6Test_FOUND', 'DRAWBORAD_HAS_ONNXRUNTIME', 'PNG_FOUND', 'JPEG_FOUND', 'ZLIB_FOUND'):
            if not enabled(dependencies.get(dependency)):
                blockers.append(f'{dependency}: required integration dependency was not found')
    return blockers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--facts', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    facts = json.loads(args.facts.read_text(encoding='utf-8-sig'))
    blockers = validate_inputs(facts)
    report = {'schema_version': 1, 'status': 'Blocked' if blockers else 'Passed',
              'scope': 'configure inputs only', 'profile': facts['profile'],
              'blockers': blockers, 'dependencies': facts['dependencies']}
    if facts['stage'] == 'inventory':
        registered = facts['registered']
        names = {test['name'] for test in registered}
        report.update(source_identity(facts['source_dir']))
        files = input_files(facts['dependencies']) if not blockers else []
        report.update({'input_files': files, 'inputs_fingerprint': inputs_fingerprint(files)})
        report.update({'source_dir': facts['source_dir'], 'configuration': facts['configuration'],
                       'host': {'system': facts['host_system'], 'version': facts['host_version']},
                       'cmake_cache': facts.get('cmake_cache', facts['dependencies']), 'registered': registered,
                       'not_registered': [{'name': name, 'labels': [label], 'status': 'Blocked', 'reason': reason}
                                          for name, (label, reason) in OPTIONAL_TESTS.items() if name not in names]})
        report['inventory_fingerprint'] = inventory_fingerprint(report)
    _write_json(args.output, report)
    for reason in blockers:
        print('Blocked: ' + reason, file=sys.stderr)
    return 2 if blockers else 0


if __name__ == '__main__':
    sys.exit(main())

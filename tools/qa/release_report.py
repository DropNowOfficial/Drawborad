#!/usr/bin/env python3
"""Bind one complete CTest run to its exact source and build configuration.

Automated suite success never asserts attended Windows, Photoshop or fidelity
acceptance. No network access, model acquisition or test retries occur here.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

REAL_TESTS = {'F05.NativeTransport', 'F06', 'U01.Layers', 'R00.BuildFreshness',
              'M03.Coffee', 'M03.CoffeeAutomatic', 'U04.RealImageReview',
              'E07.RealReconstruction', 'E08.FullScene'}
IDENTITY_KEYS = ('git_sha', 'source_tree', 'source_fingerprint', 'source_dirty')


def inventory_fingerprint(inventory):
    content = {key: value for key, value in inventory.items() if key != 'inventory_fingerprint'}
    return hashlib.sha256(json.dumps(content, sort_keys=True, separators=(',', ':'),
                                     ensure_ascii=False).encode('utf-8')).hexdigest()


def file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def inputs_fingerprint(files):
    digest = hashlib.sha256()
    for row in sorted(files, key=lambda value: value['path']):
        path = Path(row['path'])
        digest.update(str(path).encode('utf-8') + b'\0')
        digest.update((file_sha256(path) if path.is_file() else 'missing').encode('ascii') + b'\0')
    return digest.hexdigest()


def source_identity(source_dir):
    root = Path(source_dir).resolve()
    def git(*args):
        return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE)
    if Path(git('rev-parse', '--show-toplevel').decode().strip()).resolve() != root:
        raise ValueError('source directory is not the repository root')
    sha = git('rev-parse', 'HEAD').decode().strip()
    tree = git('rev-parse', 'HEAD^{tree}').decode().strip()
    status = git('status', '--porcelain', '--untracked-files=all').decode()
    paths = set(git('ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0'))
    fingerprint = hashlib.sha256()
    for encoded in sorted(paths - {b''}):
        relative = os.fsdecode(encoded)
        path = root / relative
        fingerprint.update(encoded + b'\0')
        if path.is_symlink():
            fingerprint.update(b'symlink\0' + os.fsencode(os.readlink(path)))
        elif path.is_file():
            fingerprint.update(b'file\0' + hashlib.sha256(path.read_bytes()).digest())
        else:
            fingerprint.update(b'missing\0')
    return {'git_sha': sha, 'source_tree': tree, 'source_fingerprint': fingerprint.hexdigest(),
            'source_dirty': bool(status.strip())}


def _read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def _write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    temporary.replace(path)


def summarize(inventory, junit_path, receipt, current_identity, configuration):
    blockers = []
    configured_fingerprint = inventory_fingerprint(inventory)
    if (inventory.get('inventory_fingerprint') != configured_fingerprint or
            receipt.get('inventory_fingerprint') != configured_fingerprint):
        blockers.append('configuration/inventory differs from the execution receipt')
    for key in IDENTITY_KEYS:
        if key not in receipt or receipt.get(key) != inventory.get(key) or current_identity.get(key) != inventory.get(key):
            blockers.append(f'source identity mismatch or missing receipt: {key}')
    if inventory.get('configuration') not in ('', None, configuration):
        blockers.append('configured build type differs from requested configuration')
    if receipt.get('configuration') != configuration or receipt.get('profile') != inventory.get('profile'):
        blockers.append('execution profile/configuration differs from inventory')
    if 'inputs_fingerprint' in inventory:
        if receipt.get('inputs_fingerprint') != inventory['inputs_fingerprint'] or current_identity.get('inputs_fingerprint') != inventory['inputs_fingerprint']:
            blockers.append('model, SDK or reference input changed since configure/execution')
    expected = [row['name'] for row in inventory.get('registered', [])]
    if not expected or len(expected) != len(set(expected)):
        blockers.append('registered test inventory is empty or duplicated')
    if sorted(receipt.get('registered', [])) != sorted(expected):
        blockers.append('CTest execution registry does not match configure inventory')
    if inventory.get('profile') == 'RealWindowsIntegration':
        for name in sorted(REAL_TESTS - set(expected)):
            blockers.append(f'required real Windows integration test absent: {name}')
    tests = []
    try:
        data = Path(junit_path).read_bytes()
        if receipt.get('junit_sha256') != hashlib.sha256(data).hexdigest():
            blockers.append('JUnit digest does not match execution receipt')
        root = ET.fromstring(data)
        if root.tag not in ('testsuite', 'testsuites'):
            raise ValueError('not a JUnit test suite')
        for case in root.iter('testcase'):
            name = case.get('name', '')
            try:
                duration = float(case.get('time', '0'))
                if not 0 <= duration < float('inf'):
                    raise ValueError('invalid test duration')
            except ValueError:
                blockers.append(f'invalid test duration: {name}')
                duration = None
            failure = case.find('failure')
            failure = failure if failure is not None else case.find('error')
            skipped = case.find('skipped')
            status = 'Failed' if failure is not None else 'Skipped' if skipped is not None else 'Passed'
            tests.append({'name': name, 'status': status, 'elapsed_seconds': duration,
                          'message': (failure.get('message') or failure.text or '') if failure is not None
                          else (skipped.get('message') or skipped.text or '') if skipped is not None else ''})
    except (OSError, ET.ParseError, ValueError) as error:
        blockers.append(f'JUnit unavailable or malformed: {error}')
    names = [row['name'] for row in tests]
    duplicates = sorted(name for name, count in Counter(names).items() if count > 1)
    if duplicates:
        blockers.append('duplicate JUnit test records: ' + ', '.join(duplicates))
    for name in sorted(set(names) - set(expected)):
        blockers.append(f'unregistered test result from another configuration: {name}')
    for name in sorted(set(expected) - set(names)):
        blockers.append(f'registered test did not run: {name}')
        tests.append({'name': name, 'status': 'Blocked', 'elapsed_seconds': None,
                      'message': 'no JUnit record from this invocation'})
    skipped_count = sum(row['status'] == 'Skipped' for row in tests)
    if skipped_count:
        blockers.append(f'{skipped_count} registered tests were skipped')
    failed_count = sum(row['status'] == 'Failed' for row in tests)
    if type(receipt.get('ctest_exit_code')) is not int:
        blockers.append('CTest execution exit status is missing or invalid')
    elif receipt['ctest_exit_code'] != 0 and not failed_count:
        blockers.append(f'CTest exited {receipt["ctest_exit_code"]} without a complete successful report')
    status = 'Failed' if failed_count else 'Blocked' if blockers else 'Passed'
    omitted = inventory.get('not_registered', [])
    return {
        'schema_version': 1, 'status': status, 'scope': 'automated tests in this configuration only',
        **{key: inventory.get(key) for key in IDENTITY_KEYS},
        'profile': inventory.get('profile'), 'configuration': configuration,
        'host': inventory.get('host', {}), 'dependencies': inventory.get('dependencies', {}),
        'input_files': inventory.get('input_files', []), 'inputs_fingerprint': inventory.get('inputs_fingerprint'),
        'inventory_fingerprint': inventory.get('inventory_fingerprint'),
        'cmake_cache': inventory.get('cmake_cache', {}),
        'counts': {'passed': sum(row['status'] == 'Passed' for row in tests), 'failed': failed_count,
                   'skipped': skipped_count, 'blocked': sum(row['status'] == 'Blocked' for row in tests),
                   'not_registered': len(omitted)},
        'elapsed_seconds': receipt.get('elapsed_seconds'), 'started_at': receipt.get('started_at'),
        'finished_at': receipt.get('finished_at'), 'command': receipt.get('command'),
        'tests': tests, 'not_registered': omitted, 'blockers': blockers,
        'runtime_alpha': {'status': 'Blocked', 'reason': 'attended Windows application, complete real AI workflow and extracted-package acceptance require separate evidence'},
        'fidelity': {'status': 'Failed', 'historical_ssim': 0.6487, 'target_ssim': 0.98,
                     'current_measurement': 'Not Tested', 'reason': 'historical measurement only; no new fidelity acceptance is inferred'},
        'photoshop_oracle': {'status': 'Blocked', 'reason': 'Not Run'},
        'natural_process': {'status': 'Not Tested', 'reason': 'human process review required'},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--configuration', default='Release')
    parser.add_argument('--junit', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--run-ctest', action='store_true')
    parser.add_argument('--ctest-executable', default='ctest')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    junit = (args.junit or (build / f'rc1-ctest-{args.configuration}.xml')).resolve()
    receipt_path = build / 'TEST_EXECUTION.json'
    inventory = _read_json(build / 'TEST_INVENTORY.json')
    if inventory.get('inventory_fingerprint') != inventory_fingerprint(inventory):
        raise ValueError('configuration inventory is missing or altered; configure this exact source again')
    identity = source_identity(inventory['source_dir'])
    identity['inputs_fingerprint'] = inputs_fingerprint(inventory.get('input_files', []))
    receipt = {}
    if args.run_ctest:
        if any(identity[key] != inventory.get(key) for key in IDENTITY_KEYS):
            raise ValueError('source changed since configure; configure this exact commit again before testing')
        if identity['inputs_fingerprint'] != inventory.get('inputs_fingerprint'):
            raise ValueError('model, SDK or reference inputs changed since configure')
        if inventory.get('configuration') not in ('', None, args.configuration):
            raise ValueError('build configuration does not match inventory')
        registry_command = [args.ctest_executable, '--test-dir', str(build), '-C', args.configuration, '--show-only=json-v1']
        registry = json.loads(subprocess.check_output(registry_command, text=True))
        actual = sorted(test['name'] for test in registry.get('tests', []))
        expected = sorted(test['name'] for test in inventory['registered'])
        if actual != expected:
            raise ValueError('actual CTest registry differs from configuration inventory')
        junit.parent.mkdir(parents=True, exist_ok=True)
        # Remove only this invocation's result/receipt: an old green report must not survive a crash.
        junit.unlink(missing_ok=True)
        receipt_path.unlink(missing_ok=True)
        command = [args.ctest_executable, '--test-dir', str(build), '-C', args.configuration,
                   '--output-on-failure', '--no-tests=error', '--output-junit', str(junit)]
        started_at = datetime.now(timezone.utc).isoformat()
        start = time.monotonic()
        with (build / f'rc1-ctest-{args.configuration}.log').open('w', encoding='utf-8') as log:
            process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        receipt = {**identity, 'profile': inventory['profile'], 'configuration': args.configuration,
                   'inventory_fingerprint': inventory['inventory_fingerprint'],
                   'registered': actual, 'ctest_exit_code': process.returncode, 'command': command,
                   'started_at': started_at, 'finished_at': datetime.now(timezone.utc).isoformat(),
                   'elapsed_seconds': time.monotonic() - start,
                   'junit_sha256': hashlib.sha256(junit.read_bytes()).hexdigest() if junit.exists() else None}
        _write_json(receipt_path, receipt)
        identity = source_identity(inventory['source_dir'])
        identity['inputs_fingerprint'] = inputs_fingerprint(inventory.get('input_files', []))
    elif receipt_path.exists():
        receipt = _read_json(receipt_path)
    report = summarize(inventory, junit, receipt, identity, args.configuration)
    _write_json(args.output, report)
    print(json.dumps({key: report[key] for key in ('status', 'git_sha', 'profile', 'configuration', 'counts', 'blockers')}, indent=2))
    return 0 if report['status'] == 'Passed' else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        print(f'Blocked: {error}', file=sys.stderr)
        sys.exit(2)

#!/usr/bin/env python3
"""Explicit opt-in, hash-bound locality research on an existing native project."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import stat
import subprocess
import sys
import time

import locality

MAX_PROJECT_BYTES = 32 * 1024 * 1024
MAX_DECLARATION_BYTES = 256


def reject_symlinks(path):
    path = Path(os.path.abspath(path))
    for part in [*reversed(path.parents), path]:
        try:
            mode = part.lstat().st_mode
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(mode):
            raise ValueError('symlink paths are outside this research contract')
    return path


def read_bounded(path, limit):
    path = reject_symlinks(path)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
        raise ValueError('regular input file within byte budget required')
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError('input grew beyond byte budget')
    return data


def new_path(path):
    path = reject_symlinks(path)
    if os.path.lexists(path):
        raise ValueError('output already exists; no clobber')
    if not path.parent.is_dir():
        raise ValueError('output parent directory must already exist')
    return path


def native_path(explicit=None):
    value = explicit or os.environ.get('DRAWBORAD_NATIVE_LOCALITY')
    path = Path(value) if value else Path(__file__).resolve().parent/'build/native-locality'
    path = reject_symlinks(path)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ValueError('native locality runner missing or not executable; build it first')
    return path


def declaration(data):
    if not data or len(data) > MAX_DECLARATION_BYTES:
        raise ValueError('prefix declaration byte budget')
    try:
        fields = data.decode('ascii').split()
    except UnicodeDecodeError as error:
        raise ValueError('ASCII prefix declaration required') from error
    if len(fields) != 5 or fields[0] != 'DBLOCALITY1':
        raise ValueError('prefix declaration schema/token count')
    for value in fields[1:3]:
        if len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
            raise ValueError('canonical lowercase SHA256 required')
    if any(not value.isascii() or not value.isdecimal() or len(value) > 5 for value in fields[3:]):
        raise ValueError('prefix declaration integer')
    events, prefix = map(int, fields[3:])
    if not 1 <= prefix <= events <= locality.MAX_EVENTS:
        raise ValueError('declared prefix must be 1..event count within budget')
    canonical = f'DBLOCALITY1 {fields[1]} {fields[2]} {events} {prefix}\n'.encode()
    if data != canonical:
        raise ValueError('canonical single-line declaration required')
    return dict(inputSHA256=fields[1], stateSHA256=fields[2], eventCount=events,
                immutablePrefixCount=prefix)


def input_contract(path):
    path = reject_symlinks(path)
    for suffix in ('.bak', '.journal'):
        if os.path.lexists(str(path) + suffix):
            raise ValueError('recovery sidecar present; use a clean saved project copy')
    data = read_bounded(path, MAX_PROJECT_BYTES)
    return path, hashlib.sha256(data).hexdigest()


def run_native(binary, arguments, timeout=300):
    run = subprocess.run([str(binary), '--research', *map(str, arguments)],
                         capture_output=True, text=True, timeout=timeout)
    if run.returncode:
        raise RuntimeError('native locality failed: ' + run.stderr.strip())
    return dict(argv=[str(binary), '--research', *map(str, arguments)],
                returncode=run.returncode, stdout=run.stdout.strip())


def save_status(output, record):
    temporary = output/'result.next.json'
    with temporary.open('x', encoding='utf-8') as stream:
        json.dump(record, stream, indent=2)
        stream.write('\n')
    os.replace(temporary, output/'result.json')


def reorder(input_path, declaration_path, output_path, binary):
    source, source_sha = input_contract(input_path)
    declaration_path = reject_symlinks(declaration_path)
    raw_declaration = read_bounded(declaration_path, MAX_DECLARATION_BYTES)
    bound = declaration(raw_declaration)
    if bound['inputSHA256'] != source_sha:
        raise ValueError('declaration belongs to different project bytes')
    output = new_path(output_path)
    if output in (source, declaration_path):
        raise ValueError('output must not alias an input')
    # A new private directory is owned by this operation. Partial output is
    # retained for diagnosis, but success is never inferred from its existence.
    output.mkdir(mode=0o700)
    started = time.perf_counter()
    record = dict(status='running', researchOnly=True, sourceSHA256=source_sha,
                  declarationSHA256=hashlib.sha256(raw_declaration).hexdigest(),
                  immutablePrefixCount=bound['immutablePrefixCount'],
                  prefixResponsibility='operator explicitly declares the complete immutable prefix; no semantic primer inference',
                  invocations=[])
    save_status(output, record)
    try:
        record['invocations'].append(run_native(binary, ['inspect', source, output/'inspection']))
        metadata = json.loads(read_bounded(output/'inspection/metadata.json', MAX_PROJECT_BYTES))
        if metadata['stateHash'] != bound['stateSHA256'] or metadata['eventCount'] != bound['eventCount']:
            raise ValueError('declaration does not match recovered native state/count')
        permutation, report = locality.schedule(metadata, bound['immutablePrefixCount'],
                                                 deadline=time.perf_counter()+locality.SCHEDULE_SECONDS)
        with (output/'permutation.txt').open('x') as stream:
            stream.write(''.join(str(i)+'\n' for i in permutation))
        with (output/'schedule.json').open('x') as stream:
            json.dump(report, stream, indent=2)
        record['invocations'].append(run_native(binary, ['reorder', source, output/'permutation.txt',
                                                         declaration_path, output/'native']))
        checks = json.loads(read_bounded(output/'native/checks.json', MAX_PROJECT_BYTES))
        required = ('passed', 'permutationBijection', 'primerVerbatim', 'blockBoundariesFixed',
                    'allOverlapOrderPreserved', 'fullStrokeEventProjectionEqual',
                    'relativePointTimesEqual', 'durationEqual', 'unaffectedSnapshotContentEqual',
                    'independentEventIndex', 'savedReopenedEqual', 'originalUnchanged',
                    'originalFinalLayerPhasePixelsEqual')
        if any(checks.get(key) is not True for key in required) or checks.get('sourceAssetReads') != 0:
            raise RuntimeError('incomplete native integrity checks')
        if (checks['inputFileSha256'] != source_sha or checks['inputStateHash'] != bound['stateSHA256'] or
                checks['immutablePrefixCount'] != bound['immutablePrefixCount']):
            raise RuntimeError('native input/prefix provenance differs')
        if input_contract(source)[1] != source_sha or read_bounded(declaration_path, MAX_DECLARATION_BYTES) != raw_declaration:
            raise RuntimeError('source/declaration changed during operation')
        record.update(status='completed', integrityPassed=True, nativeChecks=checks,
                      originalTravel=report['originalTravel'], retainedTravel=report['retainedTravel'],
                      candidateRetained=report['rawCandidateRetained'], schedulingSeconds=report['seconds'],
                      outputProject='native/reordered.draw',
                      meaning='pixel-preserving locality only; no semantic painting or image-quality gain')
    except BaseException as error:
        record.update(status='failed', integrityPassed=False, errorType=type(error).__name__, error=str(error))
        raise
    finally:
        record['seconds'] = time.perf_counter()-started
        save_status(output, record)
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--research', action='store_true', help='explicitly opt into the isolated research tool')
    parser.add_argument('--native', type=Path, help='compiled locality helper; otherwise env/default build path')
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('inspect', 'declare', 'reorder'):
        item = sub.add_parser(name)
        item.add_argument('--input', type=Path, required=True)
        item.add_argument('--output', type=Path, required=True)
        if name == 'declare':
            item.add_argument('--immutable-prefix-events', type=int, required=True,
                              help='explicit assertion of the COMPLETE prefix to protect, never inferred')
        if name == 'reorder':
            item.add_argument('--declaration', type=Path, required=True)
    args = parser.parse_args(argv)
    if not args.research:
        parser.error('--research is required; no stable/default entry point is changed')
    try:
        binary = native_path(args.native)
        if args.command == 'reorder':
            record = reorder(args.input, args.declaration, args.output, binary)
            print(json.dumps(dict(status=record['status'], outputProject=record['outputProject'],
                                  totalBefore=record['originalTravel']['total'],
                                  totalAfter=record['retainedTravel']['total'])))
        else:
            source, before = input_contract(args.input)
            output = new_path(args.output)
            if args.command == 'declare':
                if not 1 <= args.immutable_prefix_events <= locality.MAX_EVENTS:
                    raise ValueError('immutable prefix must be 1..8192; native event count is also verified')
                run_native(binary, ['declare', source, args.immutable_prefix_events, output])
                declaration(read_bounded(output, MAX_DECLARATION_BYTES))
            else:
                run_native(binary, ['inspect', source, output])
            if input_contract(source)[1] != before:
                raise RuntimeError('input changed')
            print('Research '+args.command+' completed')
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('FAILED: '+str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

"""Compare configured release inventory with CTest's actual complete test set."""
import argparse
import json
from pathlib import Path
import subprocess

SELF = 'Build.InventoryMatchesCTest'


def names(rows, label):
    if not isinstance(rows, list):
        raise AssertionError(label + ' is not a list')
    result = []
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get('name'), str) or not row['name']:
            raise AssertionError(label + ' has an invalid test name')
        result.append(row['name'])
    if len(result) != len(set(result)):
        raise AssertionError(label + ' has duplicate names')
    return set(result)


def verify(inventory, actual):
    declared = names(inventory['registered'], 'inventory')
    registered = names(actual['tests'], 'CTest')
    if SELF not in declared or SELF not in registered:
        raise AssertionError('inventory regression must include itself in both complete sets')
    if declared != registered:
        raise AssertionError('inventory mismatch: missing=' + str(sorted(registered - declared))
                             + '; extra=' + str(sorted(declared - registered)))
    return len(registered)


def negative_checks():
    full = [{'name': SELF}, {'name': 'late-feature'}]
    for inventory, actual in [
        ({'registered': full[:1]}, {'tests': full}),
        ({'registered': full}, {'tests': full[:1]}),
        ({'registered': full[1:]}, {'tests': full[1:]}),
        ({'registered': full + full[:1]}, {'tests': full}),
    ]:
        try:
            verify(inventory, actual)
        except AssertionError:
            continue
        raise AssertionError('negative inventory fixture incorrectly accepted')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--ctest', required=True)
    parser.add_argument('--config', default='')
    args = parser.parse_args()
    negative_checks()
    command = [args.ctest, '--test-dir', str(args.build.resolve()), '--show-only=json-v1']
    if args.config:
        command += ['-C', args.config]
    # Enumeration does not run tests or recursively invoke this test.
    result = subprocess.run(command, check=True, capture_output=True, text=True, timeout=45)
    actual = json.loads(result.stdout)
    inventory = json.loads((args.build / 'TEST_INVENTORY.json').read_text())
    count = verify(inventory, actual)
    print(f'PASS complete inventory equals actual CTest: {count} unique tests, including self')


if __name__ == '__main__':
    main()

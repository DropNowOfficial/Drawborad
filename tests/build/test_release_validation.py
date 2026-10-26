"""Release gates exercise configure/report behavior, not source-text spellings."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(add_help=False)
parser.add_argument('--cmake', default=shutil.which('cmake'))
args, unittest_args = parser.parse_known_args()


class ConfigureGateTests(unittest.TestCase):
    def configure(self, build, *extra):
        self.assertTrue(args.cmake, 'CMake is required for actual configure gate tests')
        return subprocess.run([args.cmake, '-S', str(ROOT), '-B', str(build),
                               '-DDRAWBORAD_DESKTOP=OFF', '-DDRAWBORAD_QUALIFY=OFF',
                               '-DDRAWBORAD_ONNXRUNTIME_ROOT=', *extra],
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def test_strict_integration_cannot_silently_become_core(self):
        with tempfile.TemporaryDirectory() as td:
            result = self.configure(td, '-DDRAWBORAD_TEST_PROFILE=RealWindowsIntegration')
            self.assertNotEqual(result.returncode, 0, result.stdout)
            for variable in ('DRAWBORAD_ONNXRUNTIME_ROOT', 'DRAWBORAD_TEST_COFFEE_IMAGE',
                             'DRAWBORAD_TEST_COFFEE_PACKAGE', 'DRAWBORAD_TEST_FULL_SCENE_REFERENCE'):
                self.assertIn(variable, result.stdout)
            status = json.loads((Path(td) / 'CONFIGURE_STATUS.json').read_text())
            self.assertEqual(status['status'], 'Blocked')

    def test_unknown_profile_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            result = self.configure(td, '-DDRAWBORAD_TEST_PROFILE=RealWindowsIntegraton')
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn('DRAWBORAD_TEST_PROFILE', result.stdout)

    def test_core_inventory_exposes_unregistered_real_tests(self):
        with tempfile.TemporaryDirectory() as td:
            result = self.configure(td, '-DDRAWBORAD_TEST_PROFILE=CorePortable')
            self.assertEqual(result.returncode, 0, result.stdout)
            inventory_path = Path(td) / 'TEST_INVENTORY.json'
            self.assertTrue(inventory_path.exists(), 'missing integration tests must be inventoried')
            inventory = json.loads(inventory_path.read_text())
            omitted = {test['name']: test for test in inventory['not_registered']}
            for name in ('M03.Coffee', 'M03.CoffeeAutomatic', 'U04.RealImageReview', 'E08.FullScene'):
                self.assertIn(name, omitted)
                self.assertEqual(omitted[name]['status'], 'Blocked')
                self.assertTrue(omitted[name]['reason'])
            self.assertTrue(inventory['registered'])
            if sys.platform != 'win32':
                self.assertNotIn('F06', [test['name'] for test in inventory['registered']])
                self.assertIn('F06', omitted)

    def test_switching_sdk_root_cannot_reuse_old_find_cache(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            sdk = root / 'sdk'; (sdk / 'include').mkdir(parents=True); (sdk / 'lib').mkdir()
            for name in ('onnxruntime_cxx_api.h', 'onnxruntime_c_api.h', 'onnxruntime_float16.h'):
                (sdk / 'include' / name).write_text('// configure-only SDK fixture\n')
            # This test exercises path/cache configuration only, never links or executes this fixture.
            if sys.platform == 'win32':
                (sdk / 'lib' / 'onnxruntime.lib').write_bytes(b'configure-only import fixture')
                dos = bytearray(64); dos[:2] = b'MZ'; dos[60:64] = (64).to_bytes(4, 'little')
                (sdk / 'lib' / 'onnxruntime.dll').write_bytes(dos + b'PE\0\0\x64\x86')
            else:
                (sdk / 'lib' / 'libonnxruntime.so').write_bytes(b'configure-only import fixture')
            build = root / 'build'
            first = self.configure(build, f'-DDRAWBORAD_ONNXRUNTIME_ROOT={sdk}')
            self.assertEqual(first.returncode, 0, first.stdout)
            second = self.configure(build, f'-DDRAWBORAD_ONNXRUNTIME_ROOT={root / "absent-sdk"}')
            self.assertNotEqual(second.returncode, 0, 'old SDK find-cache escaped new root validation')
            self.assertIn('DRAWBORAD_ONNXRUNTIME_ROOT', second.stdout)


class EvidenceReportTests(unittest.TestCase):
    def setUp(self):
        path = ROOT / 'tools/qa/release_report.py'
        self.assertTrue(path.exists(), 'release evidence report implementation is missing')
        spec = importlib.util.spec_from_file_location('drawborad_release_report', path)
        self.report = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.report)
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.junit = Path(self.directory.name) / 'results.xml'
        self.identity = {'git_sha': 'a' * 40, 'source_tree': 'b' * 40,
                         'source_fingerprint': 'c' * 64, 'source_dirty': False}
        self.inventory = {**self.identity, 'schema_version': 1, 'profile': 'CorePortable',
                          'configuration': 'Release', 'registered': [{'name': 'A', 'labels': ['CorePortable']}],
                          'not_registered': [{'name': 'M03.Coffee', 'status': 'Blocked',
                                              'reason': 'model package missing'}],
                          'dependencies': {}}
        self.inventory['inventory_fingerprint'] = hashlib.sha256(json.dumps(
            self.inventory, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8')).hexdigest()

    def summarize(self, xml, **overrides):
        self.junit.write_text(xml)
        receipt = {**self.identity, 'configuration': 'Release', 'profile': 'CorePortable',
                   'inventory_fingerprint': self.inventory['inventory_fingerprint'],
                   'ctest_exit_code': 0, 'elapsed_seconds': 1.25,
                   'junit_sha256': hashlib.sha256(self.junit.read_bytes()).hexdigest(),
                   'registered': ['A']}
        receipt.update(overrides)
        return self.report.summarize(self.inventory, self.junit, receipt, self.identity, 'Release')

    def test_current_complete_core_evidence_passes_only_core(self):
        result = self.summarize('<testsuite><testcase name="A" time="1.25"/></testsuite>')
        self.assertEqual(result['status'], 'Passed')
        self.assertEqual(result['counts']['passed'], 1)
        self.assertEqual(result['runtime_alpha']['status'], 'Blocked')
        self.assertEqual(result['photoshop_oracle']['status'], 'Blocked')
        self.assertEqual(result['fidelity']['historical_ssim'], 0.6487)

    def test_missing_registered_test_is_blocked(self):
        result = self.summarize('<testsuite/>')
        self.assertEqual(result['status'], 'Blocked')
        self.assertEqual(result['counts']['passed'], 0)
        self.assertIn('A', ' '.join(result['blockers']))

    def test_skipped_registered_test_is_not_success(self):
        result = self.summarize('<testsuite><testcase name="A"><skipped/></testcase></testsuite>')
        self.assertEqual(result['status'], 'Blocked')
        self.assertEqual(result['counts']['skipped'], 1)

    def test_failure_overrides_zero_process_exit(self):
        result = self.summarize('<testsuite><testcase name="A"><failure message="wrong pixels"/></testcase></testsuite>')
        self.assertEqual(result['status'], 'Failed')
        self.assertEqual(result['counts']['failed'], 1)

    def test_duplicate_or_unregistered_records_are_blocked(self):
        for xml in ('<testsuite><testcase name="A"/><testcase name="A"/></testsuite>',
                    '<testsuite><testcase name="A"/><testcase name="from-other-build"/></testsuite>'):
            with self.subTest(xml=xml):
                self.assertEqual(self.summarize(xml)['status'], 'Blocked')

    def test_source_configuration_and_junit_receipts_cannot_be_mixed(self):
        for mutation in ({'git_sha': 'd' * 40}, {'source_fingerprint': 'e' * 64},
                         {'configuration': 'Debug'}, {'junit_sha256': 'f' * 64},
                         {'registered': ['from-other-build']}):
            with self.subTest(mutation=mutation):
                self.assertEqual(self.summarize('<testsuite><testcase name="A"/></testsuite>', **mutation)['status'], 'Blocked')

    def test_model_or_reference_inputs_cannot_change_between_configure_and_test(self):
        self.inventory['inputs_fingerprint'] = '1' * 64
        self.identity['inputs_fingerprint'] = '2' * 64
        result = self.summarize('<testsuite><testcase name="A"/></testsuite>', inputs_fingerprint='1' * 64)
        self.assertEqual(result['status'], 'Blocked')

    def test_reconfiguration_cannot_reuse_an_old_execution_receipt(self):
        # Same commit/profile/test names, but different effective compiler settings.
        old_fingerprint = self.inventory['inventory_fingerprint']
        self.inventory['cmake_cache'] = {'DRAWBORAD_SANITIZE': 'ON', 'CMAKE_CXX_FLAGS_RELEASE': '-O0'}
        self.inventory['inventory_fingerprint'] = self.report.inventory_fingerprint(self.inventory)
        result = self.summarize('<testsuite><testcase name="A"/></testsuite>', inventory_fingerprint=old_fingerprint)
        self.assertEqual(result['status'], 'Blocked')

    def test_ctest_exit_status_must_be_an_actual_zero_integer(self):
        for value in (None, False, '0', 1):
            with self.subTest(value=value):
                result = self.summarize('<testsuite><testcase name="A"/></testsuite>', ctest_exit_code=value)
                self.assertEqual(result['status'], 'Blocked')

    def test_empty_malformed_or_missing_receipt_never_passes(self):
        self.assertEqual(self.summarize('not XML')['status'], 'Blocked')
        self.junit.write_text('<testsuite><testcase name="A"/></testsuite>')
        self.assertEqual(self.report.summarize(self.inventory, self.junit, {}, self.identity, 'Release')['status'], 'Blocked')

    def test_strict_profile_requires_real_windows_test_identities(self):
        self.inventory['profile'] = 'RealWindowsIntegration'
        result = self.summarize('<testsuite><testcase name="A"/></testsuite>', profile='RealWindowsIntegration')
        self.assertEqual(result['status'], 'Blocked')
        self.assertIn('M03.Coffee', ' '.join(result['blockers']))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *unittest_args])

"""Portable packaging contracts; no package install, server or credential creation."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / 'tools/packaging/runtime_platform.py'

class LinuxRuntimeTests(unittest.TestCase):
    def helper(self):
        self.assertTrue(HELPER.exists(), 'per-distribution runtime selector is missing')
        spec = importlib.util.spec_from_file_location('runtime_platform', HELPER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_supported_native_platforms_select_different_jpeg_abis(self):
        m = self.helper()
        ubuntu = m.runtime_packages('ubuntu', '24.04', 'x86_64')
        debian = m.runtime_packages('debian', '13', 'x86_64')
        self.assertIn('libjpeg-turbo8', ubuntu)
        self.assertNotIn('libjpeg62-turbo', ubuntu)
        self.assertIn('libjpeg62-turbo', debian)
        self.assertNotIn('libjpeg-turbo8', debian)
        self.assertIn('libqt6gui6', ubuntu)
        self.assertIn('libqt6gui6', debian)
        for packages in (ubuntu, debian):
            self.assertIn('python3-venv', packages)
            self.assertIn('util-linux', packages)
            self.assertIn('libwebpmux3', packages)
            self.assertIn('ca-certificates', packages)

    def test_unsupported_distribution_version_and_arch_fail_closed(self):
        m = self.helper()
        for args in [('debian','12','x86_64'), ('ubuntu','22.04','x86_64'),
                     ('linuxmint','24.04','x86_64'), ('debian','13','aarch64')]:
            with self.subTest(args=args), self.assertRaises(ValueError):
                m.runtime_packages(*args)

    def test_os_release_is_parsed_as_data_not_executed(self):
        m = self.helper()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'os-release'
            marker = Path(tmp) / 'must-not-exist'
            path.write_text(f'ID="debian"\nVERSION_ID="13"\nNAME="$(touch {marker})"\n')
            self.assertEqual(m.read_os_release(path)['ID'], 'debian')
            self.assertFalse(marker.exists())

    def test_bundle_must_match_native_os_version_and_arch(self):
        m = self.helper()
        good = dict(os_id='debian', version_id='13', arch='x86_64', git_sha='a'*40)
        m.validate_bundle(good, 'debian', '13', 'x86_64')
        for key, value in [('os_id','ubuntu'),('version_id','24.04'),('arch','aarch64')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                m.validate_bundle(dict(good, **{key:value}), 'debian', '13', 'x86_64')
        with self.assertRaises(ValueError):
            m.validate_bundle({}, 'debian', '13', 'x86_64')

    def test_preflight_is_read_only_and_does_not_create_credentials(self):
        m = self.helper()
        release = m.read_os_release(Path('/etc/os-release'))
        with tempfile.TemporaryDirectory() as tmp:
            bundle = Path(tmp) / 'bundle'
            bundle.mkdir()
            shutil.copytree(ROOT / 'tools', bundle / 'tools')
            shutil.copy(ROOT / 'tools/packaging/native-editor/start-cloud-session.sh', bundle)
            engine = bundle / 'DrawboradHeadless'
            engine.write_text('#!/bin/sh\ntest "$1" = --help\n')
            engine.chmod(0o700)
            (bundle / 'runtime-target.json').write_text(json.dumps(dict(
                os_id=release['ID'],version_id=release['VERSION_ID'],arch=os.uname().machine,git_sha='a'*40)))
            session = Path(tmp) / 'session'
            env = dict(os.environ, DRAWBORAD_SESSION_HOME=str(session))
            result = subprocess.run(['bash',str(bundle / 'start-cloud-session.sh'),'--check'],
                                    env=env,capture_output=True,text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Preflight passed', result.stdout)
            self.assertFalse(session.exists(), 'read-only preflight created session state')

    def test_foreign_bundle_is_rejected_before_running_engine_or_creating_state(self):
        self.helper()
        with tempfile.TemporaryDirectory() as tmp:
            bundle = Path(tmp) / 'bundle'
            bundle.mkdir()
            shutil.copytree(ROOT / 'tools', bundle / 'tools')
            shutil.copy(ROOT / 'tools/packaging/native-editor/start-cloud-session.sh', bundle)
            marker = Path(tmp) / 'engine-ran'
            engine = bundle / 'DrawboradHeadless'
            engine.write_text(f'#!/bin/sh\ntouch "{marker}"\n')
            engine.chmod(0o700)
            (bundle / 'runtime-target.json').write_text(json.dumps(dict(
                os_id='unsupported',version_id='0',arch='x86_64',git_sha='a'*40)))
            session = Path(tmp) / 'session'
            result = subprocess.run(['bash',str(bundle / 'start-cloud-session.sh')],
                env=dict(os.environ,DRAWBORAD_SESSION_HOME=str(session)),capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('does not match',result.stderr)
            self.assertFalse(marker.exists())
            self.assertFalse(session.exists())


    def test_missing_manifest_and_native_dependencies_do_not_create_state(self):
        m = self.helper()
        release = m.read_os_release()
        with tempfile.TemporaryDirectory() as tmp:
            bundle = Path(tmp) / 'bundle'
            bundle.mkdir()
            shutil.copytree(ROOT / 'tools', bundle / 'tools')
            shutil.copy(ROOT / 'tools/packaging/native-editor/start-cloud-session.sh', bundle)
            engine = bundle / 'DrawboradHeadless'
            engine.write_text('#!/bin/sh\nexit 127\n')
            engine.chmod(0o700)
            session = Path(tmp) / 'session'
            env = dict(os.environ,DRAWBORAD_SESSION_HOME=str(session))
            result = subprocess.run(['bash',str(bundle/'start-cloud-session.sh'),'--check'],
                                    env=env,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('runtime-target.json',result.stderr)
            self.assertFalse(session.exists())
            (bundle/'runtime-target.json').write_text(json.dumps(dict(
                os_id=release['ID'],version_id=release['VERSION_ID'],arch=os.uname().machine)))
            result = subprocess.run(['bash',str(bundle/'start-cloud-session.sh'),'--check'],
                                    env=env,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('Missing native libraries',result.stderr)
            self.assertFalse(session.exists())

    def test_installer_print_mode_has_no_side_effects(self):
        result = subprocess.run(['bash',str(ROOT/'tools/packaging/install-runtime-linux.sh'),'--print-packages'],
                                capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        m = self.helper()
        release = m.read_os_release()
        self.assertEqual(result.stdout.splitlines(),m.runtime_packages(
            release['ID'],release['VERSION_ID'],os.uname().machine))

if __name__ == '__main__':
    unittest.main()

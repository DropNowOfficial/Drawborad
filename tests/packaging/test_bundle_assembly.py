"""Exercise workflow archive assembly with explicitly SYNTHETIC payloads only.

This is not a native build, runtime smoke, browser test, or deployment. It runs
only the two packaging blocks against temporary fixture files. No installers,
native binaries, Docker commands, servers, or credential flows are invoked.
Requires only Python's standard library and base shell tools on a supported
Ubuntu 24.04 or Debian 13 x86_64 host.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / '.github/workflows/linux-headless-p0.yml'
TARGETS = {
    'ubuntu24.04-x86_64': ('ubuntu', '24.04', 'ubuntu:24.04'),
    'debian13-x86_64': ('debian', '13', 'debian:13'),
}


def packaging_block(name):
    """Read a named literal run block without adding a YAML test dependency."""
    lines = WORKFLOW.read_text().splitlines()
    start = lines.index('      - name: ' + name)
    if lines[start + 1] != '        run: |':
        raise AssertionError('Expected a literal packaging run block: ' + name)
    body = []
    for line in lines[start + 2:]:
        if line and not line.startswith('          '):
            break
        body.append(line[10:])
    return '\n'.join(body) + '\n'


class BundleAssemblyTests(unittest.TestCase):
    def test_archives_contain_matching_manifest_runtime_files_and_checksums(self):
        target = subprocess.check_output(
            ['python3', str(ROOT / 'tools/packaging/runtime_platform.py'), 'identify'],
            text=True).strip()
        os_id, version_id, image = TARGETS[target]
        expected = dict(os_id=os_id, version_id=version_id, arch='x86_64', git_sha='a' * 40)
        with tempfile.TemporaryDirectory(prefix='drawborad-synthetic-packaging-') as tmp:
            fixture = Path(tmp)
            shutil.copytree(ROOT / 'tools/packaging', fixture / 'tools/packaging',
                            ignore=shutil.ignore_patterns('__pycache__'))
            for name in ['cloud/__init__.py', 'cloud/requirements.txt', 'cloud/static/index.html',
                         'tools/segmentation/guided_grabcut.py', 'tools/models/sam3_semantic.py',
                         'tests/headless/verify_artifacts.py', 'tools/packaging/README.md',
                         'out/evidence/readiness.json', 'out/evidence/build-linked-libraries.txt',
                         'out/linux-headless/DrawboradHeadless']:
                path = fixture / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('SYNTHETIC PACKAGING PAYLOAD ONLY; NOT EXECUTABLE APPLICATION CODE\n')
            (fixture / 'out/linux-headless/DrawboradHeadless').chmod(0o755)
            env = dict(os.environ, GITHUB_SHA=expected['git_sha'], RUNTIME_OS_ID=os_id,
                       RUNTIME_VERSION_ID=version_id)
            for name in ['Package distro-native runtime',
                         'Package the authenticated editor for a persistent host']:
                script = packaging_block(name)
                script = script.replace('${{ matrix.target }}', target)
                script = script.replace('${{ matrix.image }}', image)
                self.assertNotIn('${{', script, 'Unresolved workflow expression in packaging block')
                subprocess.run(['bash', '-euo', 'pipefail', '-c', script], cwd=fixture,
                               env=env, check=True, capture_output=True, text=True)
            for prefix in ['DrawboradHeadless', 'DrawboradNativeEditor']:
                with self.subTest(archive=prefix, target=target):
                    archive = f'{prefix}-{target}.tar.gz'
                    subprocess.run(['sha256sum', '-c', archive + '.sha256'], cwd=fixture / 'out',
                                   check=True, capture_output=True, text=True)
                    extracted = fixture / prefix
                    extracted.mkdir()
                    with tarfile.open(fixture / 'out' / archive) as bundle:
                        bundle.extractall(extracted, filter='data')
                    subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=extracted,
                                   check=True, capture_output=True, text=True)
                    self.assertEqual(json.loads((extracted / 'runtime-target.json').read_text()), expected)
                    self.assertTrue(os.access(extracted / 'DrawboradHeadless', os.X_OK))
                    native_editor = prefix == 'DrawboradNativeEditor'
                    packaging = extracted / 'tools/packaging' if native_editor else extracted
                    for name in ['install-runtime-linux.sh', 'install-runtime-ubuntu.sh', 'runtime_platform.py']:
                        self.assertEqual((packaging / name).read_bytes(),
                                         (ROOT / 'tools/packaging' / name).read_bytes())
                    if native_editor:
                        self.assertEqual((extracted / 'start-cloud-session.sh').read_bytes(),
                                         (ROOT / 'tools/packaging/native-editor/start-cloud-session.sh').read_bytes())
                        self.assertIn('ARG RUNTIME_IMAGE=' + image + '\n', (extracted / 'Dockerfile').read_text())
                        dockerignore = (extracted / '.dockerignore').read_text().splitlines()
                        for name in ['runtime-target.json', 'tools/packaging/install-runtime-linux.sh',
                                     'tools/packaging/runtime_platform.py']:
                            self.assertIn('!' + name, dockerignore)
                    (extracted / 'runtime-target.json').write_text('{}\n')
                    tampered = subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=extracted,
                                              capture_output=True, text=True)
                    self.assertNotEqual(tampered.returncode, 0, 'Manifest tampering was not detected')


if __name__ == '__main__':
    unittest.main()

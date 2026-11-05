"""Assemble P0 evidence from executed CTest and real workflow artifacts."""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

build = Path(sys.argv[1]).resolve()
bundle = Path(sys.argv[2]).resolve()
bundle.mkdir(parents=True, exist_ok=True)
artifacts = bundle / 'painting'
report = json.loads((artifacts / 'report.json').read_text())
inventory = json.loads((build / 'TEST_INVENTORY.json').read_text())
registered = json.loads((bundle / 'ctest-inventory.json').read_text())
assert {r['name'] for r in inventory['registered']} == {r['name'] for r in registered['tests']}
xml = ET.parse(bundle / 'ctest.xml').getroot()
cases = list(xml.iter('testcase'))
failed = sum(c.find('failure') is not None or c.find('error') is not None for c in cases)
skipped = sum(c.find('skipped') is not None for c in cases)
assert cases and not failed and not skipped
sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
assert sha == report['gitSha'] == inventory['git_sha']
for filename in ['CMakeCache.txt', 'TEST_INVENTORY.json', 'CONFIGURE_STATUS.json']:
    shutil.copy2(build / filename, bundle / filename)
shutil.copy2(build / 'Testing/Temporary/LastTest.log', bundle / 'LastTest.log')
summary = dict(report)
summary.update(repository='DropNowOfficial/Drawborad', branch=os.environ.get('GITHUB_HEAD_REF') or os.environ.get('GITHUB_REF_NAME'),
               platform=platform.platform(), buildType='Release', tests={'passed': len(cases)-failed-skipped, 'failed': failed, 'skipped': skipped},
               notRegistered=inventory['not_registered'], dependencies=inventory['dependencies'],
               actionsRun=os.environ.get('GITHUB_RUN_ID'),
               independentOra=json.loads((bundle / 'ora-validation.json').read_text()))
(bundle / 'readiness.json').write_text(json.dumps(summary, indent=2)+'\n')
(bundle / 'P0_READINESS.md').write_text(
    '# Linux Headless P0 evidence\n\n'
    f'Commit: `{sha}`\n\nUbuntu Release build and {len(cases)} CTest entries passed.\n\n'
    f"Fixture: `{report['backend']}`; objects: {report['objectCount']}; strokes: {report['strokeEvents']}; paint layers: {report['paintLayers']}.\n\n"
    'The C++ test removes its source image, reopens `.draw`, compares final pixels, and exercises ReplayEngine per layer. '
    'The independent Python verifier decodes ORA PNG leaves and composites them against final.png.\n\n'
    'SAM 3 inference: Not Tested. Real coffee input unavailable. High fidelity: NeedsReview. '
    'Natural process: Not Reviewed. Photoshop: Blocked — Not Run. Windows desktop: Not Run.\n\n'
    'See readiness.json and TEST_INVENTORY.json for exact environment and unavailable tests.\n')
lines=[]
for path in sorted(bundle.rglob('*')):
    if path.is_file() and path.name != 'SHA256SUMS':
        lines.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.relative_to(bundle).as_posix())
(bundle / 'SHA256SUMS').write_text('\n'.join(lines)+'\n')
print(json.dumps({'sha':sha, 'tests':summary['tests'], 'bundle':str(bundle)}))

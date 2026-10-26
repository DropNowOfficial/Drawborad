#!/usr/bin/env python3
"""Offline, platform-independent package wiring checks; not a Windows build."""
from pathlib import Path
import unittest
import yaml
ROOT=Path(__file__).resolve().parents[2]

class AlphaPackagingContract(unittest.TestCase):
    def test_actions_is_manual_and_cannot_trigger_a_push_build(self):
        data=yaml.load((ROOT/'.github/workflows/windows-alpha-portable.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(set(data['on']), {'workflow_dispatch'})
        self.assertEqual(data['on']['workflow_dispatch']['inputs']['expected_commit']['required'], 'true')

    def test_hosted_job_is_candidate_guarded_and_explicitly_ui_only(self):
        data=yaml.load((ROOT/'.github/workflows/windows-alpha-portable.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(data['jobs']['package-windows-alpha']['runs-on'],'windows-2022')
        guard=data['jobs']['package-windows-alpha'].get('if','')
        self.assertIn("github.event_name == 'workflow_dispatch'", guard)
        self.assertIn("github.ref == 'refs/heads/release/win11-integrated-alpha-rc1'", guard)
        self.assertIn('github.sha == inputs.expected_commit', guard)
        steps=data['jobs']['package-windows-alpha']['steps']
        self.assertTrue(any('install-qt-action' in x.get('uses','') for x in steps))
        builds=[x for x in steps if 'Build-Alpha.ps1' in x.get('run','')]
        self.assertEqual(len(builds),1)
        self.assertIn('-UiOnly', builds[0]['run'])
        self.assertTrue(any('upload-artifact' in x.get('uses','') and 'zip' in x['with']['path'] for x in steps))

    def test_hosted_sdk_preflight_precedes_dependency_installation(self):
        data=yaml.load((ROOT/'.github/workflows/windows-alpha-portable.yml').read_text(), Loader=yaml.BaseLoader)
        inputs=data['on']['workflow_dispatch']['inputs']
        self.assertIn('native_sdk_root', inputs)
        self.assertIn('cmake_toolchain_file', inputs)
        self.assertEqual(inputs['native_sdk_root']['required'], 'false')
        steps=data['jobs']['package-windows-alpha']['steps']
        ids={step.get('id'):index for index,step in enumerate(steps) if step.get('id')}
        self.assertIn('native_sdk_preflight', ids)
        for index, step in enumerate(steps):
            if 'setup-python' in step.get('uses','') or 'install-qt-action' in step.get('uses',''):
                self.assertLess(ids['native_sdk_preflight'],index)
    def test_reference_preview_applies_exif_orientation(self):
        qml=(ROOT/'ui/Main.qml').read_text()
        cpp=(ROOT/'apps/studio/QtBridge.cpp').read_text()
        self.assertIn('reader.setAutoTransform(true)',cpp)
        self.assertRegex(qml,r'(?s)id:\s*referencePreview.*?autoTransform:\s*true')

    def test_ui_labels_reference_as_preview_only(self):
        qml=(ROOT/'ui/Main.qml').read_text()
        cpp=(ROOT/'apps/studio/QtBridge.cpp').read_text()
        self.assertIn('studio.importReferenceUrl(selectedFile)',qml)
        self.assertIn('preview-only',qml)
        self.assertIn('showReference',qml)
        self.assertIn('referenceOpacity',qml)
        self.assertIn('sameCanvas',cpp)
        self.assertIn('validateAlphaReferenceCandidate',cpp)

if __name__=='__main__': unittest.main()

#!/usr/bin/env python3
"""Offline, platform-independent package wiring checks; not a Windows build."""
from pathlib import Path
import unittest
import yaml
ROOT=Path(__file__).resolve().parents[2]

class AlphaPackagingContract(unittest.TestCase):
    def test_actions_builds_a_real_windows_exe_and_uploads_zip(self):
        data=yaml.safe_load((ROOT/'.github/workflows/windows-alpha-portable.yml').read_text())
        self.assertEqual(data['jobs']['package-windows-alpha']['runs-on'],'windows-2022')
        steps=data['jobs']['package-windows-alpha']['steps']
        self.assertTrue(any('install-qt-action' in x.get('uses','') for x in steps))
        self.assertTrue(any('Build-Alpha.ps1' in x.get('run','') for x in steps))
        self.assertTrue(any('upload-artifact' in x.get('uses','') and 'zip' in x['with']['path'] for x in steps))
    def test_local_builder_checks_windows_dependencies_and_diagnostics(self):
        s=(ROOT/'tools/windows/Build-Alpha.ps1').read_text()
        for item in ('Drawborad.exe','DrawboradWorker.exe','drawborad_build_token.txt',
                     'windeployqt.exe','--qmldir','platforms\\qwindows.dll',
                     '--diagnostics','Get-FileHash','Compress-Archive','ALPHA_ZIP='):
            self.assertIn(item,s)
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

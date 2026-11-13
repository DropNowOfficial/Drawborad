"""Catch direct renderer dependencies omitted by isolated native build scripts."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ('tools/experimental/automatic_stages/build_native.sh',
           'tools/experimental/flow_planner/build.sh')

def dependencies(root):
    text = (root / 'src/render/CoverageRasterizer.cpp').read_text()
    names = re.findall(r'^#include "([A-Za-z0-9_]*Coverage)\.h"', text, re.M)
    return sorted('src/render/' + name + '.cpp' for name in names
                  if (root / ('src/render/' + name + '.cpp')).is_file())

def missing(required, script):
    code = '\n'.join(line for line in script.splitlines() if not line.lstrip().startswith('#'))
    listed = set(re.findall(r'src/render/[A-Za-z0-9_]+\.cpp', code))
    return sorted(set(required) - listed)

def target_sources(path, script):
    if 'flow_planner' not in path:
        return {'native-plan': script}
    shared = re.findall(r'^c\+\+[^\n]*-shared[^\n]*$', script, re.M)
    arrays = re.findall(r'^modules=\((.*?)\)', script, re.M | re.S)
    if len(shared) != 1 or len(arrays) != 1:
        raise AssertionError('flow build target layout changed; update per-target validation')
    modules = arrays[0].split()
    if not all(re.fullmatch(r'[A-Za-z0-9_/]+', x) for x in modules):
        raise AssertionError('nonliteral module list requires explicit validation')
    return {'libflowfeedback': shared[0],
            'native-flow': ' '.join('src/' + x + '.cpp' for x in modules)}

class CoverageLists(unittest.TestCase):
    def test_isolated_builds_cover_dispatcher(self):
        required = dependencies(ROOT)
        self.assertTrue(required)
        for path in SCRIPTS:
            for target, sources in target_sources(path, (ROOT / path).read_text()).items():
                with self.subTest(script=path, target=target):
                    self.assertEqual([], missing(required, sources))
    def test_shared_target_cannot_hide_native_target_omission(self):
        script = 'c++ -shared src/render/ChiselInkCoverage.cpp -o lib.so\nmodules=(render/CoverageRasterizer)\n'
        targets = target_sources('flow_planner/build.sh', script)
        self.assertEqual([], missing(['src/render/ChiselInkCoverage.cpp'], targets['libflowfeedback']))
        self.assertEqual(['src/render/ChiselInkCoverage.cpp'], missing(['src/render/ChiselInkCoverage.cpp'], targets['native-flow']))
    def test_detector_rejects_omission_and_comment_only_entry(self):
        for path in dependencies(ROOT):
            self.assertEqual([path], missing([path], '# ' + path + '\n'))
            self.assertEqual([], missing([path], 'c++ "${source}/' + path + '"'))

if __name__ == '__main__':
    unittest.main()

"""Catch direct renderer dependencies omitted by isolated native build scripts."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

def dependencies(root):
    text = (root / 'src/render/CoverageRasterizer.cpp').read_text()
    names = re.findall(r'^#include "([A-Za-z0-9_]*Coverage)\.h"', text, re.M)
    return sorted('src/render/' + name + '.cpp' for name in names
                  if (root / ('src/render/' + name + '.cpp')).is_file())

def source_code(script):
    # Preserve quoted # but remove shell comments before scanning filenames.
    joined = script.replace('\\\n', ' ')
    lines = []
    for line in joined.splitlines():
        quote = None
        escaped = False
        for i, char in enumerate(line):
            if escaped:
                escaped = False
                continue
            if char == "\\" and quote != "'":
                escaped = True
                continue
            if quote:
                if char == quote:
                    quote = None
            elif char in ("'", '"'):
                quote = char
            elif char == '#' and (i == 0 or line[i - 1].isspace()):
                line = line[:i]
                break
        lines.append(line.strip())
    return '\n'.join(lines)

COMPILER = re.compile(r'^(?:c\+\+|g\+\+|clang\+\+|"\$\{CXX:-c\+\+\}")\s')
COMPILER_ANY = re.compile(r'(?:^|\s)(?:c\+\+|g\+\+|clang\+\+|"\$\{CXX:-c\+\+\}")\s')

def missing(required, script):
    code = source_code(script)
    code = re.sub(r'src/render/\{([A-Za-z0-9_,]+)\}\.cpp', lambda m: ' '.join('src/render/' + name + '.cpp' for name in m[1].split(',')), code)
    listed = set(re.findall(r'src/render/[A-Za-z0-9_]+\.cpp', code))
    return sorted(set(required) - listed)

def scripts(root):
    shells = [path for path in root.rglob('*.sh') if 'CoverageRasterizer' in source_code(path.read_text()) or any(COMPILER_ANY.search(line) and '--version' not in line for line in source_code(path.read_text()).splitlines())]
    docs = [path for path in root.rglob('*.md') if any(COMPILER_ANY.search(line) and 'CoverageRasterizer' in line for line in source_code(path.read_text()).splitlines())]
    return sorted(shells + docs)

def target_sources(path, script):
    code = source_code(script)
    targets = {}
    arrays = re.findall(r'^modules=\((.*?)\)', code, re.M | re.S)
    if len(arrays) > 1:
        raise AssertionError('multiple module lists need explicit target validation: ' + path)
    for array in arrays:
        modules = array.split()
        if not all(re.fullmatch(r'[A-Za-z0-9_/]+', x) for x in modules):
            raise AssertionError('nonliteral module list requires explicit validation: ' + path)
        if 'render/CoverageRasterizer' in modules:
            targets['module-target'] = ' '.join('src/' + x + '.cpp' for x in modules)
    for i, line in enumerate(code.splitlines()):
        if COMPILER_ANY.search(line):
            if '--version' in line:
                continue
            if not COMPILER.match(line):
                raise AssertionError('unsupported compiler prefix: ' + path)
            if 'CoverageRasterizer' in line:
                targets['direct-target-' + str(i)] = line
            elif '"${files[@]}"' in line and 'module-target' in targets:
                # The current scripts populate files only from literal modules.
                if not re.search(r'files=\(\); for module in "\$\{modules\[@\]\}"; do files\+=\("\$source/src/\$module\.cpp"\); done', code):
                    raise AssertionError('unknown module expansion: ' + path)
            else:
                raise AssertionError('unclassified independent compiler target: ' + path)
        elif path.endswith('.sh') and 'CoverageRasterizer' in line and not line.startswith('modules=('):
            raise AssertionError('unclassified renderer source list: ' + path)
    if not targets:
        raise AssertionError('unclassified renderer build layout: ' + path)
    return targets

class CoverageLists(unittest.TestCase):
    def test_isolated_builds_cover_dispatcher(self):
        required = dependencies(ROOT)
        self.assertTrue(required)
        found = scripts(ROOT)
        self.assertTrue(found)
        for path in found:
            for target, sources in target_sources(str(path), path.read_text()).items():
                with self.subTest(script=path, target=target):
                    self.assertEqual([], missing(required, sources))
    def test_shared_target_cannot_hide_native_target_omission(self):
        script = 'c++ -shared src/render/CoverageRasterizer.cpp src/render/ChiselInkCoverage.cpp -o lib.so\nmodules=(render/CoverageRasterizer)\n'
        targets = target_sources('any/build.sh', script)
        self.assertEqual([], missing(['src/render/ChiselInkCoverage.cpp'], targets['direct-target-0']))
        self.assertEqual(['src/render/ChiselInkCoverage.cpp'], missing(['src/render/ChiselInkCoverage.cpp'], targets['module-target']))
    def test_new_module_script_discovered_without_filename_allowlist(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / 'new-unlisted-target.sh'
            path.write_text('modules=(render/CoverageRasterizer render/FineLineCoverage)\n')
            self.assertEqual([path], scripts(root))
            self.assertEqual(['src/render/ChiselInkCoverage.cpp'], missing(['src/render/ChiselInkCoverage.cpp'], target_sources(str(path), path.read_text())['module-target']))
    def test_unknown_layout_fails_closed(self):
        with self.assertRaises(AssertionError):
            target_sources('unknown.sh', 'files=(render/CoverageRasterizer)')
    def test_detector_rejects_omission_and_comment_only_entry(self):
        for path in dependencies(ROOT):
            self.assertEqual([path], missing([path], '# ' + path + '\n'))
            self.assertEqual([], missing([path], 'c++ "${source}/' + path + '"'))

    def test_new_dynamic_compiler_script_cannot_escape_discovery(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / 'new-dynamic.sh'
            path.write_text('c++ "${unknown_sources[@]}" -o new\n')
            self.assertEqual([path], scripts(root))
            with self.assertRaises(AssertionError):
                target_sources(str(path), path.read_text())
    def test_inline_comment_does_not_satisfy_dependency(self):
        self.assertEqual(['src/render/ChiselInkCoverage.cpp'], missing(['src/render/ChiselInkCoverage.cpp'], 'c++ src/render/CoverageRasterizer.cpp -o out # src/render/ChiselInkCoverage.cpp'))
    def test_sibling_unknown_target_is_not_hidden_by_valid_modules(self):
        prefix = 'modules=(render/CoverageRasterizer render/ChiselInkCoverage)\n'
        for extra in ['other=(render/CoverageRasterizer)\n', 'c++ "${other[@]}" -o other\n', 'env c++ src/render/CoverageRasterizer.cpp -o other\n']:
            with self.subTest(extra=extra), self.assertRaises(AssertionError):
                target_sources('new.sh', prefix + extra)
    def test_indented_brace_document_target_and_continuation(self):
        script = '  g++ -std=c++20 \\\n src/render/{CoverageRasterizer,FineLineCoverage,ChiselInkCoverage}.cpp -o test\n'
        targets = target_sources('README.md', script)
        self.assertEqual(1, len(targets))
        self.assertEqual([], missing(['src/render/ChiselInkCoverage.cpp'], next(iter(targets.values()))))
    def test_each_actual_target_omission_is_detected(self):
        for path in scripts(ROOT):
            for target, sources in target_sources(str(path), path.read_text()).items():
                expanded = re.sub(r'src/render/\{([A-Za-z0-9_,]+)\}\.cpp', lambda m: ' '.join('src/render/' + x + '.cpp' for x in m[1].split(',')), sources)
                for dependency in dependencies(ROOT):
                    with self.subTest(path=path, target=target, dependency=dependency):
                        self.assertIn(dependency, missing([dependency], expanded.replace(dependency, '') + ' # ' + dependency))

if __name__ == '__main__':
    unittest.main()

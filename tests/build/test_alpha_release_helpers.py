#!/usr/bin/env python3
"""Portable failure-contract tests for the Windows release evidence helper.

These exercise recorded DUMPBIN/deployment inputs, not a Windows native build.
"""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "tools/windows/alpha_release.py"
X64_HEADER = "FILE HEADER VALUES\n            8664 machine (x64)\n"
X64_IMPORT_HEADER = "Version      : 0\nMachine      : 8664 (x64)\n"


class AlphaReleaseHelpers(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.package = self.root / "package"
        self.package.mkdir()
        self.system = self.root / "system32"
        self.system.mkdir()

    def invoke(self, command, value, expected=0, *arguments):
        source = self.root / "input.json"
        source.write_text(json.dumps(value), encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(HELPER), command, "--input", str(source), *arguments],
            capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def record(self, target, dependencies=(), headers=X64_HEADER):
        source = self.package / target
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(b"recorded native file")
        return {
            "target": target,
            "source": str(source),
            "headers": headers,
            "dependents": "File Type: EXECUTABLE IMAGE\n\n"
                          "  Image has the following dependencies:\n"
                          + "".join("    " + name + "\n" for name in dependencies)
                          + "\n  Summary\n",
        }

    def audit(self, records, expected=0):
        return self.invoke("audit-records", records, expected,
                           "--system-dir", str(self.system))

    def test_accepts_both_x64_dll_and_import_library_headers(self):
        result = self.invoke("machine", {"headers": X64_HEADER + X64_IMPORT_HEADER})
        self.assertEqual(result["machines"], ["8664"])
        self.assertEqual(result["status"], "Passed")

    def test_rejects_mixed_architecture_import_library(self):
        result = self.invoke("machine", {"headers": X64_IMPORT_HEADER +
                             "Machine      : 14C (x86)\n"}, 1)
        self.assertIn("x64", result["error"])

    def test_rejects_unrecognized_header_output(self):
        result = self.invoke("machine", {"headers": "DUMPBIN failed to open file"}, 1)
        self.assertIn("machine", result["error"])

    def test_dependency_audit_resolves_case_insensitive_package_root(self):
        (self.system / "KERNEL32.dll").write_bytes(b"system fixture")
        records = [
            self.record("Drawborad.exe", ["Qt6Core.dll", "KERNEL32.dll"]),
            self.record("qt6core.dll", ["api-ms-win-core-file-l1-2-0.dll"]),
            self.record("platforms/qwindows.dll", ["QT6CORE.DLL"]),
        ]
        result = self.audit(records)
        self.assertEqual(result["status"], "Passed")
        self.assertEqual(len(result["files"]), 3)
        self.assertTrue(all(len(item["sha256"]) == 64 for item in result["files"]))

    def test_missing_transitive_dll_cannot_pass(self):
        records = [self.record("Drawborad.exe", ["onnxruntime.dll"]),
                   self.record("onnxruntime.dll", ["missing-provider.dll"])]
        result = self.audit(records, 1)
        self.assertIn("missing-provider.dll", " ".join(result["errors"]))

    def test_unrelated_plugin_directory_does_not_satisfy_exe_import(self):
        records = [self.record("Drawborad.exe", ["private.dll"]),
                   self.record("unused/private.dll")]
        result = self.audit(records, 1)
        self.assertIn("private.dll", " ".join(result["errors"]))

    def test_arbitrary_system32_file_is_not_treated_as_windows_dependency(self):
        (self.system / "vendor.dll").write_bytes(b"not a Windows component")
        result = self.audit([self.record("Drawborad.exe", ["vendor.dll"])], 1)
        self.assertIn("vendor.dll", " ".join(result["errors"]))

    def test_windows_accessibility_dependency_must_exist_in_system32(self):
        records = [self.record("platforms/qwindows.dll", ["UIAutomationCore.dll"])]
        self.audit(records, 1)
        (self.system / "UIAutomationCore.dll").write_bytes(b"Windows accessibility fixture")
        self.assertEqual(self.audit(records)["status"], "Passed")

    def test_rejects_debug_runtime_even_if_installed(self):
        (self.system / "VCRUNTIME140D.dll").write_bytes(b"debug runtime")
        result = self.audit([self.record("Drawborad.exe", ["VCRUNTIME140D.dll"])], 1)
        self.assertIn("debug runtime", " ".join(result["errors"]))

    def test_rejects_non_x64_deployed_dll(self):
        result = self.audit([self.record("Drawborad.exe"),
                             self.record("Qt6Core.dll", headers="14C machine (x86)\n")], 1)
        self.assertIn("x64", " ".join(result["errors"]))

    def test_non_dll_import_suffix_is_not_silently_ignored(self):
        result = self.audit([self.record("Drawborad.exe", ["missing-driver.drv"])], 1)
        self.assertIn("missing-driver.drv", " ".join(result["errors"]))

    def test_import_name_with_spaces_is_not_silently_ignored(self):
        result = self.audit([self.record("Drawborad.exe", ["vendor helper.dll"])], 1)
        self.assertIn("vendor helper.dll", " ".join(result["errors"]))

    def test_dry_run_plan_keeps_plugin_destination(self):
        source = self.root / "qwindows.dll"
        source.write_bytes(b"plugin fixture")
        plan = {"files": [{"source": str(source), "target": "platforms/qwindows.dll"}]}
        result = self.invoke("plan", plan, 0, "--root", str(self.package))
        self.assertEqual(result["files"], [{"source": str(source),
                                           "target": "platforms/qwindows.dll"}])

    def test_dry_run_plan_rejects_destination_outside_package(self):
        source = self.root / "qwindows.dll"
        source.write_bytes(b"plugin fixture")
        plan = {"files": [{"source": str(source), "target": "../qwindows.dll"}]}
        result = self.invoke("plan", plan, 1, "--root", str(self.package))
        self.assertIn("outside", result["error"])

    def test_reusing_cache_with_different_ort_root_is_rejected(self):
        cache = self.root / "CMakeCache.txt"
        cache.write_text("CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n"
                         "DRAWBORAD_ONNXRUNTIME_ROOT:PATH=C:/old-ort\n", encoding="utf-8")
        expected = {"CMAKE_GENERATOR": "Visual Studio 17 2022",
                    "DRAWBORAD_ONNXRUNTIME_ROOT": "C:/new-ort"}
        result = self.invoke("check-cache", expected, 1, "--cache", str(cache))
        self.assertIn("DRAWBORAD_ONNXRUNTIME_ROOT", result["error"])

    def test_compatible_cache_is_reused(self):
        cache = self.root / "CMakeCache.txt"
        cache.write_text("CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n"
                         "DRAWBORAD_TEST_PROFILE:STRING=CorePortable\n", encoding="utf-8")
        expected = {"CMAKE_GENERATOR": "Visual Studio 17 2022",
                    "DRAWBORAD_TEST_PROFILE": "CorePortable"}
        result = self.invoke("check-cache", expected, 0, "--cache", str(cache))
        self.assertTrue(result["reused"])

    def test_clean_smoke_environment_excludes_developer_search_paths(self):
        environment = {"PATH": "C:/Qt/bin;C:/ort/lib;C:/Python", "SystemRoot": "C:/Windows",
                       "QT_PLUGIN_PATH": "C:/Qt/plugins", "QML2_IMPORT_PATH": "C:/Qt/qml",
                       "QSG_RHI_BACKEND": "software", "CMAKE_PREFIX_PATH": "C:/Qt",
                       "PYTHONHOME": "C:/developer/python", "PYTHONPATH": "C:/developer/modules",
                       "USERPROFILE": "C:/Users/tester"}
        result = self.invoke("clean-environment", environment, 0,
                             "--root", "C:/Extracted/Drawborad")
        self.assertEqual(result["environment"]["PATH"],
                         "C:/Extracted/Drawborad;C:/Windows/System32;C:/Windows")
        for key in ("QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "QSG_RHI_BACKEND", "CMAKE_PREFIX_PATH",
                    "PYTHONHOME", "PYTHONPATH"):
            self.assertNotIn(key, result["environment"])
        self.assertEqual(result["environment"]["USERPROFILE"], "C:/Users/tester")

    def codec_sdk(self):
        sdk = self.root / "native-sdk"
        triplet = sdk / "x64-windows"
        files = ["include/lcms2.h", "lib/lcms2.lib", "share/lcms/copyright",
                 "include/png.h", "lib/libpng16.lib", "share/libpng/copyright",
                 "include/jpeglib.h", "lib/jpeg.lib", "share/libjpeg-turbo/copyright",
                 "include/tiffio.h", "lib/tiff.lib", "share/tiff/copyright",
                 "include/webp/encode.h", "include/webp/decode.h", "include/webp/mux.h",
                 "lib/webp.lib", "lib/webpmux.lib", "share/libwebp/copyright",
                 "include/zlib.h", "lib/zlib.lib", "share/zlib/copyright", "bin/libpng16.dll"]
        for relative in files:
            path = triplet / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"abc")
        toolchain = self.root / "vcpkg.cmake"
        toolchain.write_bytes(b"abc")
        status = sdk / "vcpkg/status"
        status.parent.mkdir(parents=True, exist_ok=True)
        status.write_bytes(b"abc")
        return {"root": str(sdk), "toolchain": str(toolchain)}

    def test_missing_hosted_sdk_is_blocked_before_build(self):
        result = self.invoke("codec-sdk", {"root": "", "toolchain": ""}, 1)
        self.assertEqual(result["status"], "Blocked")
        self.assertIn("preinstalled", " ".join(result["blockers"]))

    def test_incomplete_codec_sdk_is_blocked(self):
        request = self.codec_sdk()
        (Path(request["root"]) / "x64-windows/lib/jpeg.lib").unlink()
        result = self.invoke("codec-sdk", request, 1)
        self.assertEqual(result["status"], "Blocked")
        self.assertIn("lib/jpeg.lib", " ".join(result["blockers"]))

    def test_preinstalled_codec_inputs_are_hashed_without_downloading(self):
        result = self.invoke("codec-sdk", self.codec_sdk())
        self.assertEqual(result["status"], "Passed")
        self.assertTrue(result["files"])
        self.assertEqual({row["sha256"] for row in result["files"]},
                         {"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"})
        self.assertIn("not native qualification", result["scope"])

    def test_sdk_without_installed_package_version_ledger_is_blocked(self):
        request = self.codec_sdk()
        (Path(request["root"]) / "vcpkg/status").unlink()
        result = self.invoke("codec-sdk", request, 1)
        self.assertEqual(result["status"], "Blocked")
        self.assertIn("vcpkg/status", " ".join(result["blockers"]))


if __name__ == "__main__":
    unittest.main()

# Drawborad RC1 candidate

This is an unsigned test candidate. Read `BUILD_INFO.json` for its exact source
commit, package mode, host, toolchain and dependency versions. `UIOnly` packages
do not contain ONNX Runtime. `Integrated-CPU` packages contain the CPU runtime,
but do not include model weights. A successful startup check does not establish
Runtime Alpha acceptance or painting fidelity.

## Start and diagnose

1. Verify the ZIP SHA-256 against the adjacent `.sha256.txt` file, then extract
   the entire ZIP to a writable local directory.
2. Install the official Microsoft Visual C++ 2015-2022 **x64** Redistributable,
   at least as new as the compiler recorded in `BUILD_INFO.json`, if it is not
   already installed. Use Microsoft's supported download:
   <https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist>.
3. Start `Drawborad.exe` as a normal user. Keep its DLLs, Qt plugin directories,
   worker, codec and build-token file together.
4. Run `Run-Diagnostics.cmd` to collect device information and a native WARP
   startup report. This does not run a model or modify a painting.
5. Keep originals and save test copies. Report the full source SHA, exact
   operation, visible error, Windows build, and the generated diagnostics.

The Microsoft compiler-runtime DLLs are deliberately not copied from a developer
machine. Actual upstream SDK license and notice files are under `licenses/`.

## What the evidence means

`TEST_RESULTS.json` contains the complete configured-suite result, registered
and unregistered test inventory, skips/blockers, and the extracted-candidate
startup/worker result. `evidence/` contains configure/build logs, the official
Qt deployment dry-run, the planned and deployed import audits, and test receipts.
The final immutable ZIP is extracted and tested again; that result is the
adjacent `.smoke.json` file. No checksum is published for a failed final smoke.

Model fixtures used during validation are not distributed in the ZIP. Use a
verified compatible offline model package through the application's model
selection controls. A CPU runtime being present does not make an absent or
incompatible model ready. See `KNOWN_LIMITATIONS.md` before interpreting results.

## Reproduce an integrated local build

Use a clean checkout of the exact candidate commit on Windows 11 x64. Required
tools are Visual Studio 2022 with the x64 C++ workload, CMake 3.25+, Python 3.9+
with PyYAML, and Qt 6.12 for MSVC2022 x64 including Qt Test. Supply the matching
Qt license/notice bundle if the SDK does not provide a discoverable one.

From a normal PowerShell session, invoke `tools/windows/Build-Alpha.ps1` with:

| Parameter | Input |
|---|---|
| `-QtRoot` | Qt 6.12 `msvc2022_64` directory |
| `-QtLicensesRoot` | Actual notices from the same Qt SDK/source distribution |
| `-OnnxRuntimeRoot` | CPU x64 ORT SDK containing `include`, `lib`, `LICENSE` and `ThirdPartyNotices.txt` |
| `-CoffeeImage` | The exact pinned coffee image required by the integration gate |
| `-CoffeePackage` | Verified model package with manifest, encoder and decoder |
| `-CoffeeReference` | Directory containing the official prompted tensors and masks |
| `-CoffeeAutoReference` | Directory containing the official automatic masks |
| `-CoffeeFullSceneReference` | Directory containing the required full-scene reference and reviewed scene |
| `-ExpectedCommit` | Exact 40-character candidate Git SHA |

The complete C03/C06 suite links native PNG, JPEG, TIFF, WebP (including mux),
zlib and LCMS development libraries through CMake. For the offline vcpkg SDK,
provide both `-CMakeToolchainFile` and `-VcpkgInstalledDir`; the latter must contain
`x64-windows/bin` and `x64-windows/share` with real copyright notices. The local
SDK must also retain `vcpkg/status`. Its actual package versions and triplets are
included in `BUILD_INFO.json` and copied unchanged to
`licenses/native/vcpkg-status.txt`, with its SHA-256 in the license manifest.
The deployed binary file/product versions are recorded when present. The local
builder does not acquire dependencies or weights, except that the explicit
`-InstallQt` switch may install Qt. It never downloads models.

The default profile is `RealWindowsIntegration`. All configured targets are
built in Release and the complete CTest suite is run. Missing required integration
fixtures or skipped integration tests prevent a release ZIP. A compatible cache
is reused; an incompatible cache is retained and rejected. Choose a separate
`-BuildDirectory` for different dependency inputs or profiles.

To make a separate UI-only candidate, pass `-UiOnly -QtRoot <qt-directory>`,
the native SDK/toolchain parameters above, and, when needed,
`-QtLicensesRoot <license-directory>`. Do not pass ORT or model fixtures in
UI-only mode. The manual GitHub workflow offers only this mode on
Windows Server 2022 and labels it as not Windows-11-qualified. It is restricted
to the candidate branch and exact requested commit; it has no push trigger.

The workflow's optional `native_sdk_root` and `cmake_toolchain_file` inputs name
existing paths **on that runner**. Empty or incomplete inputs produce a `Blocked`
preflight report before Python package or Qt installation. The check records the
required headers, import libraries, release DLLs and upstream notice hashes; it
does not establish native qualification. This repository supplies no preseeded
Windows codec SDK. A standard hosted image without those inputs remains blocked;
the local offline recipe is the available path when the SDK is on your own PC.
The workflow consumes existing SDK files and does not build or download them.

## Official deployment references

- Qt deployment and dry-run: <https://doc.qt.io/qt-6/windows-deployment.html>
- Microsoft DLL import inspection: <https://learn.microsoft.com/en-us/cpp/build/reference/dependents>
- Microsoft PE/library headers: <https://learn.microsoft.com/en-us/cpp/build/reference/headers>
- ONNX Runtime requirements: <https://onnxruntime.ai/docs/install/>

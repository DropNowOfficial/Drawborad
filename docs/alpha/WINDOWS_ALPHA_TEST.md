# Drawborad Windows Alpha 0.1 — installation and test protocol

This is a **test build**, not a signed consumer release and **not** the full AI reconstruction pipeline. It is limited to native canvas, PNG/JPEG reference **preview**, pointer strokes, `.draw` project persistence, and worker/diagnostics smoke testing. PSD/PSB import/export, SAM automatic object segmentation, offline ONNX model management and end-to-end reconstruction are not connected to this Alpha UI.

## A. Download prebuilt ZIP (when a Windows Actions job succeeds)

1. Sign in to GitHub with access to the private [Drawborad repository](https://github.com/DropNowOfficial/Drawborad).
2. Open [Windows Alpha workflow runs](https://github.com/DropNowOfficial/Drawborad/actions/workflows/windows-alpha-portable.yml) and select the **latest successful** run for `alpha/windows-portable-20261008`.
3. In **Artifacts**, download the `Drawborad-Windows-Alpha-...` bundle containing the ZIP and `.sha256.txt`.
4. Verify the checksum with `Get-FileHash .\Drawborad-Alpha-Win11-x64-*.zip -Algorithm SHA256` and compare against the checksum text.
5. Extract the ZIP completely; run `Drawborad.exe` from the extracted `Drawborad-Alpha-Win11-x64` directory. Do **not** run as administrator. Keep the Qt DLLs and `platforms` folder alongside it. Install the **official Microsoft Visual C++ 2015–2022 x64 Redistributable** if the app reports a missing MSVC runtime. The portable packager deliberately does not redistribute Visual Studio compiler-runtime DLLs.

**Build status gate:** a source commit or workflow that fails before any step executes does **not** create a working artifact. Do not advertise a download until a Windows build succeeds and the ZIP is present.

## B. Build locally if GitHub Actions has no runner

Prerequisites: Windows 11 x64, Visual Studio 2022 with Desktop development with C++, CMake >=3.25, Python 3.9+, and the complete source ZIP of branch `alpha/windows-portable-20261008`. Windows desktop code targets **Qt 6.12.0 MSVC2022 64-bit**.

From a normal PowerShell window in the extracted source directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\Build-Alpha.ps1 -InstallQt
```

If Qt 6.12.0 is already installed, omit `-InstallQt` and optionally specify `-QtRoot "C:\Qt\6.12.0\msvc2022_64"`. The script checks the compiler outputs, uses `windeployqt`, runs a non-graphical device-diagnostics smoke test, creates a versioned Windows ZIP in `out\alpha`, and writes its SHA-256 checksum. It **does not** install drivers, change registry settings or require elevated privileges. Installing Qt via `-InstallQt` requires network access.

If MSVC is absent, install the official Visual Studio 2022 C++ workload first; do not substitute Linux binaries. A build on Linux without Qt/D3D11 is not a Windows GUI test.

## Test checklist (report each result as Pass / Fail / Not tested)

1. Unzip into a local writable folder and start `Drawborad.exe` (record any missing DLL or Qt plugin errors).
2. Select **New**, draw strokes with a mouse, save to `.draw`, close and reopen. Confirm strokes persist.
3. Select **Import PNG/JPEG reference** (a local file <=32 MiB and <=4096 pixels each side). Confirm it displays at the correct aspect ratio, follows the canvas, and can be shown/hidden with the overlay controls.
4. Draw while the overlay is visible, then change opacity and hide the overlay. The reference must **not** be baked into painted strokes.
5. Save a `.draw` project with an image overlay, reopen it, then reattach the **same-size** image. Strokes should survive; the original image is not embedded in the project.
6. Use **Check native worker**, then **Cancel worker**. It must not modify the document.
7. Double-click `Run-Alpha-Diagnostics.cmd` and retain `alpha-user-diagnostics.json`; record Windows build, GPU, exception dialogs and exact reproduction steps.

## Explicitly deferred (not valid Pass criteria for this Alpha)

- Automatic segmentation, object-by-object reviewing, ONNX/SAM 2.1 live inference.
- E03/E04/E05 reconstructed art from a source image, editable PSD export and Photoshop reopen oracle.
- Cross-app signing/installer certification, tablet pressure and long-session performance validation.

These are future acceptance gates, not capabilities of this Alpha download.

## If the GitHub job has 0 steps

The private repository's GitHub-hosted Windows runner may be blocked by owner account Actions quota, payment/budget settings or runner provisioning. A workflow with `steps=[]` cannot be fixed by changing application C++ code. The repository owner should check **GitHub Settings → Billing and licensing → Actions** and **Repository Settings → Actions → General**, then retry the workflow. The exact cause is **not yet established**.

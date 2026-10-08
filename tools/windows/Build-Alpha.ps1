# Build a portable, unsigned Windows 11 x64 Alpha test ZIP.
# No account credentials, source images, or API tokens are required by this script.
# Requires: Windows 11 x64, Visual Studio 2022 C++ workload, CMake, Python 3.
[CmdletBinding()]
param(
    [string]$QtRoot = '',
    [switch]$InstallQt,
    [switch]$SkipDiagnostics
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Run-Checked {
    param([string]$Executable, [string[]]$Arguments)
    Write-Host "[alpha-build] $Executable $($Arguments -join ' ')"
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command exited with code $LASTEXITCODE : $Executable"
    }
}

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not (Test-Path (Join-Path $repo 'CMakeLists.txt'))) {
    throw 'Run this script from a complete Drawborad source checkout or branch ZIP.'
}
if (-not [System.Environment]::Is64BitOperatingSystem -or [System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw 'Windows 11 x64 is required to build the native desktop Alpha.'
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake 3.25+ is required. Install CMake and run this script again.'
}
if (-not $QtRoot) { $QtRoot = $env:QT_ROOT_DIR }
if (-not $QtRoot) { $QtRoot = 'C:\Qt\6.12.0\msvc2022_64' }

if (-not (Test-Path (Join-Path $QtRoot 'bin\windeployqt.exe'))) {
    if (-not $InstallQt) {
        throw "Qt 6.12.0 MSVC2022 x64 not found at '$QtRoot'. Pass -QtRoot 'C:\Qt\6.12.0\msvc2022_64' or add -InstallQt."
    }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) { $python = $py.Source } else {
        $py = Get-Command python -ErrorAction SilentlyContinue
        if (-not $py) { throw 'Python 3 is required to install Qt with aqtinstall.' }
        $python = $py.Source
    }
    $qtInstallRoot = Join-Path $repo '.alpha-qt'
    Run-Checked -Executable $python -Arguments @('-m','pip','install','--user','aqtinstall>=3.3,<4')
    Run-Checked -Executable $python -Arguments @('-m','aqt','install-qt','windows','desktop','6.12.0','win64_msvc2022_64','-O',$qtInstallRoot)
    $QtRoot = Join-Path $qtInstallRoot '6.12.0\msvc2022_64'
}
$QtRoot = [System.IO.Path]::GetFullPath($QtRoot)
if (-not (Test-Path (Join-Path $QtRoot 'bin\windeployqt.exe'))) {
    throw "Qt deployment tool missing after installation: $QtRoot"
}

$build = Join-Path $repo 'out\build\win-alpha-portable'
$artifacts = Join-Path $repo 'out\alpha'
$folder = Join-Path $artifacts 'Drawborad-Alpha-Win11-x64'
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null

Run-Checked -Executable 'cmake' -Arguments @('-S',$repo,'-B',$build,'-G','Visual Studio 17 2022','-A','x64',
    "-DCMAKE_PREFIX_PATH=$QtRoot",'-DDRAWBORAD_DESKTOP=ON',
    '-DDRAWBORAD_QUALIFY=OFF','-DDRAWBORAD_WINDOWS_VALIDATION=ON')
Run-Checked -Executable 'cmake' -Arguments @('--build',$build,'--config','Release','--target','Drawborad','DrawboradWorker','--parallel','2')

$bin = Join-Path $build 'Release'
$exe = Join-Path $bin 'Drawborad.exe'
$worker = Join-Path $bin 'DrawboradWorker.exe'
$token = Join-Path $bin 'drawborad_build_token.txt'
foreach ($needed in @($exe,$worker,$token)) {
    if (-not (Test-Path $needed)) { throw "Build output missing: $needed" }
}
if (Test-Path $folder) { Remove-Item -LiteralPath $folder -Recurse -Force }
New-Item -ItemType Directory -Path $folder | Out-Null
Copy-Item -LiteralPath $exe,$worker,$token -Destination $folder
Run-Checked -Executable (Join-Path $QtRoot 'bin\windeployqt.exe') -Arguments @('--release','--no-compiler-runtime','--qmldir',(Join-Path $repo 'ui'),'--dir',$folder,(Join-Path $folder 'Drawborad.exe'))

foreach ($needed in @('Drawborad.exe','DrawboradWorker.exe','Qt6Core.dll','Qt6Qml.dll','Qt6Quick.dll','platforms\qwindows.dll','drawborad_build_token.txt')) {
    if (-not (Test-Path (Join-Path $folder $needed))) {
        throw "Portable runtime dependency missing: $needed"
    }
}

$commit = $env:GITHUB_SHA
if (-not $commit -and (Test-Path (Join-Path $repo '.git')) -and (Get-Command git -ErrorAction SilentlyContinue)) {
    $commit = (& git -C $repo rev-parse --verify HEAD 2>$null)
}
if (-not $commit) { $commit = 'local' }
if ($commit.Length -gt 8) { $commit = $commit.Substring(0,8) }
$readme = @"
DRAWBORAD WINDOWS ALPHA 0.1 (UNSIGNED TEST BUILD)
Build: $commit

Start: double-click Drawborad.exe. Do not run as administrator.
1. New -> draw two strokes -> Save to a .draw file.
2. Close and reopen -> Open project -> confirm strokes survived.
3. Import PNG/JPEG reference -> draw with the visible overlay -> adjust opacity.
4. Save .draw, reopen and import the SAME SIZE reference to reattach overlay.
5. Check native worker and verify Cancel works without changing your paint.

ALPHA LIMITS:
- Reference image preview only (PNG/JPEG up to 32 MB and 4096 pixels per side).
- The .draw project saves strokes but does NOT embed the imported source image.
- SAM/ONNX auto-segmentation, AI high-fidelity painting, full PSD export and
  Photoshop interoperability are NOT exposed in this Alpha UI.
- Unofficial unsigned pre-release. Keep your original files and save test copies.
- Qt deployed via windeployqt. Requires the official Microsoft Visual C++ 2015-2022 x64 Redistributable.
Do not copy compiler runtime DLLs from a developer machine.

Report an issue: record Windows version, GPU, exact operation, error message,
and whether the .draw file was newly created or reopened.
"@
Set-Content -LiteralPath (Join-Path $folder 'ALPHA_README.txt') -Value $readme -Encoding UTF8
$diagRunner = @'
@echo off
"%~dp0Drawborad.exe" --diagnostics "%~dp0alpha-user-diagnostics.json"
if errorlevel 1 (echo Diagnostics failed. & exit /b 1)
echo Diagnostics written to alpha-user-diagnostics.json
pause
'@
Set-Content -LiteralPath (Join-Path $folder 'Run-Alpha-Diagnostics.cmd') -Value $diagRunner -Encoding ascii

if (-not $SkipDiagnostics) {
    Run-Checked -Executable (Join-Path $folder 'Drawborad.exe') -Arguments @('--diagnostics',(Join-Path $folder 'alpha-diagnostics.json'))
    if (-not (Test-Path (Join-Path $folder 'alpha-diagnostics.json'))) {
        throw 'Diagnostics smoke test did not create an output report.'
    }
}

$zip = Join-Path $artifacts "Drawborad-Alpha-Win11-x64-$commit.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path $folder -DestinationPath $zip -CompressionLevel Optimal -Force
if (-not (Test-Path $zip)) { throw "Packaging failed: $zip" }
$sum = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumFile = Join-Path $artifacts "Drawborad-Alpha-Win11-x64-$commit.sha256.txt"
Set-Content -LiteralPath $checksumFile -Value "$sum  $([IO.Path]::GetFileName($zip))" -Encoding ascii
Write-Host "ALPHA_ZIP=$zip"
Write-Host "ALPHA_SHA256=$sum"
Write-Host "ALPHA_CHECKSUM_FILE=$checksumFile"

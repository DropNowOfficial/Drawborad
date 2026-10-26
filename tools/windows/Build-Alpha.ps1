# Build a portable, unsigned RC1 candidate from a clean, exact source revision.
# Default: Windows 11 x64 / Qt 6.12 / MSVC 2022 / CPU ONNX Runtime integration.
# -UiOnly is an explicitly unqualified alternative; models are never downloaded.
[CmdletBinding()]
param(
    [string]$QtRoot = '',
    [string]$OnnxRuntimeRoot = '',
    [string]$CoffeeImage = '',
    [string]$CoffeePackage = '',
    [string]$CoffeeReference = '',
    [string]$CoffeeAutoReference = '',
    [string]$CoffeeFullSceneReference = '',
    [string]$QtLicensesRoot = '',
    [string]$CMakeToolchainFile = '',
    [string]$VcpkgInstalledDir = '',
    [string]$BuildDirectory = '',
    [string]$OutputDirectory = '',
    [string]$ExpectedCommit = '',
    [switch]$InstallQt,
    [switch]$UiOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Json {
    param([string]$Path, $Value)
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 100) + [Environment]::NewLine,
        (New-Object Text.UTF8Encoding($false)))
}
function Invoke-Logged {
    param([string]$Executable, [string[]]$Arguments, [string]$Log, [switch]$AllowFailure)
    Write-Host "[alpha-build] $Executable $($Arguments -join ' ')"
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | ForEach-Object { $_.ToString() } |
            Tee-Object -FilePath $Log | ForEach-Object { Write-Host $_ }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($code -ne 0 -and -not $AllowFailure) {
        throw "Command exited $code : $Executable. Evidence: $Log"
    }
    return [int]$code
}
function Get-ToolText {
    param([string]$Executable, [string[]]$Arguments)
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $Executable @Arguments 2>&1)
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($code -ne 0) { throw "$Executable failed ($code): $($lines -join ' ')" }
    return ($lines -join [Environment]::NewLine).Trim()
}
function Invoke-DeploymentPlan {
    param([string]$Executable, [string[]]$Arguments, [string]$Output, [string]$ErrorLog)
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $Executable @Arguments 2> $ErrorLog)
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    [IO.File]::WriteAllText($Output, ($lines -join [Environment]::NewLine),
        (New-Object Text.UTF8Encoding($false)))
    if ($code -ne 0) { throw "windeployqt dry-run failed ($code). Evidence: $ErrorLog" }
}
function Resolve-Input {
    param([string]$Value, [string]$Name, [string]$Type = 'Leaf')
    if (-not $Value -or -not (Test-Path -LiteralPath $Value -PathType $Type)) {
        throw "Required $Name is missing: '$Value'. Supply an existing offline input."
    }
    return (Resolve-Path -LiteralPath $Value).ProviderPath
}
function Copy-LicenseTree {
    param([string]$Source, [string]$Destination)
    $files = @(Get-ChildItem -LiteralPath $Source -File -Recurse)
    if ($files.Count -eq 0) { throw "License/notice directory is empty: $Source" }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($Source.TrimEnd('\','/').Length).TrimStart('\','/')
        $target = Join-Path $Destination $relative
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$helper = Join-Path $PSScriptRoot 'alpha_release.py'
$evidence = $null
$stageName = 'preflight'
$temporary = $null
$zip = $null
$zipCreated = $false
$completed = $false
$originalPath = $env:PATH
$originalVsLang = $env:VSLANG
try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
        -not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess -or
        $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
        throw 'A native x64 Windows process is required. Linux results cannot qualify this package.'
    }
    $os = Get-CimInstance -ClassName Win32_OperatingSystem
    $isWindows11 = [int]$os.ProductType -eq 1 -and [int]$os.BuildNumber -ge 22000
    if (-not $UiOnly -and -not $isWindows11) {
        throw 'Integrated packaging requires an actual Windows 11 workstation. Use -UiOnly for an explicitly unqualified Windows Server UI candidate.'
    }
    foreach ($tool in @('git','cmake','ctest','python')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool missing: $tool" }
    }
    $git = (Get-Command git).Source
    $cmake = (Get-Command cmake).Source
    $ctest = (Get-Command ctest).Source
    $python = (Get-Command python).Source
    $commit = Get-ToolText $git @('-C',$repo,'rev-parse','--verify','HEAD')
    $sourceTree = Get-ToolText $git @('-C',$repo,'rev-parse','HEAD^{tree}')
    $branch = Get-ToolText $git @('-C',$repo,'rev-parse','--abbrev-ref','HEAD')
    if ($commit -notmatch '^[0-9a-f]{40}$') { throw 'An exact Git source commit is required.' }
    if (Get-ToolText $git @('-C',$repo,'status','--porcelain','--untracked-files=normal')) {
        throw 'Release ZIPs require a clean source checkout. Commit the intended candidate changes before building.'
    }
    if ($ExpectedCommit -and ($ExpectedCommit -notmatch '^[0-9a-fA-F]{40}$' -or $ExpectedCommit -ne $commit)) {
        throw "Expected source SHA '$ExpectedCommit' does not match '$commit'."
    }
    if ($env:GITHUB_SHA -and $env:GITHUB_SHA -ne $commit) { throw 'GitHub SHA differs from the checked-out source.' }

    $mode = if ($UiOnly) { 'UIOnly' } else { 'Integrated-CPU' }
    $profile = if ($UiOnly) { 'CorePortable' } else { 'RealWindowsIntegration' }
    $runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'out\alpha' }
    $artifacts = [IO.Path]::GetFullPath($OutputDirectory)
    $evidence = Join-Path $artifacts "evidence\$mode-$runId"
    New-Item -ItemType Directory -Path $evidence -Force | Out-Null
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ("drawborad-alpha-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $packageName = "Drawborad-Alpha-Win11-x64-$mode"
    $folder = Join-Path $temporary $packageName
    New-Item -ItemType Directory -Path $folder | Out-Null
    if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repo "out\build\win-alpha-rc1-$mode" }
    $build = [IO.Path]::GetFullPath($BuildDirectory)
    $baseName = "$packageName-$commit-$runId"
    $zip = Join-Path $artifacts "$baseName.zip"
    if (Test-Path -LiteralPath $zip) { throw "Refusing to overwrite existing artifact: $zip" }
    $env:VSLANG = '1033'
    $cmakeVersion = Get-ToolText $cmake @('--version')
    if ($cmakeVersion -notmatch 'cmake version (\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'3.25.0') {
        throw 'CMake 3.25 or later is required.'
    }
    Invoke-Logged $python @('-c','import sys, yaml; assert sys.version_info >= (3,9); print(sys.version)') (Join-Path $evidence 'python.log') | Out-Null

    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vswhere = Resolve-Input $vswhere 'Visual Studio locator'
    $vs = Get-ToolText $vswhere @('-latest','-products','*','-version','[17.0,18.0)','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath')
    if (-not $vs) { throw 'Visual Studio 2022 with the x64 C++ workload is required.' }
    $msvc = Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $msvc) { throw 'MSVC 2022 tools were not found.' }
    $dumpbin = Resolve-Input (Join-Path $msvc.FullName 'bin\Hostx64\x64\dumpbin.exe') 'x64 DUMPBIN'
    $compiler = Resolve-Input (Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe') 'x64 MSVC compiler'

    if (-not $QtRoot) { $QtRoot = $env:QT_ROOT_DIR }
    if (-not $QtRoot) { $QtRoot = 'C:\Qt\6.12.0\msvc2022_64' }
    if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'bin\windeployqt.exe'))) {
        if (-not $InstallQt) { throw "Qt 6.12 MSVC2022 x64 is missing at '$QtRoot'. Pass -QtRoot or explicitly use -InstallQt." }
        $qtInstall = Join-Path $repo 'out\qt-sdk'
        Invoke-Logged $python @('-m','pip','install','--user','aqtinstall>=3.3,<4') (Join-Path $evidence 'install-aqt.log') | Out-Null
        Invoke-Logged $python @('-m','aqt','install-qt','windows','desktop','6.12.0','win64_msvc2022_64','-O',$qtInstall) (Join-Path $evidence 'install-qt.log') | Out-Null
        $QtRoot = Join-Path $qtInstall '6.12.0\msvc2022_64'
    }
    $QtRoot = Resolve-Input $QtRoot 'Qt root' 'Container'
    $windeployqt = Resolve-Input (Join-Path $QtRoot 'bin\windeployqt.exe') 'official windeployqt'
    $qmake = Resolve-Input (Join-Path $QtRoot 'bin\qmake.exe') 'Qt qmake'
    $qtVersion = Get-ToolText $qmake @('-query','QT_VERSION')
    $qtSpec = Get-ToolText $qmake @('-query','QMAKE_XSPEC')
    if ($qtVersion -notmatch '^6\.12\.\d+$' -or $qtSpec -ne 'win32-msvc') {
        throw "Qt 6.12 MSVC is required; found version=$qtVersion spec=$qtSpec"
    }
    Invoke-Logged $python @($helper,'machine','--binary',(Join-Path $QtRoot 'bin\Qt6Core.dll'),'--dumpbin',$dumpbin,'--output',(Join-Path $evidence 'qt-machine.json')) (Join-Path $evidence 'qt-machine.log') | Out-Null
    if (-not $QtLicensesRoot) {
        foreach ($candidate in @((Join-Path $QtRoot 'licenses'),(Join-Path $QtRoot 'LICENSES'),(Join-Path $QtRoot '..\..\Licenses'))) {
            if (Test-Path -LiteralPath $candidate -PathType Container) { $QtLicensesRoot = $candidate; break }
        }
    }
    $QtLicensesRoot = Resolve-Input $QtLicensesRoot 'matching Qt SDK license/notice directory (-QtLicensesRoot)' 'Container'

    $ortInfo = [ordered]@{ enabled = $false; provider = 'not built'; models_bundled = $false }
    if ($UiOnly) {
        if ($OnnxRuntimeRoot -or $CoffeeImage -or $CoffeePackage -or $CoffeeReference -or $CoffeeAutoReference -or $CoffeeFullSceneReference) {
            throw '-UiOnly cannot be combined with runtime/model fixture inputs. Build a separate Integrated-CPU candidate.'
        }
    } else {
        $OnnxRuntimeRoot = Resolve-Input $OnnxRuntimeRoot 'CPU ONNX Runtime SDK (-OnnxRuntimeRoot)' 'Container'
        foreach ($relative in @('include\onnxruntime_c_api.h','include\onnxruntime_cxx_api.h','include\onnxruntime_cxx_inline.h','include\onnxruntime_float16.h','lib\onnxruntime.lib','lib\onnxruntime.dll','LICENSE','ThirdPartyNotices.txt')) {
            Resolve-Input (Join-Path $OnnxRuntimeRoot $relative) "ORT $relative" | Out-Null
        }
        $gpuProviders = @(Get-ChildItem -LiteralPath (Join-Path $OnnxRuntimeRoot 'lib') -File |
            Where-Object { $_.Name -match 'onnxruntime_providers_(cuda|tensorrt|rocm|dml|qnn|openvino)' })
        if ($gpuProviders.Count -ne 0) { throw 'Use the CPU ONNX Runtime SDK; accelerator provider DLLs were found.' }
        foreach ($binary in @('onnxruntime.lib','onnxruntime.dll')) {
            Invoke-Logged $python @($helper,'machine','--binary',(Join-Path $OnnxRuntimeRoot "lib\$binary"),'--dumpbin',$dumpbin,'--output',(Join-Path $evidence "$binary-machine.json")) (Join-Path $evidence "$binary-machine.log") | Out-Null
        }
        $CoffeeImage = Resolve-Input $CoffeeImage 'exact coffee input image (-CoffeeImage)'
        $CoffeePackage = Resolve-Input $CoffeePackage 'verified offline model package (-CoffeePackage)' 'Container'
        $CoffeeReference = Resolve-Input $CoffeeReference 'official prompted coffee reference directory (-CoffeeReference)' 'Container'
        $CoffeeAutoReference = Resolve-Input $CoffeeAutoReference 'official automatic coffee reference directory (-CoffeeAutoReference)' 'Container'
        $CoffeeFullSceneReference = Resolve-Input $CoffeeFullSceneReference 'full-scene reference directory (-CoffeeFullSceneReference)' 'Container'
        $ortDll = Join-Path $OnnxRuntimeRoot 'lib\onnxruntime.dll'
        $ortInfo = [ordered]@{
            enabled = $true; provider = 'CPU'; root = $OnnxRuntimeRoot
            file_version = (Get-Item -LiteralPath $ortDll).VersionInfo.FileVersion
            dll_sha256 = (Get-FileHash -LiteralPath $ortDll -Algorithm SHA256).Hash.ToLowerInvariant()
            import_library_sha256 = (Get-FileHash -LiteralPath (Join-Path $OnnxRuntimeRoot 'lib\onnxruntime.lib') -Algorithm SHA256).Hash.ToLowerInvariant()
            models_bundled = $false
        }
    }
    $nativeBin = ''
    $nativeStatusPath = ''
    if ($CMakeToolchainFile -or $VcpkgInstalledDir) {
        $CMakeToolchainFile = Resolve-Input $CMakeToolchainFile 'offline CMake toolchain (-CMakeToolchainFile)'
        $VcpkgInstalledDir = Resolve-Input $VcpkgInstalledDir 'installed vcpkg dependencies (-VcpkgInstalledDir)' 'Container'
        $nativeBin = Resolve-Input (Join-Path $VcpkgInstalledDir 'x64-windows\bin') 'x64 release dependency DLL directory' 'Container'
        $nativeStatusPath = Resolve-Input (Join-Path $VcpkgInstalledDir 'vcpkg\status') 'installed vcpkg package version/triplet ledger'
        if ((Get-Item -LiteralPath $nativeStatusPath).Length -eq 0) { throw 'The installed vcpkg package version/triplet ledger is empty.' }
        Resolve-Input (Join-Path $VcpkgInstalledDir 'x64-windows\share') 'vcpkg copyright notices' 'Container' | Out-Null
    }
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $originalPath
    if ($OnnxRuntimeRoot) { $env:PATH = (Join-Path $OnnxRuntimeRoot 'lib') + ';' + $env:PATH }
    if ($nativeBin) { $env:PATH = $nativeBin + ';' + $env:PATH }

    $expectedCache = [ordered]@{
        CMAKE_HOME_DIRECTORY = $repo; CMAKE_GENERATOR = 'Visual Studio 17 2022'
        CMAKE_GENERATOR_PLATFORM = 'x64'; CMAKE_GENERATOR_TOOLSET = 'v143'
        CMAKE_GENERATOR_INSTANCE = $vs; CMAKE_PREFIX_PATH = $QtRoot
        DRAWBORAD_DESKTOP = 'ON'; DRAWBORAD_QUALIFY = 'OFF'; DRAWBORAD_WINDOWS_VALIDATION = 'ON'; DRAWBORAD_SANITIZE = 'OFF'
        DRAWBORAD_TEST_PROFILE = $profile; DRAWBORAD_ONNXRUNTIME_ROOT = $OnnxRuntimeRoot
        DRAWBORAD_TEST_COFFEE_IMAGE = $CoffeeImage; DRAWBORAD_TEST_COFFEE_PACKAGE = $CoffeePackage
        DRAWBORAD_TEST_COFFEE_REFERENCE = $CoffeeReference; DRAWBORAD_TEST_COFFEE_AUTO_REFERENCE = $CoffeeAutoReference
        DRAWBORAD_TEST_FULL_SCENE_REFERENCE = $CoffeeFullSceneReference
        DRAWBORAD_TEST_ONNX_MODEL = ''; DRAWBORAD_TEST_SAM_ENCODER = ''; DRAWBORAD_TEST_SAM_DECODER = ''
        DRAWBORAD_TEST_PROBE_ENCODER = ''; DRAWBORAD_TEST_PROBE_DECODER = ''
        CMAKE_TOOLCHAIN_FILE = $CMakeToolchainFile; VCPKG_INSTALLED_DIR = $VcpkgInstalledDir
        Python3_EXECUTABLE = $python
    }
    $cacheRequest = Join-Path $evidence 'cache-request.json'
    Write-Json $cacheRequest $expectedCache
    Invoke-Logged $python @($helper,'check-cache','--cache',(Join-Path $build 'CMakeCache.txt'),'--input',$cacheRequest,'--output',(Join-Path $evidence 'cache-check.json')) (Join-Path $evidence 'cache-check.log') | Out-Null
    $configure = @('-S',$repo,'-B',$build,'-G','Visual Studio 17 2022','-A','x64','-T','v143')
    foreach ($key in $expectedCache.Keys) {
        if ($key -notin @('CMAKE_HOME_DIRECTORY','CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM','CMAKE_GENERATOR_TOOLSET')) {
            $configure += "-D$key=$($expectedCache[$key])"
        }
    }
    if ($VcpkgInstalledDir) { $configure += '-DVCPKG_TARGET_TRIPLET=x64-windows' }
    $stageName = 'configure'
    Invoke-Logged $cmake $configure (Join-Path $evidence 'configure.log') | Out-Null
    $stageName = 'build-all-release'
    # No target filter: build the application, worker, codec and ALL registered test binaries.
    Invoke-Logged $cmake @('--build',$build,'--config','Release','--parallel','2') (Join-Path $evidence 'build.log') | Out-Null
    $stageName = 'complete-ctest-suite'
    $suiteReport = Join-Path $evidence 'suite-results.json'
    $suiteCode = Invoke-Logged $python @((Join-Path $repo 'tools\qa\release_report.py'),'--build-dir',$build,'--configuration','Release','--run-ctest','--ctest-executable',$ctest,'--junit',(Join-Path $evidence 'ctest-results.xml'),'--output',$suiteReport) (Join-Path $evidence 'ctest.log') -AllowFailure
    if (-not (Test-Path -LiteralPath $suiteReport)) { throw "CTest report was not created (exit $suiteCode)." }
    $suite = Get-Content -LiteralPath $suiteReport -Raw | ConvertFrom-Json
    if ($suiteCode -ne 0 -or $suite.status -ne 'Passed') { throw "Automated suite gate: $($suite.status). No release ZIP will be produced." }
    if ($suite.git_sha -ne $commit -or $suite.source_tree -ne $sourceTree -or $suite.source_dirty -or $suite.profile -ne $profile -or $suite.configuration -ne 'Release') {
        throw 'Test receipt does not identify this clean source, profile and configuration. Keep outputs under ignored out/ or outside the checkout.'
    }
    if (-not $UiOnly -and [int]$suite.counts.skipped -ne 0) { throw 'Integrated packaging prohibits skipped integration tests.' }
    foreach ($file in @('TEST_INVENTORY.json','TEST_EXECUTION.json')) {
        Copy-Item -LiteralPath (Resolve-Input (Join-Path $build $file) $file) -Destination $evidence
    }
    $lastTest = Join-Path $build 'Testing\Temporary\LastTest.log'
    if (Test-Path -LiteralPath $lastTest) { Copy-Item -LiteralPath $lastTest -Destination $evidence }
    $bin = Join-Path $build 'Release'
    foreach ($name in @('Drawborad.exe','DrawboradWorker.exe','DrawboradCodec.exe','drawborad_build_token.txt')) {
        Copy-Item -LiteralPath (Resolve-Input (Join-Path $bin $name) $name) -Destination $folder
    }
    $expectedToken = Resolve-Input (Join-Path $build 'drawborad_expected_build_token.txt') 'configured build token'
    if ((Get-Content -LiteralPath $expectedToken -Raw).Trim() -ne (Get-Content -LiteralPath (Join-Path $folder 'drawborad_build_token.txt') -Raw).Trim()) {
        throw 'Build freshness token mismatch.'
    }
    if ($OnnxRuntimeRoot) { Copy-Item -LiteralPath (Join-Path $OnnxRuntimeRoot 'lib\onnxruntime.dll') -Destination $folder }
    if ($nativeBin) {
        Get-ChildItem -LiteralPath $nativeBin -File -Filter '*.dll' | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $folder
        }
    }

    $stageName = 'deployment-plan-and-audit'
    $deploy = @('--release','--no-compiler-runtime','--qmldir',(Join-Path $repo 'ui'),'--dir',$folder,(Join-Path $folder 'Drawborad.exe'))
    $plan = Join-Path $evidence 'windeployqt-plan.json'
    Invoke-DeploymentPlan $windeployqt (@('--dry-run','--json') + $deploy) $plan (Join-Path $evidence 'windeployqt-plan-stderr.log')
    Invoke-Logged $python @($helper,'audit','--root',$folder,'--input',$plan,'--dumpbin',$dumpbin,'--system-dir',(Join-Path $env:SystemRoot 'System32'),'--log-dir',(Join-Path $evidence 'planned-imports'),'--output',(Join-Path $evidence 'DLL_AUDIT_PLANNED.json')) (Join-Path $evidence 'planned-audit.log') | Out-Null
    $stageName = 'deployment-and-audit'
    Invoke-Logged $windeployqt $deploy (Join-Path $evidence 'windeployqt.log') | Out-Null
    foreach ($name in @('Qt6Core.dll','Qt6Gui.dll','Qt6Qml.dll','Qt6Quick.dll','platforms\qwindows.dll')) {
        Resolve-Input (Join-Path $folder $name) "deployed $name" | Out-Null
    }
    $postAuditPath = Join-Path $evidence 'DLL_AUDIT_DEPLOYED.json'
    Invoke-Logged $python @($helper,'audit','--root',$folder,'--dumpbin',$dumpbin,'--system-dir',(Join-Path $env:SystemRoot 'System32'),'--log-dir',(Join-Path $evidence 'deployed-imports'),'--output',$postAuditPath) (Join-Path $evidence 'deployed-audit.log') | Out-Null

    $stageName = 'licenses-and-package-records'
    $licenses = Join-Path $folder 'licenses'
    Copy-LicenseTree $QtLicensesRoot (Join-Path $licenses 'Qt')
    if ($OnnxRuntimeRoot) {
        New-Item -ItemType Directory -Path (Join-Path $licenses 'ONNXRuntime') -Force | Out-Null
        foreach ($name in @('LICENSE','ThirdPartyNotices.txt','Privacy.md')) {
            $source = Join-Path $OnnxRuntimeRoot $name
            if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination (Join-Path $licenses 'ONNXRuntime') }
        }
    }
    $nativeVersionLedger = $null
    if ($VcpkgInstalledDir) {
        $share = Join-Path $VcpkgInstalledDir 'x64-windows\share'
        $notices = @(Get-ChildItem -LiteralPath $share -File -Recurse -Filter copyright)
        if ($notices.Count -eq 0) { throw 'Native dependency copyright notices were not found.' }
        foreach ($notice in $notices) {
            $destination = Join-Path $licenses ('native\' + $notice.Directory.Name)
            New-Item -ItemType Directory -Path $destination -Force | Out-Null
            Copy-Item -LiteralPath $notice.FullName -Destination $destination
        }
        Copy-Item -LiteralPath $nativeStatusPath -Destination (Join-Path $licenses 'native\vcpkg-status.txt')
        $nativeVersionLedger = [ordered]@{
            format = 'vcpkg installed status; package versions, features and triplets'
            path = 'licenses/native/vcpkg-status.txt'
            sha256 = (Get-FileHash -LiteralPath $nativeStatusPath -Algorithm SHA256).Hash.ToLowerInvariant()
            contents = Get-Content -LiteralPath $nativeStatusPath -Raw -Encoding UTF8
        }
    }
    $licenseManifest = @(Get-ChildItem -LiteralPath $licenses -File -Recurse | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($folder.Length + 1); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    Write-Json (Join-Path $licenses 'MANIFEST.json') ([ordered]@{
        qt_source = $QtLicensesRoot; ort_source = $OnnxRuntimeRoot; native_source = $VcpkgInstalledDir
        files = $licenseManifest
        note = 'Actual supplied SDK notices; no license terms have been invented. Microsoft compiler runtime DLLs are not bundled.'
    })
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'PACKAGE_README.md') -Destination (Join-Path $folder 'README.md')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'KNOWN_LIMITATIONS.md') -Destination $folder
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Run-Diagnostics.cmd') -Destination $folder
    $inventory = Get-Content -LiteralPath (Join-Path $build 'TEST_INVENTORY.json') -Raw | ConvertFrom-Json
    $deployedAudit = Get-Content -LiteralPath $postAuditPath -Raw | ConvertFrom-Json
    $binaryVersions = @($deployedAudit.files | ForEach-Object {
        $version = (Get-Item -LiteralPath (Join-Path $folder $_.target)).VersionInfo
        [ordered]@{ path = $_.target; sha256 = $_.sha256; file_version = $version.FileVersion; product_version = $version.ProductVersion }
    })
    Write-Json (Join-Path $folder 'BUILD_INFO.json') ([ordered]@{
        schema = 1; git_sha = $commit; source_tree = $sourceTree; source_branch = $branch; source_clean = $true
        software_version = $inventory.dependencies.PROJECT_VERSION; release_candidate = 'Windows Integrated Alpha RC1'
        created_utc = [DateTime]::UtcNow.ToString('o'); configuration = 'Release'; architecture = 'x64'
        package_mode = $mode; test_profile = $profile; target_os = 'Windows 11 x64'
        host = [ordered]@{ caption = $os.Caption; build = $os.BuildNumber; product_type = $os.ProductType; windows11 = $isWindows11 }
        windows11_qualification = $(if ($isWindows11) { 'Host eligible; manual acceptance remains Blocked' } else { 'Blocked: UI-only build on a non-Windows-11 host' })
        compiler = [ordered]@{ generator = 'Visual Studio 17 2022'; toolset = 'v143'; executable = $inventory.dependencies.CMAKE_CXX_COMPILER; version = $inventory.dependencies.CMAKE_CXX_COMPILER_VERSION; inspection_tool_version = (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion; installation = $vs }
        cmake = $cmakeVersion; qt = [ordered]@{ version = $qtVersion; spec = $qtSpec; root = $QtRoot }
        onnxruntime = $ortInfo; native_dependency_root = $VcpkgInstalledDir; build_directory = $build
        native_dependency_status = $nativeVersionLedger; deployed_binaries = $binaryVersions
        runtime_prerequisite = 'Official Microsoft Visual C++ 2015-2022 x64 Redistributable, same or newer than this MSVC toolset'
        build_token_sha256 = (Get-FileHash -LiteralPath (Join-Path $folder 'drawborad_build_token.txt') -Algorithm SHA256).Hash.ToLowerInvariant()
        test_inventory = $inventory
    })

    $stageName = 'extracted-candidate-smoke'
    $candidate = Join-Path $temporary 'candidate.zip'
    Compress-Archive -LiteralPath $folder -DestinationPath $candidate -CompressionLevel Optimal
    $firstExtract = Join-Path $temporary 'candidate-extracted'
    Expand-Archive -LiteralPath $candidate -DestinationPath $firstExtract
    $nativeWorkerProbe = Resolve-Input (Join-Path $build 'tests\Release\worker_smoke.exe') 'native worker transport test'
    $smokeArguments = @($helper,'smoke','--root',(Join-Path $firstExtract $packageName),'--log-dir',(Join-Path $evidence 'candidate-smoke'),'--worker-probe',(Join-Path $repo 'tests\jobs\test_worker_process.py'),'--native-worker-probe',$nativeWorkerProbe,'--expected-token',$expectedToken,'--output',(Join-Path $evidence 'CANDIDATE_SMOKE.json'))
    Invoke-Logged $python $smokeArguments (Join-Path $evidence 'candidate-smoke.log') | Out-Null
    $candidateSmoke = Get-Content -LiteralPath (Join-Path $evidence 'CANDIDATE_SMOKE.json') -Raw | ConvertFrom-Json
    $suite | Add-Member -NotePropertyName packaging -NotePropertyValue ([ordered]@{
        mode = $mode; planned_dependency_audit = 'Passed'; deployed_dependency_audit = 'Passed'
        extracted_candidate_smoke = $candidateSmoke
        final_zip_smoke = 'Recorded separately beside the immutable ZIP after final extraction'
        runtime_alpha = 'Blocked: startup/transport checks do not establish real workflow acceptance'
    }) -Force
    Write-Json (Join-Path $folder 'TEST_RESULTS.json') $suite
    Copy-Item -LiteralPath $evidence -Destination (Join-Path $folder 'evidence') -Recurse

    $stageName = 'final-zip-verification'
    Compress-Archive -LiteralPath $folder -DestinationPath $zip -CompressionLevel Optimal
    $zipCreated = $true
    $finalExtract = Join-Path $temporary 'final-extracted'
    Expand-Archive -LiteralPath $zip -DestinationPath $finalExtract
    $finalRoot = Join-Path $finalExtract $packageName
    foreach ($binary in $deployedAudit.files) {
        $actual = (Get-FileHash -LiteralPath (Join-Path $finalRoot $binary.target) -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $binary.sha256) { throw "ZIP binary differs from audited file: $($binary.target)" }
    }
    $finalSmoke = Join-Path $artifacts "$baseName.smoke.json"
    Invoke-Logged $python @($helper,'smoke','--root',$finalRoot,'--log-dir',(Join-Path $evidence 'final-smoke'),'--worker-probe',(Join-Path $repo 'tests\jobs\test_worker_process.py'),'--native-worker-probe',$nativeWorkerProbe,'--expected-token',$expectedToken,'--output',$finalSmoke) (Join-Path $evidence 'final-smoke.log') | Out-Null
    $sum = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksum = Join-Path $artifacts "$baseName.sha256.txt"
    Set-Content -LiteralPath $checksum -Value "$sum  $([IO.Path]::GetFileName($zip))" -Encoding ascii
    $completed = $true
    Write-Host "ALPHA_ZIP=$zip"
    Write-Host "ALPHA_SHA256=$sum"
    Write-Host "ALPHA_CHECKSUM_FILE=$checksum"
    Write-Host "ALPHA_EVIDENCE=$evidence"
    Write-Host 'Runtime Alpha acceptance remains Blocked until the required real workflow and manual evidence is reviewed.'
} catch {
    if ($evidence) {
        Write-Json (Join-Path $evidence 'FAILURE.json') ([ordered]@{
            status = 'Failed'; stage = $stageName; error = $_.Exception.Message
            runtime_alpha = 'Blocked'; created_utc = [DateTime]::UtcNow.ToString('o')
        })
        Write-Host "Failure evidence preserved at $evidence"
    }
    if ($zipCreated -and $zip -and (Test-Path -LiteralPath $zip)) { Remove-Item -LiteralPath $zip -Force }
    throw
} finally {
    $env:PATH = $originalPath
    $env:VSLANG = $originalVsLang
    # Only this invocation's private staging/extraction directory is removed.
    # Build caches and evidence directories are retained on success and failure.
    if ($temporary -and (Test-Path -LiteralPath $temporary)) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}

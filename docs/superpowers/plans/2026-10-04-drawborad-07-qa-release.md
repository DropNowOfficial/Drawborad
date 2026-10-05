# 独立验收、压力与安全测试、离线Windows交付 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 以真实、可追溯的证据判定Windows产品可交付，而不是用计划、截图或模拟时长填测试成绩。
**Architecture:** 早期固定合法输入/独立oracle；单测、完整链、真机性能、长测、fuzz、离线安装分别报告。最终门禁拒绝缺报告、Blocked、旧build、无授权或数据丢失问题。
**Tech Stack:** CTest / QtTest / PowerShell / Python QA / Windows trace / Photoshop independent fixtures / NSIS。
**Spec:** [approved design](../specs/2026-10-04-drawborad-win11-design.md)，基线`f3a92378ccbd8d9145556f5019642f134b362fbb`。

## Global Constraints

适用[总计划](2026-10-04-drawborad-implementation-plan.md)、[契约](drawborad-contracts.md)及[原验收文档](../../product/apple-experience-and-acceptance.md)。全部数字是未来实际测试门槛，当前 **NotRun**。目标Win11/185H/32GB/4070Laptop8GB；4K/64层、128压力、8K后台、RGB/CMYK8/16/PSD/离线完整包。三质量门必须同时过，不承诺估值/获奖/无限图片都无瑕。

## Review Focus

1. 缺Windows/Photoshop/实体笔时必须Blocked，不可假绿。
2. 真实24h/8h经过时间，不能加快计时或填字段。
3. 数据恢复/取消/磁盘满保留提交版本，不只测试成功路径。
4. 模型/文件fuzz有资源与时间边界，离线测试检查实际网络尝试。
5. 发布报告必须属于同commit/lock/model，禁止关掉CMYK/PSD等mandatory功能绕门。

## File Structure

`tests/fixtures`只存有许可清单和小型可再分发夹具；私有样本不进Git。`tools/qa`和`tools/bench`生成真实报告；`tests/chaos/fuzz`可重放失败；`packaging`完整离线打包；CI只跑授权资源。每task注册`cmake/TestTargets.cmake`测试。

### Task G01: 早期冻结样本、独立oracle与报告
**Dependencies:** F01。应提前执行，不等UI完工。
**Files:** Create `tests/fixtures/manifest.json`, `tests/fixtures/rights.json`, `tools/qa/make_synthetic.py`, `tools/qa/collect_oracles.py`, `schemas/qa-report.schema.json`; Test `tests/qa/ReportSchemaTests.py`。
**Interfaces:** Consumesapproved指标/合法样本/Photoshop；Produces `QaRunReport` schema；CLI `make_synthetic.py --count 200 --seed 20261004 --out <path>`、`collect_oracles.py --manifest <json> --out <path>`。
- [ ] **Step 1: Tests.** 本task夹具加载manifest，至少80产品/人像/毛发/插画/海报/UI/线稿/透明/复杂背景，调参集与验收集独立。
```python
assert synthetic_fixture_count == 200
assert eligible_reference_count >= 80
assert all(s.has_rights and s.sha256 for s in manifest)
assert missing_photoshop_report.status == 'Blocked'
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id G01 -Phase Red`；缺合法样本或Photoshop是Blocked，不能虚构素材清单。
- [ ] **Step 3: Implement.** manifest有来源/许可/hash/类别/ICC/尺寸/ROI/alpha；Photoshop与独立parser oracle记录版本和制作方法。schema严格NotRun/Pass/Fail/Blocked、真实start/end/build/env/log。源图片、私人客户资产不上传repo。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id G01 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "test: G01 evidence and fixture contract"`。
**Accept:** 没有真实独立oracle不以产品自生成样例互相自证。

### Task G02: 全工作流、颜色与PSD互通
**Dependencies:** U06, C07, G01。
**Files:** Create `tools/qa/e2e_win.ps1`, `tools/qa/psd_interop.py`; Test `tests/integration/DesktopRoundtrip.cpp`, `tests/integration/LayerIsolation.cpp`。
**Interfaces:** ConsumesDrawborad.exe/core模型/冻结样本/PSoracle/导出器；Produces `e2e_win.ps1 -Suite Functional|Interop -ReportDir <path>`、`psd_interop.py --manifest <path> --report <path>`。
- [ ] **Step 1: Tests.** 所有mandatory格式与RGB/CMYK8/16、组/Unicode/mask/alpha/5blend；10000可重放编辑命令。
```python
assert rgb_cmyk_8_16_layer_mask_roundtrip_pass
assert source_removed_replay_hash == saved_engine_hash
assert all_three_quality_gates_pass_per_sample
assert photoshop_unavailable_never_maps_to_pass
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id G02 -Phase Red`。
- [ ] **Step 3: Implement.** GUI自动化+独立parser+实际Photoshop打开；像素用规范化颜色哈希/误差而非PNG压缩字节。所有对象操作、原生工程重开、无源图回放、格式转换预检完整检查；实体笔/ICC/PS不由CI mock替代。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id G02 -Phase Green`; `powershell -File tools/qa/e2e_win.ps1 -Suite Interop -ReportDir out/reports/interop`。
- [ ] **Step 5: Review/commit.** `git commit -m "test: G02 end to end interchange"`。
**Accept:** 任一mandatory格式或任一质量门Fail/Blocked，不能宣布完整首版通过。

### Task G03: 4070Laptop目标机性能
**Dependencies:** R06, U06, G01。
**Files:** Create `tools/bench/target_machine.ps1`, `tools/bench/scenarios.json`, `tools/bench/report.py`; Test `tests/perf/NoMainThreadReplay.cpp`。
**Interfaces:** ConsumesrunRenderBench/WinDiagnostics/tracing；Produces `target_machine.ps1 -Profile Target4070 -ReportDir <path>`及FrameTimingReport。
- [ ] **Step 1: Tests.** 4096²/16和64可见层/百万事件；60秒预热+120秒实测至少3轮；128层和8K及CMYK16另列。
```python
assert warm_4k_16layers.p95_ms <= 33
assert warm_4k_64layers.p95_ms <= 33
assert warm_seek.p95_seconds <= 1
assert button_feedback.p95_ms <= 100
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id G03 -Phase Red`；目标机缺失Blocked。
- [ ] **Step 3: Implement.** 记录Windows build/driver/电源/DPI/热状态/GPU-LUID/model/lock-hash、原始ETW或timing、CPU/GPU峰值。冷/热与模型加载/重建分开；失败回对应模块优化，不缩小图或换基准假绿。
- [ ] **Step 4: GREEN.** `powershell -File tools/bench/target_machine.ps1 -Profile Target4070 -ReportDir out/reports/perf`; `powershell -File tools/test-task.ps1 -Id G03 -Phase Green`。
- [ ] **Step 5: Review/commit.** `git commit -m "test: G03 target machine performance gates"`。
**Accept:** 真机指标满足；Linux/Web/普通云runner不能冒充4070成绩。

### Task G04: 24小时队列、8小时交互与故障注入
**Dependencies:** G02, G03。
**Files:** Create `tools/qa/soak.ps1`, `tools/qa/chaos.ps1`; Test `tests/chaos/FaultInjector.cpp`, `tests/chaos/RecoveryMatrix.cpp`。
**Interfaces:** ConsumesJobBroker/ProjectStore/QueueStore/DeviceRecovery；Produces `soak.ps1 -QueueHours 24 -InteractionHours 8 -ReportDir <path>`、`chaos.ps1 -Seed 20261004 -Cases 10000`。
- [ ] **Step 1: Tests.** 取消/重启、IO磁盘满/只读、强制退出、journal截尾、设备丢失/显存压力、休眠、路径变更。
```python
assert queue_elapsed_hours >= 24
assert interaction_elapsed_hours >= 8
assert committed_state_survives_worker_crash
assert no_monotonic_unreleased_resource_growth
assert no_corruption_after_disk_full_or_device_loss
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id G04 -Phase Red`。
- [ ] **Step 3: Implement.** 使用临时测试盘/工程，故障注入flush/rename/IPC/device边界，保存seed与最小失败case。真实睡眠/断电未做记Blocked；资源曲线保留，不能填伪elapsed。
- [ ] **Step 4: GREEN.** `powershell -File tools/qa/soak.ps1 -QueueHours 24 -InteractionHours 8 -ReportDir out/reports/soak`; `powershell -File tools/qa/chaos.ps1 -Seed 20261004 -Cases 10000`; `powershell -File tools/test-task.ps1 -Id G04 -Phase Green`。
- [ ] **Step 5: Review/commit.** `git commit -m "test: G04 durability and long soak"`。
**Accept:** 实际时长不足不通过；发现bug修复后重跑受影响长链，不能仅删失败报告。

### Task G05: 安全fuzz和完全离线首用
**Dependencies:** C07, M05, G01, U06, G06。离线测试必须先拿到G06安装产物。
**Files:** Create `tests/fuzz/PsdFuzzer.cpp`, `tests/fuzz/ProjectFuzzer.cpp`, `tests/fuzz/ModelManifestFuzzer.cpp`, `tools/qa/fuzz.ps1`, `tools/qa/offline.ps1`。
**Interfaces:** Consumesparser/modelvalidator/installer/report；Produces `fuzz.ps1 -MinutesPerTarget 60 -MemoryMiB 1024`、`offline.ps1 -Installer <path> -ReportDir <path>`。
- [ ] **Step 1: Tests.** 每parser至少60分钟限额fuzz；断网干净Windows VM和目标GPU主机分别安装/首启/分层/导出。
```python
assert sanitizer_crash_count == 0
assert allocations_above_limit_are_rejected
assert app_and_workers_network_attempts == 0
assert no_runtime_downloaders_or_package_managers_required
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id G05 -Phase Red`。
- [ ] **Step 3: Implement.** 锁定LLVM/clang-cl sanitizer资格后fuzz；畸形尺寸/hash/路径/external-data/custom-op分别拒绝。网络监测区分应用与OS，不能控制OS服务便宣称全系统零连接；诊断不含客户图/密钥。
- [ ] **Step 4: GREEN.** `powershell -File tools/qa/fuzz.ps1 -MinutesPerTarget 60 -MemoryMiB 1024`; `powershell -File tools/qa/offline.ps1 -Installer out/package/setup.exe -ReportDir out/reports/offline`; `powershell -File tools/test-task.ps1 -Id G05 -Phase Green`。
- [ ] **Step 5: Review/commit.** `git commit -m "test: G05 offline and parser security"`。
**Accept:** 核心包安装后无需Python/npm/在线模型下载；恶意包不执行代码。

### Task G06: 完整离线EXE和Portable
**Dependencies:** F01, U06, M02, C07。
**Files:** Create `packaging/Drawborad.nsi`, `packaging/runtime-manifest.json`, `packaging/licenses/NOTICE.txt`, `tools/package.ps1`, `tools/sbom.py`; Test `tests/package/InstallTests.ps1`。
**Interfaces:** Consumesrelease二进制/qualified Tiny-Small/依赖锁/许可ICC/翻译；Produces `package.ps1 -Mode OfflineExe|Portable -Input <path> -Output <path>`、`Drawborad-win11-x64-<version>-setup.exe`、portable.zip、SBOM/checksums/licenses。
- [ ] **Step 1: Tests.** 无Python/npm/CUDAToolkit的干净VM、标准用户/中文emoji长路径、升级失败回滚、不同安装位置和卸载。
```python
assert no_missing_core_models_in_full_bundle
assert install_and_uninstall_work_without_runtime_network
assert uninstall_preserves_user_projects
assert portable_relocation_preserves_project_open
assert unsigned_preview_is_not_marked_signed
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id G06 -Phase Red`。
- [ ] **Step 3: Implement.** NSIS/portable含核心模型、CPU/CUDA运行库、Qt、合法ICC、本地说明；动态Qt许可/模型codec再分发/SBOM逐项审核。大包可分卷本地hash验证；不要求关安全软件。签名证书缺失只能明确内部未签预览，不能伪造正式签名。
- [ ] **Step 4: GREEN.** `powershell -File tools/package.ps1 -Mode OfflineExe -Input out/stage -Output out/package`; `powershell -File tools/package.ps1 -Mode Portable -Input out/stage -Output out/package`; `powershell -File tools/test-task.ps1 -Id G06 -Phase Green`。
- [ ] **Step 5: Review/commit.** `git commit -m "build: G06 offline Windows distribution"`；只commit清单/scripts，不权重/用户图/签名密钥。
**Accept:** installer/portable实际可用且保留用户工程；完整包不能缺标准或高质量核心模型。

### Task G07: 最终证据门禁与受控交付
**Dependencies:** G02, G03, G04, G05, G06。
**Files:** Create `tools/qa/release_gate.py`, `schemas/release-evidence.schema.json`, `docs/release/checklist.md`, `.github/workflows/windows-ci.yml`, `.github/workflows/release-candidate.yml`; Test `tests/qa/ReleaseGateTests.py`。
**Interfaces:** Consumes同commit/build/lock/model的mandatory reports/SBOM/artifacts；Produces `release_gate.py --manifest <json> --report-dir <path> --artifact-dir <path>`，只有全门Pass返回0。
- [ ] **Step 1: Tests.** 假Pass缺日志、零测试、旧build、缺PS/实体笔/许可/签名、已知数据丢失、禁用核心功能绕门。
```python
assert missing_report.exit_code != 0
assert blocked_required_check.exit_code != 0
assert mismatched_build_hash.exit_code != 0
assert known_data_loss_bug.exit_code != 0
assert all_required_pass.exit_code == 0
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id G07 -Phase Red`。
- [ ] **Step 3: Implement.** 核心CMYK/PSD/自然重绘不能以“禁用该功能”规避门；可选HEIC/OCR可以明确禁用。普通Windows CI只证明编译/CPU，目标GPU/PS证据来自真实受控机；不允许不受信PR触发自托管或拿密钥。公开release/收费runner另需授权，本轮不启动。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id G07 -Phase Green`; `python tools/qa/release_gate.py --manifest out/release-manifest.json --report-dir out/reports --artifact-dir out/package`。
- [ ] **Step 5: Review/commit.** `git commit -m "ci: G07 evidence-gated desktop release"`。
**Accept:** 交付EXE/portable/源码/可编辑样例/真实RGB-CMYK PSD/PNG/TIFF/报告；未执行不标Pass，不宣称软件完成、获奖或估值。

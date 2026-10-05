# 原生工程、文档内核与安全执行底座 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立可测试的 Windows 原生程序与事务化文档内核，让后续模块不再共享网页全局状态。
**Architecture:** C++20核心不依赖QML；不可变快照经显式命令变更。磁盘块、journal和独立worker边界先于复杂UI落地。
**Tech Stack:** C++20 / Qt Quick / D3D11 / Little CMS / ONNX Runtime / CMake / CTest。
**Spec:** [approved design](../specs/2026-10-04-drawborad-win11-design.md)，基线 `f3a92378ccbd8d9145556f5019642f134b362fbb`。

## Global Constraints

逐条适用[总计划](2026-10-04-drawborad-implementation-plan.md)的Global Constraints与[接口契约](drawborad-contracts.md)。Windows11 x64/185H/32GB/4070Laptop8GB；512²块；RAM8–12GiB常规/18GiB回收；显示2GiB/总GPU6GiB软预算；1重GPU任务。4K/64层常规、128压力、8K后台不降尺寸。RGB/CMYK8/16、真PSD、PSB仅输入、完全离线、自定义安全模型包、对象×阶段、自然过程不换照片。中英/Light-Dark/Apple式交互保留Windows行为。所有产品任务当前 **NotRun**。

## Review Focus

1. F05/M06：取消后迟到结果不得覆盖新revision。
2. C01/C02/E04：CMYK、alpha与文字关键边不得用预览或字体替换蒙混。
3. F04/C07/G04：中文长路径、磁盘满、journal损坏保留有效工程。
4. M01/M05/G05：模型包无任意代码/目录逃逸/隐式联网。
5. R02/R05/R06/U06：DPI/设备变化不得污染像素与缓存。

## File Structure

`src/core`只管文档/类型/命令；`src/storage`只管块和事务；`src/jobs`只管独立任务；`src/platform`是Windows绑定；`apps`是进程入口；`ui/Main.qml`只展示原生状态。各任务的精确文件在下方，后续任务仅修改 `cmake/TestTargets.cmake` 注册自己的测试。

### Task F01: 锁定依赖、原生测试入口与离线构建证据
**Dependencies:** none。
**Files:** Create `CMakeLists.txt`, `CMakePresets.json`, `cmake/Dependencies.cmake`, `dependencies/lock.json`, `tools/bootstrap.ps1`, `tools/test-task.ps1`, `tests/support/TestMain.cpp`, `tests/build/DependencyGate.cpp`, `tests/CMakeLists.txt`, `cmake/TestTargets.cmake`。
**Interfaces:** Consumes候选版本表；Produces `bootstrap.ps1 -Mode Acquire|Verify -Cache <path>`、`test-task.ps1 -Id <task> -Phase Red|Green`。CTest名等于task ID。
- [ ] **Step 1: Write failing tests.** 本任务定义本地DependencyError、verify_lock夹具，四份清单为有效/缺hash/篡改/DLL缺失；先建最小测试runner和失败stub，不借用F02类型。
```cpp
CHECK(verify_lock(missing_digest).code == DependencyError::MissingDigest);
CHECK(verify_lock(changed_archive).code == DependencyError::DigestMismatch);
CHECK(offline_smoke.network_attempts == 0);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id F01 -Phase Red`；新行为断言失败才算RED，编译环境/命令缺失是Blocked。
- [ ] **Step 3: Implement.** 本地哈希验证、开发Acquire受控下载、离线Verify；禁止构建FetchContent临时联网。逐个记录实际Windows工具/DLL/源码包patch、SHA256、许可、ABI、安全补丁；`cl /Bv`、SDK、Qt、CUDA/cuDNN加载可查。缺字节/主机不得编造hash；候选调整有ADR。定义Catch2/QtTest/Python标准库runner，零测试失败。
- [ ] **Step 4: GREEN.** `powershell -File tools/bootstrap.ps1 -Mode Verify -Cache .deps`; `cmake --preset win-debug`; `cmake --build --preset win-debug`; `ctest --preset win-debug -R '^F01$' --no-tests=error --output-on-failure`；真实smoke启动退出成功。
- [ ] **Step 5: Review/commit.** 核验diff/日志，只stage本任务文件；`git commit -m "build: F01 foundation"`。
**Accept:** offline lock无空版本/hash/许可/资格记录；不把候选表当兼容性已过。

### Task F02: 统一类型、自由尺寸与RGB/CMYK描述
**Dependencies:** F01。
**Files:** Create `src/core/Types.h`, `src/core/Result.h`, `src/core/DocumentSpec.h`, `src/core/DocumentSpec.cpp`, `src/core/PixelFormat.h`; Test `tests/core/DocumentSpecTests.cpp`; Modify `cmake/TestTargets.cmake`。
**Interfaces:** Consumes共享契约；Produces `Result<DocumentSpec> validateDocument(const NewDocumentRequest&, const ResourceBudget&)`、`Result<PixelSize> toPixels(const Dimensions&)`、`uint8_t PixelFormat::channelCount() const`。
- [ ] **Step 1: Write failing tests.** 1×1/4096²/8192²/3173×911/0/负数/NaN/溢出；超预算前无分配。
```cpp
CHECK(toPixels(Dimensions{25.4,25.4,Unit::Mm,300}).value() == PixelSize{300,300});
CHECK(PixelFormat{ColorMode::CMYK,Depth::U16,true}.channelCount() == 5);
CHECK(validateDocument(overflow_request,budget).error().code == ErrorCode::Overflow);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id F02 -Phase Red`；断言失败。
- [ ] **Step 3: Implement.** 带溢出检查64-bit尺寸/字节/块乘法；PPI-only和resample分离；RGBA/CMYKA权威存储；CMYK缺ProfileId返回ProfileRequired。自由比例不被预设锁死。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id F02 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** 只stage列明文件和CTest注册；`git commit -m "feat: F02 document specification"`。
**Accept:** 资源/格式边界明确，不静默缩图，不假无限画布。

### Task F03: 对象×阶段树与可逆事务
**Dependencies:** F02。
**Files:** Create `src/core/Document.h`, `src/core/Document.cpp`, `src/core/Commands.h`, `src/core/CommandBus.cpp`; Test `tests/core/CommandTests.cpp`; Modify `cmake/TestTargets.cmake`。
**Interfaces:** ConsumesDocumentSpec/NodeId/Revision；Produces `DocumentSnapshot Document::snapshot() const`、`Result<ChangeSet> Document::apply(const Command&, Revision)`、`Result<ChangeSet> Document::undo()`/`redo()`。
- [ ] **Step 1: Write failing tests.** 测试定义两对象各两阶段、独立alpha-mask夹具，覆盖改名/顺序/复制/删除/显隐/变换/合并/拆分。
```cpp
CHECK(doc.apply(move_into_own_child,rev).error().code == ErrorCode::Cycle);
CHECK(doc.apply(paint_locked_layer,rev).error().code == ErrorCode::Locked);
CHECK(after_undo_hash == before_hash);
CHECK(after_redo_hash == edited_hash);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id F03 -Phase Red`。
- [ ] **Step 3: Implement.** 层主所有权树与资源依赖DAG分开；稳定ID、不可变快照。命令返回区域ChangeSet，拒绝锁层/循环/重复ID/过期revision；undo涵盖属性与mask。merge/split留来源映射，层重排不改变历史事件身份。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id F03 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** 消费者类型/diff/日志核验；`git commit -m "feat: F03 transactional document"`。
**Accept:** 局部编辑不改其他对象，所有已承诺编辑可以撤销重做。

### Task F04: 块存储、保存恢复和历史预算
**Dependencies:** F03。
**Files:** Create `src/storage/TileStore.h`, `src/storage/TileStore.cpp`, `src/storage/ProjectStore.h`, `src/storage/ProjectStore.cpp`, `src/storage/Journal.cpp`; Test `tests/storage/RecoveryTests.cpp`; Modify `cmake/TestTargets.cmake`。
**Interfaces:** ConsumesDocumentSnapshot/TileData/ProjectPath/ChangeSet；Produces `Result<TileId> TileStore::put(const TileData&)`、`Result<TileData> TileStore::get(TileId)`、`Result<void> ProjectStore::save(const ProjectPath&, const DocumentSnapshot&)`、`Result<RecoveryResult> ProjectStore::recover(const ProjectPath&)`。
- [ ] **Step 1: Write failing tests.** 注入FileIO在write/flush/rename失败，截断journal尾、只读/占用/磁盘满；这里检查状态hash，像素复现由R01/E05负责。
```cpp
CHECK(recover_truncated_tail.last_revision == last_committed_revision);
CHECK(failed_save.original_sha256 == before_save_sha256);
CHECK(reopen_without_source_or_model.state_hash == saved_state_hash);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id F04 -Phase Red`。
- [ ] **Step 3: Implement.** 512²内容寻址块、二进制事件、ICC原字节；单写者journal、FlushFileBuffers、同卷原子替换/备份。打开不跑模型；缓存可弃。30秒有变更时保存已提交事务；历史默认10GiB预算，当前有效版本与保护点不可删。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id F04 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** 检查失败日志及磁盘残留；`git commit -m "feat: F04 durable project store"`。
**Accept:** 最后有效工程可恢复；取消和半成品不覆盖原工程。

### Task F05: Worker协议、取消与资源治理
**Dependencies:** F04。
**Files:** Create `src/jobs/Protocol.h`, `src/jobs/JobBroker.cpp`, `src/jobs/WinWorkerHost.cpp`, `src/jobs/ResourceGovernor.cpp`, `apps/worker/Main.cpp`; Test `tests/jobs/BrokerTests.cpp`; Modify `cmake/TestTargets.cmake`。
**Interfaces:** ConsumesSnapshotRef/JobRequest/JobResult/ResourceBudget；Produces `Result<JobId> JobBroker::submit(const JobRequest&)`、`void JobBroker::cancel(JobId)`、`Result<ChangeSet> JobBroker::accept(const JobResult&, Document&)`、`ResourceDecision ResourceGovernor::admit(const ResourceRequest&) const`。
- [ ] **Step 1: Write failing tests.** fake worker延迟/崩溃/重复回应/wrong jobId/撤销后返回；fake clock测即时ack与5秒终止，实际时延归G03。
```cpp
CHECK(broker.accept(stale_result,doc).error().code == ErrorCode::StaleRevision);
CHECK(governor.concurrent_heavy_gpu_jobs == 1);
CHECK(cancelled_job.committed_tiles == 0);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id F05 -Phase Red`。
- [ ] **Step 3: Implement.** 受限named-pipe消息+只读共享映射；JobObject管理worker。先协作取消，5秒无响应终止整子作业；只写staging，revision/hash正确才原子提交。按实时余量收紧RAM/GPU软预算，不能把ONNX内存池当总显存上限。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id F05 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** 检查所有迟到/取消分支；`git commit -m "feat: F05 isolated job broker"`。
**Accept:** 取消/设备故障最多丢可重建缓存，正式版本安全。

### Task F06: 原生Qt外壳与诊断
**Dependencies:** F05。
**Files:** Create `apps/studio/Main.cpp`, `apps/studio/ApplicationController.h`, `apps/studio/ApplicationController.cpp`, `ui/Main.qml`, `src/platform/WinDiagnostics.cpp`; Test `tests/platform/NativeSmokeTests.cpp`; Modify `cmake/TestTargets.cmake`。
**Interfaces:** ConsumesDocument/ProjectStore/JobBroker；Produces `ApplicationController::newDocument(NewDocumentRequest) -> Result<void>`、`ApplicationController::open(ProjectPath) -> Result<void>`、`DeviceReport collectDeviceReport()`；CLI `Drawborad.exe --diagnostics <json-path>`。
- [ ] **Step 1: Write failing tests.** WARP和硬件D3D11启动空文档，1280×800；记录真实OS/scenegraph/device/driver。
```cpp
CHECK(launch_result.webview_modules == 0);
CHECK(cancel_during_worker_load.document_revision == original_revision);
CHECK(report.contains_os_build_and_gpu_luid);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id F06 -Phase Red`；Windows缺失为Blocked。
- [ ] **Step 3: Implement.** Win标题栏/Snap/系统菜单；显示adapter和CUDA device分别记录；worker忙时窗可移动/关。空白、恢复、错误三状态完整，不默认简笔模板冒充作品。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id F06 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: F06 native desktop shell"`；只stage列明路径及测试注册。
**Accept:** 原生程序可启动，标内部skeleton，不标完整产品。

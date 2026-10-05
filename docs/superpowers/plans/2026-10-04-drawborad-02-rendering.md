# 分块画布、真实笔刷与独立回放 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 不重放全工程即可编辑百万笔迹，同时可从空白逐笔重建作品。
**Architecture:** CPU参考像素是权威；GPU仅缓存/合成受影响块。历史、内容和显示缓存分开，QML只提交命令。
**Tech Stack:** C++20 / Qt Quick QRhi D3D11 / CPU rasterizer / CMake-CTest。
**Spec:** [approved design](../specs/2026-10-04-drawborad-win11-design.md)，基线`f3a92378ccbd8d9145556f5019642f134b362fbb`。

## Global Constraints

适用[总计划](2026-10-04-drawborad-implementation-plan.md)和[契约](drawborad-contracts.md)。Win11/185H/32GB/4070Laptop8GB；512²tile、RAM8–12/18GiB、GPU显示2/应用6GiB软预算、1重GPU任务。4K/64层热交互P95<=33ms，128压力、8K后台不降实际像素。RGB/CMYKA8/16、离线、无原图兜底/整图residual/帧间重新抽随机。产品测试全部 **NotRun**。

## Review Focus

F05/M06拒绝旧结果；C01/C02/E04关键颜色/文字；F04/C07/G04路径和恢复；M01/M05/G05安全离线；R02/R05/R06/U06缓存/DPI/device。真实GPU/实体笔证据和单元模拟分开。

## File Structure

`src/render`CPU/reference/content/display缓存，`src/platform`Qt和Windows绑定，`src/brush`输入到事件，`src/history`索引，`src/view`坐标，`tools/bench`原始采样。每task修改`cmake/TestTargets.cmake`注册测试；Green后统一执行 `ctest --preset win-debug --no-tests=error --output-on-failure`。提交仅stage本task文件及测试注册，先核验diff/接口/log。

### Task R01: 确定性参考渲染
**Dependencies:** F03, F04, C01。
**Files:** Create `src/render/ReferenceRenderer.h`, `src/render/ReferenceRenderer.cpp`, `src/render/CoverageRasterizer.cpp`, `src/render/PixelBlend.cpp`; Test `tests/render/ReferenceTests.cpp`。
**Interfaces:** ConsumesDocumentSnapshot/StrokeEvent/TileKey/TileData/ColorTransform/TileStore；Produces `Result<TileData> ReferenceRenderer::render(const DocumentSnapshot&, TileKey, EventLimit, std::stop_token)`, `PixelHash canonicalPixelHash(const TileData&)`。
- [ ] **Step 1: Tests.** 跨512边界、alpha渐变、CMYKA16、guide/eraser、固定seed/舍入；本项原始Normal合成，已验证高级blend由C02接入。
```cpp
CHECK(full_replay_hash == checkpoint_replay_hash);
CHECK(native_cmyk_output.format.channelCount() == 5);
CHECK(source_asset_reads_during_replay == 0);
CHECK(erase_one_layer.other_layer_hash == original_other_hash);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id R01 -Phase Red`；缺环境不是有效RED。
- [ ] **Step 3: Implement.** 定点coverage/固定舍入，按笔迹bounds索引；alpha独立。局部stroke/region/eraser事件，不准嵌图或sourceHandle；guide单独可擦。噪声编译时固定，重播不新增随机数。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id R01 -Phase Green`，统一回归。
- [ ] **Step 5: Commit.** `git commit -m "feat: R01 canonical reference renderer"`。
**Accept:** 锁定CPU重复哈希一致；GPU显示按独立容差而非跨驱动相同承诺。

### Task R02: 增量合成与原生画布
**Dependencies:** F06, R01, C02。
**Files:** Create `src/render/TileCache.h`, `src/render/TileCache.cpp`, `src/render/DisplayCache.cpp`, `src/platform/CanvasItem.h`, `src/platform/CanvasItem.cpp`, `src/platform/CanvasRenderer.cpp`; Test `tests/render/DirtyTileTests.cpp`。
**Interfaces:** ConsumesReferenceRenderer/ChangeSet/DocumentSnapshot/**DisplayTileKey**/C02 CPU proof pixels；Produces `DirtySet affectedTiles(const ChangeSet&, const DocumentSnapshot&)`, `Result<DisplayTile> DisplayCache::get(const DisplayTileKey&)`, `void CanvasItem::setSnapshot(DocumentSnapshot)`。
- [ ] **Step 1: Tests.** 4096²/16层/百万事件夹具，记录事件访问/内容失效/GPU上传；不能仅截图判流畅。
```cpp
CHECK(small_brush_edit.replayed_events < total_events);
CHECK(pan_warm_cache.reconstruction_calls == 0);
CHECK(toggle_visibility.invalidated_pigment_tiles == 0);
CHECK(old_monitor_icc_texture_reused == false);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id R02 -Phase Red`。
- [ ] **Step 3: Implement.** QQuickRhiItem/QRhi纹理块；内容用layer/mask/region版本，禁止全局revision令全图作废。DisplayTileKey包含ICC/intent/LOD/变换；DPI用于显示分辨率不改文档。UI只读快照，显隐/透明度仅重合成。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id R02 -Phase Green`，统一回归。
- [ ] **Step 5: Commit.** `git commit -m "feat: R02 incremental native display"`。
**Accept:** 平移/属性编辑不触发全事件重放。

### Task R03: 真实输入与自然笔刷
**Dependencies:** R01, F05。
**Files:** Create `src/brush/BrushEngine.h`, `src/brush/BrushEngine.cpp`, `src/brush/SampleResampler.cpp`, `src/platform/TabletInput.cpp`; Test `tests/brush/InputTests.cpp`。
**Interfaces:** ConsumesInputSample/BrushSettings/StrokeEvent/CommandBus；Produces `Result<StrokeEvent> BrushEngine::compile(std::span<const InputSample>, const BrushSettings&)`, `InputSample normalizeTabletSample(const TabletPacket&)`。
- [ ] **Step 1: Tests.** 真0压力不可当缺值，tilt/笔尾可选、重复兼容鼠标事件、笔出窗/DPI；合成与物理笔测试分列。
```cpp
CHECK(same_samples_same_seed.bytes == repeated.bytes);
CHECK(zero_pressure_preserved == true);
CHECK(eraser_event.layer_id == selected_layer);
CHECK(mouse_fallback.claims_tilt_support == false);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id R03 -Phase Red`。
- [ ] **Step 3: Implement.** 按距离重采样，保存真实device capability；铅笔grain/marker透明重叠/彩铅短线/高光各有规律但不随机扭形。同笔独立preview，抬笔一次commit；不得为动效增加输入延迟。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id R03 -Phase Green`，统一回归；物理Ink没测Blocked。
- [ ] **Step 5: Commit.** `git commit -m "feat: R03 pressure aware brush engine"`。
**Accept:** 鼠标回退明确，不伪造设备功能，不反复叠preview变深。

### Task R04: 时间/空间索引回放
**Dependencies:** R01, R03, F04。
**Files:** Create `src/history/EventIndex.h`, `src/history/EventIndex.cpp`, `src/history/ReplayEngine.h`, `src/history/ReplayEngine.cpp`; Test `tests/history/ReplayTests.cpp`。
**Interfaces:** ConsumesStrokeEvent/SnapshotRef/ReferenceRenderer/TileStore；Produces `Result<ReplayFrame> ReplayEngine::seek(const ReplayRequest&, std::stop_token)`, `Result<ReplayFrame> ReplayEngine::step(EventDirection)`, `Result<ReplayFrame> ReplayEngine::newMarks(const ReplayRequest&)`。
- [ ] **Step 1: Tests.** 章边±1tick/部分笔迹/撤销版本/坏cache/逐笔逐组；索引256事件或250ms先到即分块。
```cpp
CHECK(seek_blank.nonpaper_pixels == 0);
CHECK(seek_reverse_then_forward.pixel_hash == direct_seek.pixel_hash);
CHECK(chapter_boundary.source_replacement_calls == 0);
CHECK(loop.completed_event_ids_stay_in_selected_range);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id R04 -Phase Red`。
- [ ] **Step 3: Implement.** 最近有效checkpoint继续，坏hash弃cache再算；对象/块/时间索引；只看新增是视图不改像素。原历史和当前编辑版本明确；正常编辑不开时间轴。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id R04 -Phase Green`，统一回归。
- [ ] **Step 5: Commit.** `git commit -m "feat: R04 indexed independent replay"`。
**Accept:** 倒退/暂停/变速/loop不换照片、不污染作品。

### Task R05: 视口/DPI/命中坐标
**Dependencies:** R02, R03。
**Files:** Create `src/view/Viewport.h`, `src/view/Viewport.cpp`, `src/view/HitTest.cpp`; Test `tests/view/CoordinateTests.cpp`。
**Interfaces:** ConsumesDocumentSnapshot/LayerTransform/InputSample；Produces `DocumentPoint Viewport::toDocument(ScreenPoint) const`, `ScreenPoint Viewport::toScreen(DocumentPoint) const`, `Result<NodeId> hitTest(const DocumentSnapshot&, DocumentPoint)`。
- [ ] **Step 1: Tests.** 100/125/150/200/250%DPI、不同ICC双屏、负坐标/旋转/非整数zoom；单测与真机不同报告。
```cpp
CHECK(distance(roundtrip,p) < 0.01);
CHECK(dpi_change.document_pixels_unchanged);
CHECK(zoom_anchor.document_point == anchor_before);
CHECK(hidden_or_locked_nodes_not_paintable);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id R05 -Phase Red`。
- [ ] **Step 3: Implement.** DIP/屏幕px/文档px分离；zoom锚点固定，pan不改作品；transform同作用于mask/bounds/hit。捕获丢失明确commit或cancel规则，退化矩阵拒绝。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id R05 -Phase Green`，统一回归。
- [ ] **Step 5: Commit.** `git commit -m "feat: R05 stable viewport mapping"`。
**Accept:** 无笔尖偏移、DPI触发重采样或跨屏色彩旧缓存。

### Task R06: 内存/设备丢失与实测入口
**Dependencies:** R02, R04, F05。
**Files:** Create `src/render/CacheEvictor.cpp`, `src/platform/DeviceRecovery.cpp`, `tools/bench/render_bench.cpp`; Test `tests/render/ResourceTests.cpp`。
**Interfaces:** ConsumesResourceGovernor/DisplayCache/ReplayEngine；Produces `Result<void> recoverGraphicsDevice(const DeviceLostEvent&, const DocumentSnapshot&)`, `BenchReport runRenderBench(const BenchScenario&)`。
- [ ] **Step 1: Tests.** OOM/removed/6GiB总GPU/18GiB RAM软线、pin块保护、CPUfallback。
```cpp
CHECK(device_lost.saved_document_hash == document_before);
CHECK(memory_pressure.evicts_unpinned_cache_first);
CHECK(disabled_gpu.cpu_export_succeeds);
CHECK(report.p95_frame_ms.has_value());
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id R06 -Phase Red`。
- [ ] **Step 3: Implement.** device lost只重可丢cache；输入优先、64层分组合成；128压力可低清临时预览但文档/导出尺寸不能改。cold/warm/CMYK16/热降频分开记录，提供G03真实可运行benchCLI。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id R06 -Phase Green`，统一回归。
- [ ] **Step 5: Commit.** `git commit -m "feat: R06 bounded graphics resources"`。
**Accept:** 不能用模拟时间宣称真机帧率，峰值/回收结果有raw trace。

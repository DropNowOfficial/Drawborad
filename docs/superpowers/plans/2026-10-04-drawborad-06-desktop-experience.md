# Apple 式完整桌面体验与专业编辑工作流 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 以稳定画布为中心提供精简/专业双密度、中文/英语、真实图层与颜色编辑，而不是营销网页。
**Architecture:** Qt Quick控件只调用Controller命令，耗时任务在worker。界面主题、动效、文本与文档像素分离；关键拖动1:1输入，不加动画延迟。
**Tech Stack:** Qt6 QML/C++20 / Windows Ink / Qt Test / native canvas。
**Spec:** [approved design](../specs/2026-10-04-drawborad-win11-design.md)，基线`f3a92378ccbd8d9145556f5019642f134b362fbb`。

## Global Constraints

适用[总计划](2026-10-04-drawborad-implementation-plan.md)、[契约](drawborad-contracts.md)、[体验验收](../../product/apple-experience-and-acceptance.md)。Win11原生/4070Laptop8GB/32GB；4K/64层、128压力、8K后台；真CMYK8/16/PSD/离线自定义模型/对象×阶段。Apple式反馈层级、系统字体，不打包Apple受限资产、不牺牲Windows窗口/快捷键/笔。所有产品测试 **NotRun**。

## Review Focus

F05/M06取消过期结果；C01/C02/E04颜色文字的独立验证；F04/C07/G04恢复/复杂路径；M01/M05/G05离线自定义包；R02/R05/R06/U06跨屏DPI/可访问性。视觉好看不证明输出正确。

## File Structure

`ui/tokens`设计令牌、`ui/controls`可复用控件、`ui/pages`对话/管理页、`ui/workspaces`工作区；`src/ui`只桥接核心；`resources/i18n`中英消息。每task在`cmake/TestTargets.cmake`注册Qt UI测试。实际控制器不得在QML里跑像素循环。

### Task U01: 设计系统、欢迎和自由新建
**Dependencies:** F06, C01, R02。
**Files:** Create `ui/tokens/Theme.qml`, `ui/tokens/Spacing.qml`, `ui/controls/Control.qml`, `ui/pages/Welcome.qml`, `ui/pages/NewDocument.qml`, `src/ui/NewDocumentController.cpp`, `src/ui/ThemeController.cpp`; Test `tests/ui/NewDocumentTests.cpp`。
**Interfaces:** ConsumesNewDocumentRequest/ColorService/ApplicationController；Produces `Result<void> NewDocumentController::submit(const NewDocumentRequest&)`, `void ThemeController::setTheme(ThemeMode)`。
- [ ] **Step 1: Tests.** 自由3173×911、mm/cm/in/PPI、RGB/CMYK8/16、未选ICC、超预算、透明纸、主题切换。
```cpp
CHECK(custom_size.document_size == PixelSize{3173,911});
CHECK(cmyk_without_profile.error == ErrorCode::ProfileRequired);
CHECK(theme_switch.document_hash == before_hash);
CHECK(oversized_request.shows_preallocation_error);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id U01 -Phase Red`。
- [ ] **Step 3: Implement.** 4/8DIP间距、13–14DIP主文字/通常≥11辅助、中性创作区，欢迎/设置浅色可切；资源预估即时显示。新建有预设和完全自定义并存；无强制登录、在线模板、简笔模板冒充成品。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id U01 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: U01 desktop visual foundation"`。
**Accept:** 新建、错误、加载、恢复入口都可用，不只有截图状态。

### Task U02: 对象图层树与专业检查器
**Dependencies:** U01, F03, R02, M04。
**Files:** Create `ui/controls/ObjectTree.qml`, `ui/controls/LayerInspector.qml`, `ui/controls/ColorChannels.qml`, `src/ui/LayerTreeModel.cpp`, `src/ui/WorkspaceController.cpp`; Test `tests/ui/LayerInspectorTests.cpp`。
**Interfaces:** ConsumesDocumentSnapshot/Command/MaskEdit；Produces `Result<ChangeSet> LayerTreeModel::dispatch(const Command&)`, `void WorkspaceController::setDensity(DensityMode)`。
- [ ] **Step 1: Tests.** 64/128层虚拟化、对象×阶段、显隐锁定/改名/重排/复制/删除/opacity/blend/mask；CMYK4通道和alpha分开。
```cpp
CHECK(density_switch.document_hash_unchanged);
CHECK(layer_reorder.event_ids_unchanged);
CHECK(opacity_drag.full_history_replays == 0);
CHECK(cmyk_channel_controls.count == 4);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id U02 -Phase Red`。
- [ ] **Step 3: Implement.** 简洁工具常驻、专业检查器可展开；虚拟化可搜索对象树与context actions，全部走CommandBus。组总opacity与每笔alpha不重复相乘；用户命名不随UI语言翻译。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id U02 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: U02 object stage inspector"`。
**Accept:** 不是固定八层面板；复杂性可按需显示但功能不消失。

### Task U03: 画师编辑、蒙版和数位笔
**Dependencies:** U02, R03, R05, C02。
**Files:** Create `ui/workspaces/Paint.qml`, `ui/controls/ToolRail.qml`, `ui/controls/BrushInspector.qml`, `src/ui/PaintController.cpp`; Test `tests/ui/PaintWorkflowTests.cpp`。
**Interfaces:** ConsumesInputSample/BrushSettings/LayerTransform；Produces `Result<void> PaintController::begin(const InputSample&)`, `void PaintController::update(const InputSample&)`, `Result<ChangeSet> PaintController::end(const InputSample&)`, `void PaintController::selectTool(std::string_view)`。
- [ ] **Step 1: Tests.** Pencil/Marker/White/Eraser/Mask、压感/笔尾/tilt、选错锁层、笔出窗、手动编辑后undo/redo、CMYK颜色输入。
```cpp
CHECK(live_preview.commits_before_pen_up == 0);
CHECK(pen_up.commits == 1);
CHECK(undo_then_redo.pixel_hash == edited_hash);
CHECK(brush_drag.contains_added_spring_delay == false);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id U03 -Phase Red`。
- [ ] **Step 3: Implement.** 当前笔独立预览、抬笔提交，原始采样/确定性重采样可诊断；mask修边和像素画笔模式清楚。空格pan、Ctrl+Z/S、Esc取消遵Windows；不同时将笔事件和兼容鼠标事件画两次。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id U03 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`；实体笔报告缺失Blocked。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: U03 painter editing workspace"`。
**Accept:** 不是只能看回放，用户能继续编辑并安全撤销。

### Task U04: Auto→Review→Reconstruct→Refine→Export
**Dependencies:** U03, E06, C07。
**Files:** Create `ui/workspaces/Review.qml`, `ui/workspaces/Reconstruct.qml`, `ui/pages/Export.qml`, `ui/pages/QualityReport.qml`, `src/ui/WorkflowController.cpp`; Test `tests/ui/WorkflowTests.cpp`。
**Interfaces:** ConsumesReviewedObjects/ReconstructionRequest/QualityReport/ExportPlan；Produces `Result<void> WorkflowController::advance(WorkflowAction)`, `void WorkflowController::cancel(JobId)`。
- [ ] **Step 1: Tests.** 导入到PSD/PNG真实链；低置信/未Review、质量未过、取消重建、CMYK→PNG无转换同意、目标文件存在。
```cpp
CHECK(unreviewed.advance_reconstruct.rejected);
CHECK(failed_quality.badge != Status::Pass);
CHECK(cmyk_png_without_consent.export_started == false);
CHECK(cancel_preserves_previous_valid_artwork);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id U04 -Phase Red`。
- [ ] **Step 3: Implement.** 清楚显示当前对象/步骤/实际工作进度、原图比较是显式模式，未完成不假100%。成功/失败/取消均有下一动作；导出预检位深ICCalpha尺寸路径兼容性，PSD不由ORA替代。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id U04 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: U04 complete reconstruction workflow"`。
**Accept:** E06未过不能通过此项；UI不掩盖后端能力缺口。

### Task U05: 本地模型、队列和恢复管理
**Dependencies:** U02, M05, M06, F04。
**Files:** Create `ui/pages/ModelManager.qml`, `ui/pages/Queue.qml`, `ui/pages/Recovery.qml`, `src/ui/ManagementController.cpp`; Test `tests/ui/ManagementTests.cpp`。
**Interfaces:** ConsumesModelRegistry/CompatibilityReport/QueuePolicy/ProjectStore；Produces `Result<ModelId> ManagementController::importModel(const PackagePath&, TrustDecision)`, `Result<void> ManagementController::recover(const ProjectPath&)`。
- [ ] **Step 1: Tests.** 自定义兼容包与拒绝原因、未签确认、活跃包卸载、队列暂停/取消/失败继续、损坏恢复和旧工程保留。
```cpp
CHECK(model_import.has_network_dependency == false);
CHECK(active_model_uninstall.refused);
CHECK(queue_cancel.only_selected_job_cancelled);
CHECK(recover_user_project.original_preserved);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id U05 -Phase Red`。
- [ ] **Step 3: Implement.** 本地包文件选择、hash/license/provider/memory/probe显示；模型标准/高质量/自定义可真正切换，无默认下载按钮。queue Review状态可处理；恢复选择版本及损坏说明，不自动覆盖。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id U05 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: U05 local project management"`。
**Accept:** 离线首次启动有完整可用路径，不把缺模型甩给联网服务。

### Task U06: 回放、中英、可中断动效与可访问性
**Dependencies:** U04, U05, R04。
**Files:** Create `ui/workspaces/Replay.qml`, `ui/controls/CommandPalette.qml`, `ui/controls/InterruptiblePanel.qml`, `src/ui/LocaleController.cpp`, `resources/i18n/zh_CN.ts`, `resources/i18n/en_US.ts`; Test `tests/ui/ExperienceTests.cpp`。
**Interfaces:** ConsumesReplayEngine/UiScenario/Language；Produces `void LocaleController::setLanguage(Language)`, `ExperienceReport runExperienceChecks(const UiScenario&)`。
- [ ] **Step 1: Tests.** 中英实时切换、Light/Dark、减少运动、高对比、Narrator焦点、DPI100/125/150/200/250、1366×768到4K；转场途中反向、后台推理时输入。
```cpp
CHECK(language_switch.document_hash_unchanged);
CHECK(user_layer_names_not_translated);
CHECK(panel_reverse.starts_at_current_progress);
CHECK(reduced_motion.delayed_pointer_actions == 0);
CHECK(essential_controls_keyboard_reachable);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id U06 -Phase Red`。
- [ ] **Step 3: Implement.** 120–220ms非输入关键转场可中断；直接操作零人为延迟。Ctrl/Alt/Space快捷键冲突明确；重建过程独立工作区，逐笔/组/速度/loop/只看新增/局部放大。文本4.5:1、关键边界3:1、高DPI不截断；无Apple私有字体图标。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id U06 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`；真实Windows/DPI/笔与人工视觉报告分列。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: U06 accessible bilingual replay experience"`。
**Accept:** 排版/留白/层级/色彩/动效/微交互/适配/原创性逐项审查，不仅静态截图。

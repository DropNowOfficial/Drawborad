# Drawborad Windows 11 全量 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在用户的 Windows 11 / 185H / 32 GB / RTX 4070 Laptop 8 GB 上交付完全离线、可自定义图像、真实对象分层、自然重绘及 PSD/PNG/CMYK 多格式输出的专业桌面软件。

**Architecture:** C++20 文档和参考渲染核心；Qt Quick 原生桌面界面、D3D11 分块显示；独立分析/重绘/codec worker。对象编辑与时间回放分离，源图用于显式分析而不是终点兜底。

**Tech Stack:** C++20、Qt 6、CMake/CTest、Little CMS、ONNX Runtime CPU/CUDA、原生 PSD 编解码与图像库。候选版本、真实锁定条件和跨模块数据定义见 [接口与依赖契约](drawborad-contracts.md)。

**Spec:** [已批准设计](../specs/2026-10-04-drawborad-win11-design.md)，不可变基线 commit `f3a92378ccbd8d9145556f5019642f134b362fbb`；[用户审批记录](../../product/design-approval-2026-10-04.md)。用户这次批准的是设计，计划仍待审阅；未运行任何产品代码或测试。

## Global Constraints

- 保真、对象可编辑性、自然绘制过程三项同时达标；不能用成品换图、全图residual或全幅随机线条冒充。
- 完整安装包包含已支持标准/高质量核心模型、运行库和资源，安装/首次使用/后续工作不要求联网；无默认遥测、云fallback、在线许可和在线自动更新。
- C++20原生画布而非HTML套壳；512×512块；RAM常规8–12GiB、18GiB回收软线；显示GPU约2GiB、总GPU约6GiB软预算；单个重GPU任务。
- 4096×4096/64可见层为核心工作档；128层压力档；8192×8192允许后台细化/导出；自由宽高、比例、单位和PPI，不能静默降尺寸。
- RGB(A)/CMYK(A) 8/16-bit、ICC/P3/AdobeRGB/自定义CMYK、软打样；PSB可输入但不输出；PSD有真图层/组/蒙版。PNG等需要显式RGB转换。
- Windows Ink真实能力、中文/英语切换、Light/Dark、Apple式设计与Windows原生交互；不分发Apple字体/私有图标。
- 对象×阶段结构、Review、局部修层、五种风格、队列、取消、自动恢复、版本化原生工程均是首版范围，不做账户支付插件云服务。
- 原始设计中的“待审阅”是上次文档历史状态，已被用户“设计通过”解除；本计划审阅和实施方法确认仍独立。

## Review Focus

1. Cancel后迟到结果与保存并发：F05/M06/F04有拒绝旧revision及恢复测试。
2. CMYK/alpha/Logo小字与颜色显示：C01/C02/C05/E04使用独立oracle与ROI，不以合成缩略图自证。
3. 自定义模型和恶意文件：M01/M05/C07/G05验证路径、shape、资源、无任意代码和无隐式联网。
4. 真机DPI/笔输入/设备丢失：R03/R05/R06/U06与G03分开做合成测试和实体Windows验证。
5. 假测试通过：G01/G07拒绝缺报告、空套件、旧build、Blocked转Pass和删核心能力绕过门槛。

## 1. 已勘查仓库与本次范围

分支 `design/win11-offline-studio-20261004` 当前设计基线仅README和三份设计文档，没有原生源码。此前HTML没有被迁为桌面产品；可作外部回归参考，不自动复制其状态管理/渲染性能问题。保留仓库拼写Drawborad。

本轮一次交付 **7份子系统计划、44个带RED/GREEN/提交门的任务、统一接口契约、30项需求映射和机器可读任务DAG**。每项有精确文件位置、消费/产出接口、断言、执行命令和验收条件。所有checkbox留空，没有把规划标成实现。

|分册|计划|任务|
|---|---|---|
| 01 | [原生工程、文档内核与安全执行底座](2026-10-04-drawborad-01-foundation.md) | F01, F02, F03, F04, F05, F06 |
| 02 | [分块画布、真实笔刷与独立回放](2026-10-04-drawborad-02-rendering.md) | R01, R02, R03, R04, R05, R06 |
| 03 | [真实 CMYK、ICC 与 Photoshop 文件互通](2026-10-04-drawborad-03-color-formats.md) | C01, C02, C03, C04, C05, C06, C07 |
| 04 | [离线模型、自定义包、对象分层与复核](2026-10-04-drawborad-04-models-review.md) | M01, M02, M03, M04, M05, M06 |
| 05 | [保真重绘、自然过程与三重质量验收](2026-10-04-drawborad-05-reconstruction.md) | E01, E02, E03, E04, E05, E06 |
| 06 | [Apple 式完整桌面体验与专业编辑工作流](2026-10-04-drawborad-06-desktop-experience.md) | U01, U02, U03, U04, U05, U06 |
| 07 | [独立验收、压力与安全测试、离线 Windows 交付](2026-10-04-drawborad-07-qa-release.md) | G01, G02, G03, G04, G05, G06, G07 |

辅助：[需求覆盖](drawborad-requirement-coverage.md) / [任务DAG](drawborad-task-index.json) / [接口与依赖](drawborad-contracts.md)。

## 2. 模块与目录边界

`src/core`文档与命令；`src/storage`块/journal/原生工程；`src/render`权威像素与显示缓存；`src/history`回放；`src/color`ICC/CMYK；`src/codecs`受限文件解析与输出；`src/jobs`安全worker/queue；`src/models`本地包/adapter；`src/segmentation`候选；`src/review`人工蒙版；`src/reconstruct`笔迹编译；`src/quality`独立门；`src/ui`仅桥接；`src/platform`Win/Qt图形和输入。`apps/studio`、`apps/worker`、`apps/codec`三个入口，UI不做重计算。

`ui`使用可复用令牌/控件/页面，`resources/i18n`中英文本，`schemas`版本化协议，`tools`开发/QA/打包，`tests`按所属模块。`models`只存recipe/manifest，权重不进Git。`out/`/`.deps/`/用户缓存和凭据不进Git。

模块实现前先提交对应header契约和测试。跨模块通过不可变快照/DTO/JobId，不跨线程共享可变文档。命令/协议有版本；改变签名时修改本文和DAG并跑消费者测试，不能一边开发一边悄悄换名。

## 3. 实施顺序：先证明难点，再精修整个体验

|里程碑|真正可验收的产物|通过条件|
|---|---|---|
|M0 供应链与底座|F01–F06、G01；原生启动、命令、保存恢复、worker取消|锁定实际依赖，Windows smoke，损坏/取消安全|
|M1 颜色/文件/画布|C01–C07、R01–R06按DAG交错|CMYK四通道、独立PSD/PSB解析、PSD往返、缓存编辑，不靠照片兜底|
|M2 重绘纵向链|M01–M06、E01–E06按DAG|小代表集完整导入→Review→分区重绘→回放→PSD/PNG→重开；3质量门缺一不可|
|M3 完整画师体验|U01–U06；可并行做低风险视图但依赖门不跳过|中英主题、专业/精简、模型/队列、自由尺寸、CMYK、数位笔、导出全部接真功能|
|M4 压力与安全|G02–G06，离线安装产物供离线测试|目标机、Photoshop、24h/8h、fuzz、DPI/笔、离线新装实测，缺环境为Blocked|
|M5 受控交付|G07|同commit/lock/model全部mandatory报告通过；明确签名/内部预览身份|

**最早风险门**：G01尽早获得有权样本与独立Oracle；C02/C05先证明CMYK与PSD，M02证明8GB机器模型路线，E06证明自然过程与成品同时成立。不把这些延迟到“UI都做好了再说”。

可执行拓扑顺序（不是时间估算，任务详情/依赖以JSON为准）：

`F01 -> F02 -> G01 -> F03 -> F04 -> C01 -> F05 -> C02 -> C03 -> F06 -> M01 -> R01 -> C04 -> C06 -> M02 -> R02 -> R03 -> C05 -> M03 -> R04 -> R05 -> U01 -> C07 -> M04 -> M05 -> R06 -> E01 -> M06 -> U02 -> E02 -> U03 -> U05 -> E03 -> E04 -> E05 -> E06 -> U04 -> U06 -> G02 -> G03 -> G06 -> G04 -> G05 -> G07`

并行边界：F02和G01可独立；F04之后C/模型资格准备/基础UI可按接口分支展开；R01完成前不可交付重绘；C02未过不可声称CMYK混合；E06未过不能把U界面作为产品验收。共享Types/Command/schema由一位集成者维护，避免并行改同一契约。

## 4. 统一每项闭环

- [ ] 从本轮计划commit建立隔离feature分支/工作区，读取所属计划与approved spec。
- [ ] 先写所属行为测试及夹具，实际运行看到RED；命令缺失/环境不足不算RED成功。
- [ ] 只实现该任务接口，确认GREEN及消费者回归；文档和类型一致。
- [ ] 检查diff、记录build/lock/日志，提交一个可回滚的改动；不要顺便修改其他模块。
- [ ] 运行任务人工/真机门；环境缺失标Blocked并停止该任务完成勾，不制造测试证据。

大功能完整性靠全部task通过，而不是一个巨大的最终commit。没有执行任务，不打勾。可在执行时拆更小commit，但不减少本文验收内容。

## 5. 测试和最终交付

测试执行记录与计划分开。单测/集成可在构建机运行；4070Laptop真机帧率、WindowsInk实体输入、Photoshop互通、离线干净机安装、休眠/设备丢失分别有环境和证据。G07必须识别 `NotRun/Blocked/Fail`，只有全部核心证据Pass才标首版完成。

指定目标保留：4096²、16/64可见层、百万事件，热交互P95<=33ms；128层压力安全；常见按钮反馈<=100ms、热定位<=1s；SSIM>=0.98/DeltaE00均值<=2与P95<=5/Alpha MAE<=2/255加独立ROI/自然度评审；>=80合法代表图+200程序化边界图；10000编辑事务；24h队列、8h交互；每解析目标>=60min限额模糊测试。以上均是将来运行的门槛，不是本轮成绩。

最终产物是Windows EXE安装包、portable、源代码、离线核心模型/许可/SBOM/校验清单、可编辑原生工程与RGB/CMYK PSD/PNG/TIFF样例、测试报告与已知限制。无授权素材/模型/密钥不进普通Git历史。上传构建/模型包、收费runner、公开Release和主分支合并都另需明确授权；本轮只push文档。

## 6. 执行方法与审阅

建议一次批准整套计划后按DAG逐项推进，不再重复需求问卷。当前聊天没有可调用的独立子代理或Windows目标主机，不能假称已经具备独立reviewer或真机测试。可采用当前会话逐项实施（executing-plans），在有支持的Codex环境中采用subagent-driven-development；两者都保留相同测试和证据门，不降低范围。

下一步需审阅本计划并确认执行方式。缺目标机/Photoshop时继续可验证的源码任务并把相关门记Blocked；不能以网页演示或Linux截图替代Windows产品。设计已批准，本次不重复请求设计审批。

# 需求到任务、证据与发布门的映射

状态：设计已批准；实施计划待审阅；所有产品任务 **NotRun**。来源为`docs/product/requirements-2026-10-04.md`的R01–R30，覆盖表不是测试结果。

|需求|不可削弱的约束|任务|验收|
|---|---|---|---|
|R01|长期自用专业软件，商业边界预留|F06,U04,G07|G02/G07|
|R02|2B完整离线安装和首用|F01,M01,M02,G05,G06|断网新装VM+目标GPU|
|R03|ABC三质量同时通过|E01,E02,E03,E06|Fidelity AND Layers AND Process|
|R04|对象×绘画阶段|F03,M04,E01,U02|对象隔离编辑|
|R05|不补画不可见内容|M03,M04,E01|遮挡fixture|
|R06|文字Logo忠实、可选候选|E04,C04,C05|ROI/缺字体|
|R07|常见输入含PSD/PSB、可选HEIC|C03,C04,C07|格式矩阵|
|R08|多格式输出但无PSB导出|C05,C06,C07|独立互通|
|R09|真PSD组层蒙版混合|C02,C04,C05|Photoshop打开|
|R10|真CMYK/ICC/8-16bit/自由尺寸|F02,C01,C02,C05,U01|通道/ICC/尺寸oracle|
|R11|4K顺滑8K后台不降尺寸|R02,R06,G03|真机perf|
|R12|64层正常128压力|F03,R06,U02,G03,G04|资源/层trace|
|R13|WindowsInk压力倾角笔尾|R03,R05,U03|合成+实体笔|
|R14|随design审批的A人工修层|F03,M04,U02,U03|mask编辑|
|R15|Fidelity和dry-media风格|E03,E05,E06|风格隔离|
|R16|Auto Review Reconstruct Refine Export|M04,M06,U04|真实状态机|
|R17|4070Laptop8GB CUDA主CPU回退|F05,M02,R06,G03|真实device/peak|
|R18|标准高质量+自定义本地模型|M01,M02,M05,U05,G06|安装/拒绝/离线|
|R19|中英切换|U06|切换不改文档|
|R20|Apple式完整主题和Windows习惯|U01,U02,U03,U06|八维体验检查|
|R21|精简模式与专业深度|U02,U06|切密度不重建|
|R22|EXE和portable|G06,G07|安装卸载|
|R23|默认零遥测，资料不离机|F05,M01,G05|网络审计|
|R24|保留ICC/隐私可选/无附加水印|C03,C06,E04|元数据检查|
|R25|自动保存journal原子恢复|F04,U05,G04|故障注入|
|R26|预算历史/checkpoint|F04,R04,U05|历史hash|
|R27|无任意代码插件|M01,M05,G05|包安全|
|R28|单重GPU队列|F05,M06,U05,G04|24h队列|
|R29|自定义图→无原图兜底的分层工程|E01,E06,U04,G02|移除原图重播+PSD|
|R30|商业边界、不做账号支付云|F06,G06,G07|源码/SBOM/release|

M0–M5是实施顺序，不是缩减首版的借口。CMYK、PSD、完全离线、对象编辑和自然回放是mandatory；HEIC和可选文字候选可以明确禁用。文档审查、构建单测、CPU/GPU/Photoshop/实体笔和实际24h/8h长测分别报告。未执行NotRun，缺环境Blocked，不能写Pass。权重、客户素材、凭据不进Git。

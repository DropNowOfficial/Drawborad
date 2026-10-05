# 真实 CMYK、ICC 与 Photoshop 文件互通 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 原生颜色、图层与文件交付独立可验证，不用RGB预览冒充CMYK。
**Architecture:** ICC/颜色服务输出CPU像素，绝不反向依赖GPU纹理模块；隔离codec进程有界解码并流式原子输出。PSD读取和写入分别用外部oracle核验。
**Tech Stack:** C++20、Little CMS、native codecs、CMake/CTest。
**Spec:** [approved design](../specs/2026-10-04-drawborad-win11-design.md)，基线`f3a92378ccbd8d9145556f5019642f134b362fbb`。

## Global Constraints

适用[总计划](2026-10-04-drawborad-implementation-plan.md)和[接口契约](drawborad-contracts.md)。Win11/185H/32GB/4070Laptop8GB；512²块；RAM8–12/18GiB、GPU显示2/应用6GiB软预算；1重GPU任务。RGB(A)/CMYK(A)8/16-bit/ICC/自由尺寸；PSB仅输入、PSD真图层；完全离线，无原图兜底/终点换图。4K/64层正常、128压力、8K后台。全部产品测试 **NotRun**。

## Review Focus

F05/M06迟到结果拒绝；C01/C02/E04关键文字颜色独立oracle；F04/C07/G04原子保存/路径；M01/M05/G05恶意包/文件；R02/R05/R06/U06显示不得污染权威像素。没有真实PS oracle要Blocked。

## File Structure

`src/color`ICC和CPU通道，`src/codecs`预检/输入/输出，`src/codecs/psd`Photoshop段结构，`apps/codec`独立入口。下面每项都修改`cmake/TestTargets.cmake`注册同ID测试；每项Green后运行 `ctest --preset win-debug --no-tests=error --output-on-failure`。只stage列明文件与注册；提交前审查接口、diff、测试日志。

### Task C01: ICC赋予与转换
**Dependencies:** F02, F04, G01。
**Files:** Create `src/color/ProfileStore.h`, `src/color/ProfileStore.cpp`, `src/color/ColorService.h`, `src/color/ColorService.cpp`; Test `tests/color/ProfileTests.cpp`。
**Interfaces:** ConsumesProfileId/ColorSpec/TileData/Document；Produces `Result<ProfileId> ProfileStore::import(std::span<const std::byte>)`, `Result<ChangeSet> ColorService::assign(Document&, ProfileId)`, `Result<TileData> ColorService::convert(const TileData&, const ColorTransform&)`。
- [ ] **Step 1: Tests.** 合法固定hash的sRGB/P3/AdobeRGB/CMYK，缺失/损坏/通道错误/非有限值；本task定义assign/convert夹具。
```cpp
CHECK(assign.profile_changes && assign.pixel_bytes_unchanged);
CHECK(convert.pixel_bytes_changed && convert.alpha_unchanged);
CHECK(untagged_cmyk.error == ErrorCode::ProfileRequired);
CHECK(corrupt_icc.error == ErrorCode::InvalidProfile);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id C01 -Phase Red`；行为失败而非命令找不到；缺ICC/oracle为Blocked。
- [ ] **Step 3: Implement.** Profile hash索引、保留ICC；Assign只变解释，Convert记录intent/BPC/version；alpha不转换。RGB可提议sRGB，CMYK必须选profile，分析视图不改源通道。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id C01 -Phase Green`，再跑本分册统一回归命令。
- [ ] **Step 5: Commit.** `git commit -m "feat: C01 ICC services"`。
**Accept:** 真CMYKA保留；无授权profile不能捏造万能CMYK。

### Task C02: 原生CMYK混合与CPU软打样
**Dependencies:** C01, F03, G01。
**Files:** Create `src/color/BlendPolicy.h`, `src/color/BlendPolicy.cpp`, `src/color/ProofTransform.cpp`, `tests/oracles/photoshop/README.md`; Test `tests/color/BlendOracleTests.cpp`。
**Interfaces:** ConsumesColorTransform/PixelFormat/BlendMode；Produces `Result<PixelBlock> blendPixels(const BlendRequest&)`, **`Result<PixelBlock> softProof(const ProofRequest&)`**, `BlendSupport supportedBlend(PixelFormat, BlendMode)`。PixelBlock就是TileData；GPU上传留R02，不能返回GPU DisplayTile造成依赖倒置。
- [ ] **Step 1: Tests.** 0/50/100%K、rich-black、alpha/mask、孤立组、五blend与RGB/CMYK8/16；标样记录PS/ICC/意图。
```cpp
CHECK(normal_cmyka.alpha == oracle.alpha);
CHECK(rgb_and_cmyk_8_16_match_oracle_within_one_output_lsb);
CHECK(unsupported_blend_returns_error_not_normal_fallback);
CHECK(display_profile_change.native_channel_hash_unchanged);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id C02 -Phase Red`。
- [ ] **Step 3: Implement.** Normal/Multiply/Screen/Darken/Lighten按固定文档空间策略，不把CMYK当RGB简单乘；高精中间量/固定舍入。软打样输出CPU RGB PixelBlock、纸白模拟/色域警告；未验证blend不能启用。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id C02 -Phase Green`，统一回归；外部oracle缺失Blocked。
- [ ] **Step 5: Commit.** `git commit -m "feat: C02 CMYK blend and proof"`。
**Accept:** 颜色策略先验证后供显示/PSD引用，K通道不由平均色差掩盖。

### Task C03: 有界图像输入
**Dependencies:** F05, C01。
**Files:** Create `src/codecs/ImageReader.h`, `src/codecs/ImageReader.cpp`, `src/codecs/InputPreflight.cpp`, `src/codecs/HeicAdapter.cpp`, `apps/codec/Main.cpp`; Test `tests/codecs/ImageReadTests.cpp`。
**Interfaces:** ConsumesInputPath/DecodeOptions/ResourceBudget/ColorService；Produces `Result<InputInfo> preflightInput(const InputPath&, const ResourceBudget&)`, `Result<ImportedImage> decodeImage(const InputPath&, const DecodeOptions&, std::stop_token)`。
- [ ] **Step 1: Tests.** PNG/JPEG/WebP/TIFF/BMP支持组合、EXIF1–8、ICC/GPS、alpha、截断、巨尺寸、多页TIFF；不支持组合明确报错。
```cpp
CHECK(exif_rotated.dimensions == oriented_dimensions);
CHECK(transparent_edge.alpha == expected_alpha);
CHECK(huge_declared_size.allocations_before_reject == 0);
CHECK(missing_heic_module.network_attempts == 0);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id C03 -Phase Red`。
- [ ] **Step 3: Implement.** 按魔数预检后隔离解码到TileStore；保留原始/定向尺寸和来源，帧/页面显式选择。HEIC只支持审核过离线codec，缺失不触发网络安装。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id C03 -Phase Green`，统一回归。
- [ ] **Step 5: Commit.** `git commit -m "feat: C03 bounded image import"`。
**Accept:** 失败有资源上限/可解释信息/源文件不变保证。

### Task C04: PSD/PSB导入
**Dependencies:** C03, F03。
**Files:** Create `src/codecs/psd/PsdReader.h`, `src/codecs/psd/PsdReader.cpp`, `src/codecs/psd/BoundedReader.cpp`, `src/codecs/psd/LayerRecords.cpp`; Test `tests/codecs/PsdReadTests.cpp`。
**Interfaces:** ConsumesInputInfo/ImportedImage/DocumentSpec/TileStore；Produces `Result<ImportResult> readPhotoshop(const InputPath&, const ImportPolicy&, std::stop_token)`。
- [ ] **Step 1: Tests.** PSDv1/PSBv2、raw/RLE/ZIP、luni、lsct/lsdk、Lr16、mask offset、错误段长/大PSB、文字/智能对象不支持策略。
```cpp
CHECK(rgb16.layer_group_order == oracle.group_order);
CHECK(cmyk16.k_channel == oracle.k_channel);
CHECK(psb_large_offsets_are_checked_64_bit);
CHECK(smart_object.without_consent.error == ErrorCode::UnsupportedFeature);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id C04 -Phase Red`。
- [ ] **Step 3: Implement.** 先扫描unsupported和边界再分配；保留组/像素/mask/五blend。未知高级特性选择取消或显式已有合成图作为标识新输入，无可靠预览拒绝；不宣称保留未知可编辑结构。PSB只输入。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id C04 -Phase Green`，统一回归。
- [ ] **Step 5: Commit.** `git commit -m "feat: C04 PSD PSB input"`。
**Accept:** 独立parser/PS对照，ImportedRaster与Reconstructed来源清楚。

### Task C05: 真实PSD流式输出
**Dependencies:** C04, C02, R01。
**Files:** Create `src/codecs/psd/PsdWriter.h`, `src/codecs/psd/PsdWriter.cpp`, `src/codecs/psd/ChannelEncoder.cpp`; Test `tests/codecs/PsdWriteTests.cpp`。
**Interfaces:** ConsumesDocumentSnapshot/ExportRequest/TileStore/ReferenceRenderer；Produces `Result<ExportReport> writePsd(const DocumentSnapshot&, const ExportRequest&, std::stop_token)`。
- [ ] **Step 1: Tests.** RGB8/16、CMYK8/16四族，中文组/mask/alpha/opacity/全部启用blend，层数据和merged分别验证。
```cpp
CHECK(independent_reader.layer_count == expected_count);
CHECK(mask_pixels_and_offsets_roundtrip);
CHECK(cmyk_encoded_channel_polarity == adobe_fixture_polarity);
CHECK(cancel_export.existing_destination_sha == original_sha);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id C05 -Phase Red`。
- [ ] **Step 3: Implement.** Adobe header/resources/ICC/resolution/layer-mask/merged段；CMYK极性/alpha/16-bit blocks单测。流式临时文件与长度回填、校验后提交。支持边界30000px/边或预估2GiB超限拒绝，不自动改PSB/伪Text/SmartObject。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id C05 -Phase Green`，统一回归及实际PS重开；缺PS为Blocked。
- [ ] **Step 5: Commit.** `git commit -m "feat: C05 real layered PSD export"`。
**Accept:** 不能只证明缩略图可见，真实层与CMYK颜色必须互通。

### Task C06: 其他格式与明确转换
**Dependencies:** C03, R01, C02。
**Files:** Create `src/codecs/ExportService.h`, `src/codecs/ExportService.cpp`, `src/codecs/RasterWriters.cpp`, `src/codecs/OraWriter.cpp`, `src/codecs/LayerArchive.cpp`; Test `tests/codecs/ExportMatrixTests.cpp`。
**Interfaces:** ConsumesExportRequest/DocumentSnapshot/ColorService/ReferenceRenderer；Produces `Result<ExportPlan> preflightExport(const DocumentSnapshot&, const ExportRequest&)`, `Result<ExportReport> ExportService::write(const ExportPlan&, std::stop_token)`。
- [ ] **Step 1: Tests.** PNG8/16alpha、TIFF RGB/CMYK8/16、JPEG/WebP、ORA/逐层ZIP；ICC/尺寸/offset/Unicode/隐私字段。
```cpp
CHECK(cmyk_to_png_without_explicit_rgb_profile.error == ErrorCode::ConversionConsentRequired);
CHECK(tiff16_cmyka.channel_count == 5);
CHECK(jpeg_with_alpha.requires_matte_choice);
CHECK(ora.first_entry == "mimetype" && ora.mime == "image/openraster");
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id C06 -Phase Red`。
- [ ] **Step 3: Implement.** CMYK→PNG/WebP/ORA显式RGB转换；JPEG明确matte。ORA首条未压缩mimetype、stack/merged/透明层完整；ZIP含offset/name/profile转换manifest。ICC/PPI保留、GPS默认不传播，其他EXIF/XMP用户选择。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id C06 -Phase Green`，统一回归。
- [ ] **Step 5: Commit.** `git commit -m "feat: C06 multi-format export"`。
**Accept:** UI和实际能力一致；无静默扁平化/位深或色彩转换。

### Task C07: 原子提交与文件安全
**Dependencies:** C05, C06, F04。
**Files:** Create `src/codecs/AtomicExport.cpp`, `src/codecs/CodecLimits.h`; Test `tests/codecs/FailureMatrixTests.cpp`, `tests/fuzz/CodecFuzz.cpp`。
**Interfaces:** ConsumesStagedExport/DestinationPolicy/ResourceBudget；Produces `Result<ExportReport> commitExport(const StagedExport&, const DestinationPolicy&)`, `CodecLimits defaultCodecLimits(const ResourceBudget&)`。
- [ ] **Step 1: Tests.** 中文/emoji/UNC/长路径、磁盘满/只读/文件占用、replace失败、取消、解压炸弹/路径逃逸。
```cpp
CHECK(failed_replace.destination_sha == previous_sha);
CHECK(path_escape.error == ErrorCode::PathEscape);
CHECK(decompression_bomb.peak_bytes <= configured_limit);
CHECK(cancelled_export.published_files == 0);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id C07 -Phase Red`。
- [ ] **Step 3: Implement.** 同卷staging校验后原子替换/失败回滚；size/offset/count溢出检查；worker资源/超时限制；只清理本次临时物，不改源文件/用户旧文件。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id C07 -Phase Green`，统一回归；长fuzz归G05。
- [ ] **Step 5: Commit.** `git commit -m "fix: C07 atomic safe codec output"`。
**Accept:** 输出失败不能损坏活动工程或已有目的文件。

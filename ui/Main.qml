import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Drawborad.Native 1.0
ApplicationWindow {
    id: window
    objectName: "nativeWindow"
    width: 1280
    height: 800
    visible: true
    color: "#1c1c1e"
    title: "Drawborad / Windows Alpha 0.1"
    property bool approvedClose: false
    property bool closeAfterSave: false
    Timer { id: replayTimer; interval: 100; repeat: true; onTriggered: { if (studio.busy || studio.replayCount >= studio.eventCount) stop(); else studio.setReplayCount(studio.replayCount + 1) } }
    FileDialog { id: lineartDialog; title: qsTr("嵌入同尺寸黑白灰 PNG 线稿"); nameFilters: ["PNG (*.png)"]; onAccepted: studio.importLineartUrl(selectedFile) }
    FileDialog { id: pngExportDialog; title: qsTr("导出原尺寸 RGB PNG（新文件名）"); fileMode: FileDialog.SaveFile; defaultSuffix: "png"; nameFilters: ["PNG (*.png)"]; onAccepted: studio.workflow.exportNative(selectedFile, "png") }
    FileDialog { id: oraExportDialog; title: qsTr("导出叶图层 ORA（组展开、蒙版烘焙；新文件名）"); fileMode: FileDialog.SaveFile; defaultSuffix: "ora"; nameFilters: ["OpenRaster (*.ora)"]; onAccepted: studio.workflow.exportNative(selectedFile, "ora") }
    FileDialog { id: psdDialog; title: qsTr("导出笔迹分层 PSD（请选择新文件名）"); fileMode: FileDialog.SaveFile; defaultSuffix: "psd"; nameFilters: ["Photoshop (*.psd)"]; onAccepted: studio.workflow.exportPsd(selectedFile) }
    FileDialog { id: relinkDialog; title: qsTr("选择与原始文件哈希一致的图片"); nameFilters: ["PNG/JPEG (*.png *.jpg *.jpeg)"]; onAccepted: studio.workflow.relinkSource(selectedFile) }
    Dialog { id: fillPendingDialog; title: qsTr("填色尚未提交"); modal: true; standardButtons: Dialog.Ok; Label { text: qsTr("请等待填色完成并保存，或点击取消填色后再关闭。"); wrapMode: Text.Wrap } }
    onClosing: function(event) {
        if (studio.fillBusy) { event.accepted = false; fillPendingDialog.open(); return }
        if (studio.dirty && !approvedClose) { event.accepted = false; closeDialog.open() }
    }
    header: ToolBar {
        padding: 12
        background: Rectangle { color: "#29292c" }
        RowLayout {
            anchors.fill: parent
            Label { text: "Drawborad"; color: "#fafafa"; font.pixelSize: 18; font.weight: Font.DemiBold }
            Label { text: qsTr("本地图像工作流 · Alpha"); color: "#aaaab0"; font.pixelSize: 11; Layout.leftMargin: 12 }
            Item { Layout.fillWidth: true }
            Button { objectName: "newDocumentButton"; text: qsTr("New"); onClicked: newDialog.open() }
            Button { objectName: "importForReviewButton"; text: qsTr("导入图片 · 分割审阅"); enabled: !studio.busy && studio.workflow.modelReady; onClicked: {
                if (studio.workflow.pending) replaceReviewDialog.open()
                else analysisImageDialog.open()
            } }
            Button { text: qsTr("Open project"); onClicked: { if (studio.dirty) replaceDialog.open(); else openDialog.open() } }
            Button { text: qsTr("Save"); enabled: studio.hasDocument; onClicked: saveDialog.open() }
            Button { objectName: "promptReviewButton"; text: qsTr("按提示补候选"); enabled: studio.workflow.hasReview && studio.workflow.modelReady && !studio.busy; onClicked: studio.workflow.startPrompted() }
            Button { objectName: "reconstructButton"; text: qsTr("生成笔迹"); enabled: studio.workflow.editingExisting && !studio.workflow.pending && !studio.busy; onClicked: { if (studio.workflow.startReconstruction()) workspaceTabs.currentIndex = 1 } }
            Button { objectName: "nativeExportButton"; text: qsTr("PNG / ORA"); enabled: studio.hasDocument && !studio.busy && !studio.workflow.pending; onClicked: nativeExportMenu.open()
                Menu { id: nativeExportMenu
                    MenuItem { text: qsTr("原尺寸 RGB PNG"); onTriggered: pngExportDialog.open() }
                    MenuItem { text: qsTr("叶图层 ORA（组/蒙版烘焙）"); onTriggered: oraExportDialog.open() }
                }
            }
            Button { objectName: "exportPsdButton"; text: qsTr("PSD（受限）"); enabled: studio.hasDocument && !studio.busy && !studio.workflow.pending; onClicked: psdDialog.open() }
        }
    }
    RowLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 24
        Rectangle {
            objectName: "canvasPanel"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 600
            color: "#323235"
            radius: 10
            TabBar {
                id: workspaceTabs; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                visible: studio.workflow.hasReview || studio.workflow.analyzing
                TabButton { text: qsTr("原图与蒙版审阅") }
                TabButton { text: qsTr("绘制结果") }
            }
            NativeCanvas { id: nativeCanvas; objectName: "nativeCanvas"; anchors.fill: parent; anchors.margins: 24; anchors.topMargin: workspaceTabs.visible ? 64 : 24; visible: studio.hasDocument && !reviewPanel.visible; bridge: studio }
            Label { objectName: "strokePreviewStatus"; anchors.bottom: nativeCanvas.bottom; anchors.horizontalCenter: nativeCanvas.horizontalCenter; visible: nativeCanvas.strokePreviewActive; text: nativeCanvas.previewing ? qsTr("笔迹预览更新中 · 尚未提交") : qsTr("未提交预览 · 松手提交，Esc 取消"); color: "#ffffff"; padding: 6; background: Rectangle { color: "#c0202428" } }
            Label { objectName: "canvasRendering"; anchors.centerIn: nativeCanvas; visible: nativeCanvas.visible && nativeCanvas.rendering; text: qsTr("Rendering document…"); color: "#bcbcc4" }
            Label {
                objectName: "canvasRenderError"
                anchors.centerIn: nativeCanvas
                width: Math.min(nativeCanvas.width - 32, 560)
                visible: nativeCanvas.visible && nativeCanvas.renderError.length > 0
                text: qsTr("This document cannot be fully displayed. Your layers have not been changed.") + "\n" + nativeCanvas.renderError
                color: "#ffb4ab"; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
            }
            Item {
                id: reviewPanel; objectName: "reviewPanel"
                anchors.fill: nativeCanvas
                visible: (studio.workflow.hasReview || studio.workflow.analyzing) && workspaceTabs.currentIndex === 0
                Image { id: analysisSource; anchors.fill: parent; source: studio.workflow.sourceUrl; autoTransform: true; fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false }
                Image { id: maskOverlay; anchors.fill: parent; source: studio.workflow.overlayUrl; fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false }
                MouseArea {
                    id: maskEditor; objectName: "maskEditor"; anchors.fill: parent
                    enabled: studio.workflow.hasReview && !studio.workflow.busy && (studio.workflow.activeIndex >= 0 || maskTool.currentIndex >= 4) && maskTool.currentIndex > 0
                    property real startX: 0
                    property real startY: 0
                    onPressed: function(mouse) { startX = mouse.x; startY = mouse.y }
                    onReleased: function(mouse) {
                        if (maskOverlay.paintedWidth <= 0 || maskOverlay.paintedHeight <= 0) return
                        const ox = (width - maskOverlay.paintedWidth) / 2
                        const oy = (height - maskOverlay.paintedHeight) / 2
                        const sx = studio.workflow.imageWidth / maskOverlay.paintedWidth
                        const sy = studio.workflow.imageHeight / maskOverlay.paintedHeight
                        if (maskTool.currentIndex === 4 || maskTool.currentIndex === 5) {
                            studio.workflow.addPromptPoint((mouse.x - ox) * sx, (mouse.y - oy) * sy, maskTool.currentIndex === 4)
                        } else if (maskTool.currentIndex === 6) {
                            studio.workflow.setPromptBox((startX - ox) * sx, (startY - oy) * sy, (mouse.x - ox) * sx, (mouse.y - oy) * sy)
                        } else if (maskTool.currentIndex === 3) {
                            if (studio.workflow.refineRectangle((startX - ox) * sx, (startY - oy) * sy,
                                (mouse.x - ox) * sx, (mouse.y - oy) * sy)) workspaceTabs.currentIndex = 1
                        } else studio.workflow.editRectangle(maskTool.currentIndex === 1,
                            (startX - ox) * sx, (startY - oy) * sy, (mouse.x - ox) * sx, (mouse.y - oy) * sy)
                    }
                    Rectangle {
                        visible: maskEditor.pressed
                        x: Math.min(maskEditor.startX, maskEditor.mouseX); y: Math.min(maskEditor.startY, maskEditor.mouseY)
                        width: Math.abs(maskEditor.mouseX - maskEditor.startX); height: Math.abs(maskEditor.mouseY - maskEditor.startY)
                        color: "#3050ddc8"; border.color: "#50ddc8"
                    }
                    Repeater {
                        model: studio.workflow.promptPoints
                        delegate: Rectangle {
                            required property var modelData
                            width: 8; height: 8; radius: 4; border.width: 1; border.color: "white"
                            color: modelData.positive ? "#24d67d" : "#ef5656"
                            x: (maskEditor.width - analysisSource.paintedWidth) / 2 + modelData.x * analysisSource.paintedWidth / Math.max(1,studio.workflow.imageWidth) - 4
                            y: (maskEditor.height - analysisSource.paintedHeight) / 2 + modelData.y * analysisSource.paintedHeight / Math.max(1,studio.workflow.imageHeight) - 4
                        }
                    }
                    Rectangle {
                        visible: studio.workflow.promptBox.length === 4
                        x: visible ? (maskEditor.width - analysisSource.paintedWidth) / 2 + studio.workflow.promptBox[0] * analysisSource.paintedWidth / Math.max(1,studio.workflow.imageWidth) : 0
                        y: visible ? (maskEditor.height - analysisSource.paintedHeight) / 2 + studio.workflow.promptBox[1] * analysisSource.paintedHeight / Math.max(1,studio.workflow.imageHeight) : 0
                        width: visible ? (studio.workflow.promptBox[2]-studio.workflow.promptBox[0]) * analysisSource.paintedWidth / Math.max(1,studio.workflow.imageWidth) : 0
                        height: visible ? (studio.workflow.promptBox[3]-studio.workflow.promptBox[1]) * analysisSource.paintedHeight / Math.max(1,studio.workflow.imageHeight) : 0
                        color: "transparent"; border.color: "#b6ddff"; border.width: 2
                    }
                }
                Label { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; text: studio.workflow.activeIndex < 0 ? qsTr("红色：未分配给已勾选对象的区域；保留为空") : qsTr("原图参考 + 选中蒙版；这不是重建绘画"); color: "#ffffff"; padding: 8; background: Rectangle { color: "#c0202428" } }
            }
            Item {
                anchors.fill: nativeCanvas; clip: true
                visible: studio.hasReference && showReference.checked && studio.hasDocument && !reviewPanel.visible && studio.replayCount === studio.eventCount
                Image {
                    id: referencePreview
                    x: nativeCanvas.documentRect.x; y: nativeCanvas.documentRect.y
                    width: nativeCanvas.documentRect.width; height: nativeCanvas.documentRect.height
                    source: studio.referenceUrl; autoTransform: true; opacity: referenceOpacity.value
                    fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                }
            }
            Label { anchors.centerIn: parent; visible: !studio.hasDocument && !reviewPanel.visible; text: qsTr("导入图片进行分割审阅，或新建绘画工程"); color: "#bcbcc4"; font.pixelSize: 18 }
        }
        ScrollView {
            id: sidebar; objectName: "sidebar"
            Layout.minimumWidth: 280
            Layout.preferredWidth: 280
            Layout.maximumWidth: 280
            Layout.fillWidth: false
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
            width: sidebar.availableWidth
            spacing: 12
            Label { text: qsTr("图像 → 对象审阅"); color: "#f5f5f7"; font.pixelSize: 22 }
            Label { text: qsTr("SAM 3 · 按名称自动分割对象"); color: "#f5f5f7"; font.pixelSize: 17 }
            Label { text: qsTr("按背景到前景输入对象类别，逗号分隔。识别后进入原生蒙版审阅和绘画。"); color: "#b5b5bd"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            TextField { id: sam3Prompts; objectName: "sam3ObjectPrompts"; text: "wooden table, saucer, coffee cup, spoon"; Layout.fillWidth: true }
            Button { objectName: "chooseSam3CheckpointButton"; text: qsTr("选择 SAM 3 图像模型权重"); enabled: !studio.busy; onClicked: sam3CheckpointDialog.open() }
            Label { text: studio.workflow.sam3Checkpoint.toString().length ? qsTr("SAM 3 权重已选择") : qsTr("请选择 SAM 3 图像模型 .pt 权重"); color: "#b5b5bd" }
            Button { objectName: "runSam3Button"; text: qsTr("SAM 3 自动识别 / 分层"); enabled: !studio.busy; onClicked: sam3ImageDialog.open() }
            Button { objectName: "importSemanticButton"; text: qsTr("导入 SAM 3 分层结果"); enabled: !studio.busy; onClicked: sam3ManifestDialog.open() }
            Label { text: qsTr("本地模型需先设置 DRAWBORAD_SAM3_CHECKPOINT；不上传图像。"); color: "#a5a5ae"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button { objectName: "chooseModelButton"; text: qsTr("选择本地 SAM 模型包"); enabled: !studio.busy; onClicked: modelFolderDialog.open() }
            Button { objectName: "checkModelButton"; text: qsTr("检查模型与 CPU 推理"); enabled: !studio.busy && studio.workflow.modelPackage.toString().length > 0; onClicked: studio.workflow.checkModel() }
            Label { objectName: "modelReadinessStatus"; text: studio.workflow.modelState + " · " + studio.workflow.modelStatus; color: studio.workflow.modelReady ? "#a2dbbc" : "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("执行设备：CPU。检查包含真实编码器/解码器冒烟推理；不代表咖啡图质量或 GPU 验收。"); color: "#b5b5bd"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: studio.workflow.message; color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("当前验证范围：PNG/JPEG、RGB 8 位、最多 400 万像素；暂不支持半透明对象组。"); color: "#b5b5bd"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button { objectName: "cancelWorkerButton"; text: qsTr("取消后台任务"); visible: studio.busy; enabled: studio.busy; onClicked: studio.cancelWorker() }
            ProgressBar { objectName: "workerProgress"; visible: studio.workflow.busy; Layout.fillWidth: true; from: 0; to: Math.max(1, studio.workflow.progressTotal); value: studio.workflow.progressCompleted; indeterminate: studio.workflow.progressTotal === 0 }
            BusyIndicator { running: studio.workflow.busy; visible: running }
            Label { text: qsTr("候选可能是背景或物体部件。请自行命名、选取和合并；不会补画被遮挡内容。"); visible: studio.workflow.hasReview; color: "#c9d2d6"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Repeater {
                model: studio.workflow.candidates
                delegate: Basic.Frame {
                    required property var modelData
                    Layout.fillWidth: true
                    background: Rectangle { color: studio.workflow.activeIndex === modelData.index ? "#31566e" : "#29292c"; border.color: "#50616a" }
                    ColumnLayout {
                        width: parent.width
                        RowLayout {
                            Layout.fillWidth: true
                            Basic.CheckBox { checked: modelData.included; enabled: !studio.busy; onToggled: studio.workflow.includeCandidate(modelData.index, checked) }
                            Button { objectName: "reviewCandidate_" + modelData.index; text: modelData.score; Layout.fillWidth: true; onClicked: studio.workflow.selectCandidate(modelData.index) }
                        }
                        Basic.TextField {
                            objectName: "reviewName_" + modelData.index; text: modelData.name
                            Layout.fillWidth: true; implicitHeight: 34; padding: 6; color: "#f5f5f7"
                            background: Rectangle { color: "#20282d"; border.color: parent.activeFocus ? "#80c5d6" : "#62717a" }
                            enabled: !studio.busy; onEditingFinished: studio.workflow.renameCandidate(modelData.index, text)
                        }
                    }
                }
            }
            Label { visible: studio.workflow.hasReview; text: qsTr("勾选表示应用这次改动；未勾选的已有对象保留。提示只追加候选，不替换旧笔迹。"); color: "#c9d2d6"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button { text: qsTr("清除当前点/框提示"); visible: studio.workflow.hasReview; enabled: !studio.busy; onClicked: studio.workflow.clearPrompts() }
            Button { objectName: "subtractMasksButton"; text: qsTr("当前扣除另一勾选区域（两项）"); visible: studio.workflow.hasReview; enabled: !studio.busy; onClicked: studio.workflow.subtractIncluded() }
            Button { objectName: "backgroundCandidateButton"; text: qsTr("检查剩余区域作为背景候选"); visible: studio.workflow.hasReview; enabled: !studio.busy; onClicked: studio.workflow.addBackgroundCandidate() }
            ComboBox { id: maskTool; objectName: "maskTool"; visible: studio.workflow.hasReview; Layout.fillWidth: true; model: [qsTr("查看蒙版"), qsTr("拖矩形补充蒙版"), qsTr("拖矩形擦除蒙版"), qsTr("框选区域细化笔迹"), qsTr("包含点提示（绿色）"), qsTr("排除点提示（红色）"), qsTr("框选提示范围")] }
            Button { text: qsTr("查看未分配区域"); visible: studio.workflow.hasReview; enabled: !studio.busy; onClicked: studio.workflow.selectCandidate(-1) }
            Button { text: qsTr("合并恰好两个已勾选候选"); visible: studio.workflow.hasReview && !studio.workflow.editingExisting; enabled: !studio.busy; onClicked: studio.workflow.mergeIncluded() }
            Button { objectName: "undoReviewButton"; text: qsTr("撤销蒙版操作"); visible: studio.workflow.hasReview; enabled: !studio.busy; onClicked: studio.workflow.undo() }
            Button { objectName: "acceptReviewButton"; text: studio.workflow.editingExisting ? qsTr("应用蒙版修改") : qsTr("接受所选对象与绘制层"); visible: studio.workflow.hasReview; enabled: !studio.busy; onClicked: {
                if (studio.workflow.editingExisting) studio.workflow.accept()
                else acceptReviewDialog.open()
            } }
            Label { visible: studio.workflow.hasReview; text: qsTr("已接通对象阶段笔迹、框选细化、回放和分层 PSD。当前粗绘需要审阅：局部结果不代表整图保真，自然过程和 Photoshop 实机重开尚未验收。"); color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("Document"); color: "#f5f5f7"; font.pixelSize: 23 }
            Button { objectName: "relinkSourceButton"; text: qsTr("重新关联同一原图"); enabled: studio.workflow.editingExisting && !studio.busy && !studio.workflow.pending; onClicked: relinkDialog.open() }
            Label { text: qsTr("笔迹回放（数量）"); color: "#b5b5bd"; visible: studio.eventCount > 0 }
            Slider { objectName: "replaySlider"; from: 0; to: studio.eventCount; stepSize: 1; value: studio.replayCount; visible: studio.eventCount > 0; Layout.fillWidth: true; onMoved: studio.setReplayCount(Math.round(value)) }
            Label { objectName: "replayPosition"; text: studio.replayCount + " / " + studio.eventCount; color: "#b5b5bd"; visible: studio.eventCount > 0 }
            RowLayout {
                Button { text: qsTr("从空白回放"); enabled: studio.eventCount > 0 && !studio.busy; onClicked: { studio.setReplayCount(0); replayTimer.start(); workspaceTabs.currentIndex = 1 } }
                Button { text: qsTr("全部笔迹"); onClicked: { replayTimer.stop(); studio.setReplayCount(studio.eventCount) } }
            }
            RowLayout {
                Button { objectName: "undoDrawingButton"; text: qsTr("撤销绘画"); enabled: !studio.busy && !studio.workflow.pending; onClicked: studio.undoDrawing() }
                Button { objectName: "redoDrawingButton"; text: qsTr("重做绘画"); enabled: !studio.busy && !studio.workflow.pending; onClicked: studio.redoDrawing() }
            }
            Label { text: studio.dimensions; color: "#b5b5bd"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Layers"); color: "#f5f5f7"; font.pixelSize: 18; Layout.fillWidth: true }
                Button { objectName: "addLayerButton"; text: qsTr("Add layer"); enabled: studio.canAddLayer; onClicked: studio.addLayer() }
            }
            ListView {
                objectName: "layerList"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(240, count * 48)
                clip: true
                model: studio.layers
                delegate: Basic.ItemDelegate {
                    required property var modelData
                    objectName: "layerRow_" + modelData.id
                    width: ListView.view.width
                    height: 48
                    highlighted: studio.activeLayerId === modelData.id
                    onClicked: studio.selectLayer(modelData.id)
                    background: Rectangle {
                        color: parent.highlighted ? "#31566e" : parent.hovered ? "#38383e" : "#29292c"
                        border.color: parent.highlighted ? "#80b5d6" : "#404046"
                    }
                    contentItem: RowLayout {
                        Label {
                            text: modelData.name + (modelData.locked ? qsTr(" (locked)") : "")
                            color: modelData.supported ? "#f5f5f7" : "#b5b5bd"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Basic.CheckBox {
                            objectName: "layerVisibility_" + modelData.id
                            text: qsTr("Visible")
                            checked: modelData.visible
                            onToggled: studio.setLayerVisible(modelData.id, checked)
                            contentItem: Label {
                                text: parent.text
                                font: parent.font
                                color: "#f5f5f7"
                                leftPadding: parent.indicator.width + parent.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar { }
            }
            Button { objectName: "importLineartButton"; text: qsTr("空白工程 · 嵌入原线稿"); enabled: !studio.busy; onClicked: lineartDialog.open() }
            CheckBox { objectName: "lineartBucketTool"; text: qsTr("原线稿填色桶（鼠标点击）"); checked: nativeCanvas.bucketTool; onToggled: nativeCanvas.bucketTool = checked }
            Label { text: qsTr("同尺寸黑白灰 PNG，最多 2000 万像素；原图锁定、色块独立、后台填色，不缩图。导入仍为同步操作。"); color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button { objectName: "cancelLineartFill"; text: qsTr("取消正在计算的色块"); visible: studio.fillBusy; onClicked: studio.cancelFill() }
            Label { objectName: "lineartFillNotice"; text: studio.fillNotice; color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: studio.layerNotice; color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
            Label { text: qsTr("画笔 · 原生笔迹"); color: "#f5f5f7"; font.pixelSize: 18 }
            Label { text: qsTr("宽度：") + nativeCanvas.brushWidth.toFixed(1) + " px"; color: "#bcbcc4" }
            Slider { objectName: "brushWidthSlider"; from: 0.5; to: 128; stepSize: 0.5; value: nativeCanvas.brushWidth; Layout.fillWidth: true; onMoved: nativeCanvas.configureBrush(value,nativeCanvas.brushColor,nativeCanvas.brushOpacity,nativeCanvas.brushHardness,nativeCanvas.mouseTaper) }
            RowLayout {
                Rectangle { width: 24; height: 24; color: nativeCanvas.brushColor; border.color: "#aaaaaa" }
                TextField { objectName: "brushColorInput"; text: nativeCanvas.brushColor.toString(); placeholderText: "#202020"; Layout.fillWidth: true; onEditingFinished: { if (!nativeCanvas.configureBrush(nativeCanvas.brushWidth,text,nativeCanvas.brushOpacity,nativeCanvas.brushHardness,nativeCanvas.mouseTaper)) text = nativeCanvas.brushColor.toString() } }
            }
            Label { text: qsTr("不透明度：") + Math.round(nativeCanvas.brushOpacity*100) + "%"; color: "#bcbcc4" }
            Slider { objectName: "brushOpacitySlider"; from: 0.01; to: 1; value: nativeCanvas.brushOpacity; Layout.fillWidth: true; onMoved: nativeCanvas.configureBrush(nativeCanvas.brushWidth,nativeCanvas.brushColor,value,nativeCanvas.brushHardness,nativeCanvas.mouseTaper) }
            Label { text: qsTr("硬度：") + Math.round(nativeCanvas.brushHardness*100) + "%"; color: "#bcbcc4" }
            Slider { objectName: "brushHardnessSlider"; from: 0; to: 1; value: nativeCanvas.brushHardness; Layout.fillWidth: true; onMoved: nativeCanvas.configureBrush(nativeCanvas.brushWidth,nativeCanvas.brushColor,nativeCanvas.brushOpacity,value,nativeCanvas.mouseTaper) }
            Label { text: qsTr("鼠标收笔辅助：") + Math.round(nativeCanvas.mouseTaper*100) + "%"; color: "#bcbcc4" }
            Slider { objectName: "mouseTaperSlider"; from: 0; to: 0.5; stepSize: 0.05; value: nativeCanvas.mouseTaper; Layout.fillWidth: true; onMoved: nativeCanvas.configureBrush(nativeCanvas.brushWidth,nativeCanvas.brushColor,nativeCanvas.brushOpacity,nativeCanvas.brushHardness,value) }
            Label { text: qsTr("0 为关闭；辅助只改变鼠标笔画首尾压力，不替代数位笔真实压感。设置对下一笔生效。"); color: "#bcbcc4"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: nativeCanvas.brushNotice; visible: text.length > 0; color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("Canvas view"); color: "#f5f5f7"; font.pixelSize: 18 }
            RowLayout {
                enabled: studio.hasDocument && !reviewPanel.visible
                Button { objectName: "zoomOutButton"; text: "−"; Layout.preferredWidth: 42; onClicked: nativeCanvas.zoomAt(0.8, nativeCanvas.width / 2, nativeCanvas.height / 2) }
                Label { objectName: "canvasZoom"; text: Math.round(nativeCanvas.zoom * 100) + "%"; color: "#b5b5bd"; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                Button { objectName: "zoomInButton"; text: "+"; Layout.preferredWidth: 42; onClicked: nativeCanvas.zoomAt(1.25, nativeCanvas.width / 2, nativeCanvas.height / 2) }
                Button { objectName: "fitCanvasButton"; text: qsTr("Fit"); Layout.preferredWidth: 52; onClicked: nativeCanvas.fitDocument() }
            }
            RowLayout {
                enabled: studio.hasDocument && !reviewPanel.visible
                Button { text: "←"; Layout.preferredWidth: 46; onClicked: nativeCanvas.panBy(-40, 0) }
                Button { text: "↑"; Layout.preferredWidth: 46; onClicked: nativeCanvas.panBy(0, -40) }
                Button { text: "↓"; Layout.preferredWidth: 46; onClicked: nativeCanvas.panBy(0, 40) }
                Button { text: "→"; Layout.preferredWidth: 46; onClicked: nativeCanvas.panBy(40, 0) }
            }
            Label { text: qsTr("滚轮缩放；鼠标中键拖动画布。百分比以适合窗口为基准。"); color: "#b5b5bd"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("Alpha：自动分割与人工审阅验证中"); color: "#d9c59e"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("Reference image is preview-only; .draw saves paint events, not the imported source image."); color: "#d6b1a6"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Switch { id: showReference; text: qsTr("Show reference overlay"); checked: true; enabled: studio.hasReference }
            Label { text: qsTr("Reference opacity"); color: "#b5b5bd"; visible: studio.hasReference }
            Slider { id: referenceOpacity; from: 0; to: 0.75; value: 0.35; enabled: studio.hasReference; visible: studio.hasReference; Layout.fillWidth: true }
            Label { text: studio.referenceFeedback; color: "#dbba98"; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: studio.referenceFeedback.length > 0 }
            Label { text: qsTr("Reference could not be displayed."); color: "#ffc2a1"; visible: studio.hasReference && referencePreview.status === Image.Error }
            Label { text: qsTr("This build includes the native tiled canvas, mouse/tablet stroke input, replay core, DPI coordinates and cache recovery foundation."); color: "#a5a5ae"; wrapMode: Text.Wrap; Layout.fillWidth: true; lineHeight: 1.45 }
            Button { text: qsTr("Check native worker"); enabled: studio.hasDocument && !studio.busy; onClicked: studio.probeWorker() }
            BusyIndicator { running: studio.busy; visible: studio.busy }
            Label { text: studio.message; color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }
    FolderDialog { id: modelFolderDialog; title: qsTr("选择含 manifest.json / encoder.onnx / decoder.onnx 的本地模型包"); onAccepted: studio.workflow.setModelPackage(selectedFolder) }
    FileDialog { id: sam3CheckpointDialog; title: qsTr("选择本地 SAM 3 图像模型 .pt 检查点"); nameFilters: ["PyTorch checkpoint (*.pt)"]; onAccepted: studio.workflow.setSam3Checkpoint(selectedFile) }
    FileDialog { id: sam3ImageDialog; title: qsTr("选择要由 SAM 3 识别的图像"); nameFilters: ["PNG/JPEG (*.png *.jpg *.jpeg)"]; onAccepted: { workspaceTabs.currentIndex = 0; studio.workflow.startSam3Image(selectedFile, sam3Prompts.text) } }
    FileDialog { id: sam3ManifestDialog; title: qsTr("导入 SAM 3 分层结果 manifest.json"); nameFilters: ["JSON manifest (*.json)"]; onAccepted: { workspaceTabs.currentIndex = 0; studio.workflow.importSemanticManifest(selectedFile) } }
    FileDialog { id: analysisImageDialog; title: qsTr("导入图片进行真实对象分割与审阅"); nameFilters: ["PNG/JPEG (*.png *.jpg *.jpeg)"]; onAccepted: { workspaceTabs.currentIndex = 0; studio.workflow.startImage(selectedFile) } }
    Dialog {
        id: replaceReviewDialog; anchors.centerIn: parent; modal: true; title: qsTr("保留还是替换当前审阅？"); standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: qsTr("新分析成功后会替换尚未接受的审阅。失败或取消不会改变当前绘画工程。"); wrapMode: Text.Wrap }
        onAccepted: analysisImageDialog.open()
    }
    Dialog {
        id: acceptReviewDialog; anchors.centerIn: parent; modal: true; title: qsTr("创建对象工程"); standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: qsTr("用勾选的真实蒙版创建新对象工程？当前绘画将被替换，请先保存要保留的笔迹。新工程只有蒙版和空绘制层，尚未重建原图。"); wrapMode: Text.Wrap; width: 400 }
        onAccepted: studio.workflow.accept()
    }
    Dialog {
        id: newDialog; objectName: "newDocumentDialog"; anchors.centerIn: parent; title: qsTr("New document"); modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            Label { text: qsTr("Dimensions in pixels / RGB 8-bit"); font.pixelSize: 14 }
            TextField { id: widthInput; objectName: "documentWidth"; text: "2048"; placeholderText: qsTr("Width"); validator: DoubleValidator { bottom: 1 } }
            TextField { id: heightInput; objectName: "documentHeight"; text: "2048"; placeholderText: qsTr("Height"); validator: DoubleValidator { bottom: 1 } }
            Label { text: qsTr("Unsaved work will be replaced."); visible: studio.dirty }
        }
        onAccepted: studio.createDocument(Number(widthInput.text), Number(heightInput.text), 300)
    }
    Dialog {
        id: replaceDialog; anchors.centerIn: parent; modal: true; title: qsTr("Unsaved document")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: qsTr("Discard unsaved changes and choose another project?") }
        onAccepted: openDialog.open()
    }
    Dialog {
        id: replaceWithReferenceDialog; anchors.centerIn: parent; modal: true
        title: qsTr("Replace current document?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: qsTr("A reference with matching dimensions keeps the current strokes. A different size creates a new document and discards unsaved work."); wrapMode: Text.Wrap }
        onAccepted: referenceDialog.open()
    }
    FileDialog { id: referenceDialog; title: qsTr("Import a PNG/JPEG reference (preview only)"); nameFilters: ["PNG/JPEG (*.png *.jpg *.jpeg)"]; onAccepted: studio.importReferenceUrl(selectedFile) }
    FileDialog { id: openDialog; title: qsTr("Open a Drawborad project"); nameFilters: ["Drawborad (*.draw)"]; onAccepted: studio.openUrl(selectedFile) }
    FileDialog {
        id: saveDialog; title: qsTr("Save a Drawborad project"); fileMode: FileDialog.SaveFile; defaultSuffix: "draw"; nameFilters: ["Drawborad (*.draw)"]
        onAccepted: {
            const finishClose = window.closeAfterSave
            window.closeAfterSave = false
            studio.saveUrl(selectedFile)
            if (finishClose && !studio.dirty) window.close()
        }
        onRejected: window.closeAfterSave = false
    }
    Dialog {
        id: closeDialog; anchors.centerIn: parent; modal: true; title: qsTr("Unsaved document")
        ColumnLayout {
            Label { text: qsTr("Save before closing, or explicitly discard changes.") }
            RowLayout {
                Button { text: qsTr("Cancel"); onClicked: closeDialog.close() }
                Button { text: qsTr("Save"); onClicked: { window.closeAfterSave = true; closeDialog.close(); saveDialog.open() } }
                Button { text: qsTr("Discard"); onClicked: { window.approvedClose = true; window.close() } }
            }
        }
    }
}

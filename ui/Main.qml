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
    FileDialog { id: originalReferenceDialog; title: qsTr("附加原彩到工程（分享 .draw 将包含此图）"); nameFilters: ["PNG/JPEG (*.png *.jpg *.jpeg)"]; onAccepted: studio.attachOriginalReferenceUrl(selectedFile) }
    Window {
        id: originalReferenceWindow; objectName: "originalReferenceWindow"; title: qsTr("原彩参考 · 只读对照，不是作品输出")
        width: 600; height: 800; visible: false; color: "#29292c"
        property var picked: ({ valid: false })
        property bool fitMode: true
        property real customScale: 1
        property real panX: 0
        property real panY: 0
        readonly property real imageWidth: originalReferenceImage.sourceSize.width
        readonly property real imageHeight: originalReferenceImage.sourceSize.height
        readonly property real fitScale: imageWidth > 0 && imageHeight > 0 && originalReferenceViewport.width > 0 && originalReferenceViewport.height > 0 ? Math.min(originalReferenceViewport.width / imageWidth, originalReferenceViewport.height / imageHeight) : 1
        readonly property real viewScale: fitMode ? fitScale : customScale
        property string referenceIdentity: studio.originalReferenceUrl.toString()
        onReferenceIdentityChanged: fitReference()
        Connections { target: studio; function onChanged() { originalReferenceWindow.picked = { valid: false } } }
        function fitReference() {
            fitMode = true; panX = 0; panY = 0; picked = { valid: false }
        }
        function nativeReference() {
            customScale = 1; fitMode = false; panX = 0; panY = 0; picked = { valid: false }
        }
        function insideViewport(x, y) {
            return isFinite(x) && isFinite(y) && x >= 0 && y >= 0 && x < originalReferenceViewport.width && y < originalReferenceViewport.height
        }
        function zoomReference(factor, x, y) {
            if (!insideViewport(x,y) || !isFinite(factor) || factor <= 0 || originalReferenceImage.status !== Image.Ready) return
            const old = viewScale
            if (!isFinite(old) || old <= 0) return
            const ix = (x-originalReferenceImage.x)/old, iy = (y-originalReferenceImage.y)/old
            // A tiny image can fit at more than 64x. Never make zoom-in
            // jump backwards merely because that fit exceeded the usual cap.
            const maxZoom = Math.max(64,fitScale,old)
            if (!isFinite(maxZoom)) return
            const next = Math.max(Math.min(0.0001,old),Math.min(maxZoom,old*factor))
            const nextX = x-(originalReferenceViewport.width-imageWidth*next)/2-ix*next
            const nextY = y-(originalReferenceViewport.height-imageHeight*next)/2-iy*next
            if (!isFinite(next) || !isFinite(nextX) || !isFinite(nextY) || Math.abs(nextX)>1000000 || Math.abs(nextY)>1000000) return
            customScale = next; fitMode = false; panX = nextX; panY = nextY
            picked = { valid: false }
        }
        function panReference(dx, dy) {
            if (!isFinite(dx) || !isFinite(dy) || originalReferenceImage.status !== Image.Ready) return
            const old = viewScale
            if (!isFinite(old) || old <= 0) return
            const nextX = panX+dx, nextY = panY+dy
            if (!isFinite(nextX) || !isFinite(nextY) || Math.abs(nextX)>1000000 || Math.abs(nextY)>1000000) return
            customScale = old; fitMode = false; panX = nextX; panY = nextY; picked = { valid: false }
        }
        function pickAt(x, y) {
            if (!insideViewport(x,y) || originalReferenceImage.status !== Image.Ready || !isFinite(viewScale) || viewScale <= 0) { picked = { valid: false }; return }
            if (x < originalReferenceImage.x || y < originalReferenceImage.y || x >= originalReferenceImage.x+originalReferenceImage.width || y >= originalReferenceImage.y+originalReferenceImage.height) { picked = { valid: false }; return }
            const ix = (x-originalReferenceImage.x)/viewScale, iy = (y-originalReferenceImage.y)/viewScale
            if (ix < 0 || iy < 0 || ix >= imageWidth || iy >= imageHeight) { picked = { valid: false }; return }
            picked = studio.sampleOriginalReference(referenceIdentity,ix,iy)
        }
        ColumnLayout { anchors.fill: parent; anchors.margins: 12
            Label { text: studio.originalReferenceInfo; color: "white"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Button { objectName: "referenceFitButton"; text: qsTr("适配"); onClicked: originalReferenceWindow.fitReference() }
                Button { objectName: "referenceNativeButton"; text: qsTr("1原像素 = 1逻辑像素"); onClicked: originalReferenceWindow.nativeReference() }
                Label { text: Math.round(originalReferenceWindow.viewScale*100)+"%"; color: "white" }
            }
            Item {
                id: originalReferenceViewport; objectName: "originalReferenceViewport"
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 32; clip: true
                Image {
                    id: originalReferenceImage; objectName: "originalReferenceImage"
                    source: studio.originalReferenceUrl; asynchronous: false; cache: false
                    // sourceSize is never changed by viewing. Only display geometry scales.
                    width: Math.max(0,sourceSize.width)*originalReferenceWindow.viewScale
                    height: Math.max(0,sourceSize.height)*originalReferenceWindow.viewScale
                    x: (originalReferenceViewport.width-width)/2+originalReferenceWindow.panX
                    y: (originalReferenceViewport.height-height)/2+originalReferenceWindow.panY
                    fillMode: Image.Stretch
                }
                MouseArea {
                    anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.MiddleButton; preventStealing: true
                    property real pressX: 0; property real pressY: 0
                    property real lastX: 0; property real lastY: 0
                    property bool moved: false
                    property string pressedSource: ""
                    onPressed: function(mouse) { pressX=lastX=mouse.x; pressY=lastY=mouse.y; moved=false; pressedSource=originalReferenceWindow.referenceIdentity }
                    onPositionChanged: function(mouse) {
                        if (!pressed || pressedSource !== originalReferenceWindow.referenceIdentity) return
                        if (Math.hypot(mouse.x-pressX,mouse.y-pressY)>4) moved=true
                        if (pressedButtons & Qt.MiddleButton) originalReferenceWindow.panReference(mouse.x-lastX,mouse.y-lastY)
                        lastX=mouse.x; lastY=mouse.y
                    }
                    onReleased: function(mouse) { if (mouse.button===Qt.LeftButton && !moved && pressedSource===originalReferenceWindow.referenceIdentity) originalReferenceWindow.pickAt(mouse.x,mouse.y) }
                    onCanceled: { moved=true; pressedSource="" }
                    onWheel: function(wheel) {
                        const delta=wheel.angleDelta.y!==0 ? wheel.angleDelta.y : wheel.pixelDelta.y
                        if (delta!==0) originalReferenceWindow.zoomReference(Math.pow(1.0015,delta),wheel.x,wheel.y)
                        wheel.accepted=true
                    }
                }
            }
            RowLayout {
                Rectangle { width: 28; height: 28; color: originalReferenceWindow.picked.valid ? originalReferenceWindow.picked.color : "transparent"; border.color: "#aaaaaa" }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "white"; text: originalReferenceWindow.picked.valid ? qsTr("原像素 (%1,%2) · %3 · 源 alpha %4%").arg(originalReferenceWindow.picked.x).arg(originalReferenceWindow.picked.y).arg(originalReferenceWindow.picked.hex).arg(Math.round(originalReferenceWindow.picked.alpha*100)) : qsTr("点击原彩预选单像素颜色；不自动填色") }
            }
            Button { objectName: "applyReferenceColor"; text: qsTr("应用 RGB 到画笔/填色（保留工具不透明度）"); Layout.fillWidth: true
                enabled: originalReferenceWindow.picked.valid && originalReferenceWindow.picked.alpha > 0 && originalReferenceWindow.picked.source === studio.originalReferenceUrl.toString() && !studio.busy
                onClicked: nativeCanvas.configureBrush(nativeCanvas.brushWidth,originalReferenceWindow.picked.color,nativeCanvas.brushOpacity,nativeCanvas.brushHardness,nativeCanvas.mouseTaper)
            }
            Label { text: qsTr("取解码原像素 RGB，不取屏幕插值色；源 alpha 仅展示，不复制。当前工具不透明度 %1%，叠色后不保证相同观感；无色彩配置时未校准。").arg(Math.round(nativeCanvas.brushOpacity*100)); color: "#e0c1a0"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("滚轮缩放，中键拖动，左键点击取色。1:1指逻辑像素，不承诺屏幕物理像素。仅改变显示，不重采样保存，不自动对齐作品。") ; color: "#e0c1a0"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    FileDialog { id: lineartDialog; title: qsTr("嵌入同尺寸黑白灰 PNG 线稿"); nameFilters: ["PNG (*.png)"]; onAccepted: studio.importLineartUrl(selectedFile) }
    FileDialog { id: pngExportDialog; title: qsTr("导出原尺寸 RGB PNG（新文件名）"); fileMode: FileDialog.SaveFile; defaultSuffix: "png"; nameFilters: ["PNG (*.png)"]; onAccepted: studio.workflow.exportNative(selectedFile, "png") }
    FileDialog { id: oraExportDialog; title: qsTr("导出叶图层 ORA（组展开、蒙版烘焙；新文件名）"); fileMode: FileDialog.SaveFile; defaultSuffix: "ora"; nameFilters: ["OpenRaster (*.ora)"]; onAccepted: studio.workflow.exportNative(selectedFile, "ora") }
    FileDialog { id: psdDialog; title: qsTr("导出笔迹分层 PSD（请选择新文件名）"); fileMode: FileDialog.SaveFile; defaultSuffix: "psd"; nameFilters: ["Photoshop (*.psd)"]; onAccepted: studio.workflow.exportPsd(selectedFile) }
    FileDialog { id: relinkDialog; title: qsTr("选择与原始文件哈希一致的图片"); nameFilters: ["PNG/JPEG (*.png *.jpg *.jpeg)"]; onAccepted: studio.workflow.relinkSource(selectedFile) }
    Dialog { id: fillPendingDialog; title: qsTr("操作尚未提交"); modal: true; standardButtons: Dialog.Ok; Label { text: qsTr("请先接受或取消曲线草稿、笔迹或填色，保存后再关闭。"); wrapMode: Text.Wrap } }
    onClosing: function(event) {
        if (studio.cubicDraftActive || studio.strokeCommitBusy || studio.fillBusy || studio.fillPending) { event.accepted = false; fillPendingDialog.open(); return }
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
            NativeCanvas { id: nativeCanvas; objectName: "nativeCanvas"; anchors.fill: parent; anchors.margins: 24; anchors.topMargin: workspaceTabs.visible ? 64 : 24; visible: studio.hasDocument && !reviewPanel.visible; enabled: !strokeEditMode.checked && !studio.cubicDraftActive; bridge: studio }
            Canvas {
                id: strokeEditOverlay; objectName: "strokeEditOverlay"
                x: nativeCanvas.x; y: nativeCanvas.y; width: nativeCanvas.width; height: nativeCanvas.height; z: 4
                visible: nativeCanvas.visible && strokeEditMode.checked && !studio.busy; clip: true
                property int selectedIndex: -1
                property var handles: nativeCanvas.localStrokeHandles
                function choose(x,y) {
                    selectedIndex = -1
                    let best = 100 // ten logical pixels; never silently choose a distant point
                    if (!Number.isFinite(x) || !Number.isFinite(y) || x<0 || y<0 || x>=width || y>=height) return
                    for (let i=0;i<handles.length;i++) {
                        const p=handles[i]
                        if(!Number.isFinite(p.x)||!Number.isFinite(p.y)||p.x<0||p.y<0||p.x>=width||p.y>=height)continue
                        const d=(p.x-x)*(p.x-x)+(p.y-y)*(p.y-y)
                        if (d<best) { best=d; selectedIndex=i }
                    }
                    editDx.text="0";editDy.text="0"; requestPaint()
                }
                onSelectedIndexChanged: requestPaint()
                onHandlesChanged: requestPaint()
                onPaint: {
                    const c=getContext("2d");c.clearRect(0,0,width,height)
                    c.fillStyle="#5dd6ff"
                    for(let i=0;i<handles.length;i++){const p=handles[i];c.beginPath();c.arc(p.x,p.y,i===selectedIndex?5:2,0,Math.PI*2);c.fill()}
                }
                Connections { target: nativeCanvas; function onViewChanged(){strokeEditOverlay.requestPaint()} }
                Connections { target: studio; function onChanged(){strokeEditOverlay.selectedIndex=-1;strokeEditOverlay.requestPaint()} }
                MouseArea {
                    objectName: "strokeEditPicker"; anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    property real pressX: 0; property real pressY: 0; property real lastX: 0; property real lastY: 0
                    enabled: !studio.busy
                    onPressed: function(mouse){pressX=lastX=mouse.x;pressY=lastY=mouse.y;if(mouse.button===Qt.LeftButton)strokeEditOverlay.choose(mouse.x,mouse.y)}
                    onPositionChanged: function(mouse){if(pressedButtons & Qt.MiddleButton){nativeCanvas.panBy(mouse.x-lastX,mouse.y-lastY);lastX=mouse.x;lastY=mouse.y}}
                    onWheel: function(wheel){nativeCanvas.zoomAt(wheel.angleDelta.y>0?1.25:.8,wheel.x,wheel.y);wheel.accepted=true}
                    onReleased: function(mouse){
                        const i=strokeEditOverlay.selectedIndex, p=strokeEditOverlay.handles[i]
                        if(mouse.button===Qt.LeftButton && Math.hypot(mouse.x-pressX,mouse.y-pressY)>4 && i>=0 && p && p.scale>0 && mouse.x>=0 && mouse.y>=0 && mouse.x<width && mouse.y<height){
                            editDx.text=String(Math.round((mouse.x-p.x)/p.scale*100)/100)
                            editDy.text=String(Math.round((mouse.y-p.y)/p.scale*100)/100)
                        }
                    }
                    onCanceled: { strokeEditOverlay.selectedIndex=-1 }
                }
            }
            Canvas {
                id: cubicOverlay; objectName: "cubicOverlay"
                x:nativeCanvas.x;y:nativeCanvas.y;width:nativeCanvas.width;height:nativeCanvas.height;z:6;clip:true
                visible:nativeCanvas.visible && studio.cubicDraftActive
                property var handles:nativeCanvas.cubicHandles
                property int dragIndex:-1
                property real startX:0
                property real startY:0
                focus:visible
                Keys.onEscapePressed: { dragIndex=-1;studio.cancelCubic() }
                onHandlesChanged:requestPaint()
                onVisibleChanged:{abandonDrag();requestPaint();if(visible)forceActiveFocus()}
                function abandonDrag(){if(dragIndex>=0)studio.moveCubicControl(dragIndex,startX,startY);dragIndex=-1}
                onWidthChanged:abandonDrag()
                onHeightChanged:abandonDrag()
                onPaint:{
                    const c=getContext("2d");c.clearRect(0,0,width,height);const p=handles;if(p.length<4 && !studio.cubicPathDraft)return;
                    const spans=studio.cubicPathDraft?(studio.cubicPathClosed?p.length/3:(p.length-1)/3):1;
                    c.strokeStyle="#efbc52";c.lineWidth=1;c.beginPath();
                    for(let n=0;n<spans;n++){const i=n*3,j=(i+3)%p.length;c.moveTo(p[i].x,p[i].y);c.lineTo(p[i+1].x,p[i+1].y);c.moveTo(p[i+2].x,p[i+2].y);c.lineTo(p[j].x,p[j].y)}c.stroke();
                    if(!studio.fillPending&&p.length){c.strokeStyle="#5dd6ff";c.beginPath();c.moveTo(p[0].x,p[0].y);for(let n=0;n<spans;n++){const i=n*3,j=(i+3)%p.length;c.bezierCurveTo(p[i+1].x,p[i+1].y,p[i+2].x,p[i+2].y,p[j].x,p[j].y)}c.stroke();}
                    for(let i=0;i<p.length;i++){c.fillStyle=i%3===0?"#5dd6ff":"#efbc52";c.beginPath();c.arc(p[i].x,p[i].y,6,0,Math.PI*2);c.fill();c.fillText(String(i+1),p[i].x+9,p[i].y-8)}
                }
                Connections { target:studio;function onChanged(){cubicOverlay.requestPaint()} }
                Connections { target:nativeCanvas;function onViewChanged(){cubicOverlay.requestPaint()} }
                Connections { target:window;function onActiveChanged(){if(!window.active)cubicOverlay.abandonDrag()} }
                MouseArea {
                    objectName:"cubicHandleDrag";anchors.fill:parent;acceptedButtons:Qt.LeftButton | Qt.MiddleButton
                    enabled:!studio.fillBusy
                    property real lastX:0;property real lastY:0
                    onPressed:function(mouse){
                        lastX=mouse.x;lastY=mouse.y;cubicOverlay.dragIndex=-1;
                        if(mouse.button!==Qt.LeftButton||studio.fillPending)return;
                        let distance=100;const p=cubicOverlay.handles;
                        for(let i=0;i<p.length;i++){const d=Math.pow(p[i].x-mouse.x,2)+Math.pow(p[i].y-mouse.y,2);if(d<distance){distance=d;cubicOverlay.dragIndex=i}}
                        if(cubicOverlay.dragIndex>=0){const source=studio.cubicControls[cubicOverlay.dragIndex];cubicOverlay.startX=source.x;cubicOverlay.startY=source.y}
                    }
                    onPositionChanged:function(mouse){
                        if(pressedButtons & Qt.MiddleButton){cubicOverlay.abandonDrag();nativeCanvas.panBy(mouse.x-lastX,mouse.y-lastY);lastX=mouse.x;lastY=mouse.y;return}
                        if((pressedButtons & Qt.LeftButton)&&cubicOverlay.dragIndex>=0)nativeCanvas.moveCubicHandle(cubicOverlay.dragIndex,mouse.x,mouse.y)
                    }
                    onReleased:function(mouse){if(mouse.button===Qt.LeftButton&&cubicOverlay.dragIndex>=0){const index=cubicOverlay.dragIndex;cubicOverlay.dragIndex=-1;if(mouse.x>=0&&mouse.y>=0&&mouse.x<width&&mouse.y<height){if(nativeCanvas.moveCubicHandle(index,mouse.x,mouse.y))studio.previewCubic()}else studio.moveCubicControl(index,cubicOverlay.startX,cubicOverlay.startY)}}
                    onCanceled:{if(cubicOverlay.dragIndex>=0)studio.moveCubicControl(cubicOverlay.dragIndex,cubicOverlay.startX,cubicOverlay.startY);cubicOverlay.dragIndex=-1}
                    onWheel:function(wheel){cubicOverlay.abandonDrag();nativeCanvas.zoomAt(wheel.angleDelta.y>0?1.25:.8,wheel.x,wheel.y);wheel.accepted=true}
                }
            }
            Canvas { id: brushFootprintOverlay; objectName:"brushFootprintOverlay";x:nativeCanvas.x;y:nativeCanvas.y;width:nativeCanvas.width;height:nativeCanvas.height;z:3;clip:true;visible:nativeCanvas.visible && nativeCanvas.enabled
                onPaint:{const c=getContext("2d");c.clearRect(0,0,width,height);const p=nativeCanvas.brushFootprint;if(p.length!==64)return;let cx=0,cy=0;c.beginPath();for(let i=0;i<p.length;i++){cx+=p[i].x;cy+=p[i].y;if(i===0)c.moveTo(p[i].x,p[i].y);else c.lineTo(p[i].x,p[i].y);}c.closePath();c.lineWidth=3;c.strokeStyle="#ffffff";c.stroke();c.lineWidth=1;c.strokeStyle="#171717";c.stroke();cx/=p.length;cy/=p.length;c.beginPath();c.moveTo(cx-3,cy);c.lineTo(cx+3,cy);c.moveTo(cx,cy-3);c.lineTo(cx,cy+3);c.lineWidth=3;c.strokeStyle="#ffffff";c.stroke();c.lineWidth=1;c.strokeStyle="#171717";c.stroke();}
                Connections {target:nativeCanvas;function onFootprintChanged(){brushFootprintOverlay.requestPaint()}}
                Connections {target:studio;function onChanged(){brushFootprintOverlay.requestPaint()}}
                onWidthChanged:requestPaint();onHeightChanged:requestPaint();onVisibleChanged:requestPaint()
            }
            Rectangle { objectName: "hollowBoundsOverlay"; x: nativeCanvas.x + nativeCanvas.hollowItemRect.x; y: nativeCanvas.y + nativeCanvas.hollowItemRect.y; width: nativeCanvas.hollowItemRect.width; height: nativeCanvas.hollowItemRect.height; visible: nativeCanvas.visible && nativeCanvas.hollowTool && (nativeCanvas.hollowBounded || nativeCanvas.hollowDragActive) && nativeCanvas.hollowLassoPoints.length === 0; color: "transparent"; border.color: "#ffcf70"; border.width: 2; z: 2 }
            Canvas { id: lassoGuide; objectName: "hollowLassoGuide"; x: nativeCanvas.x; y: nativeCanvas.y; width: nativeCanvas.width; height: nativeCanvas.height; z: 2; visible: nativeCanvas.visible && nativeCanvas.hollowTool && nativeCanvas.hollowLassoPoints.length > 0
                onPaint: { const c = getContext("2d"); c.clearRect(0,0,width,height); const p = nativeCanvas.hollowLassoPoints; if (p.length < 2) return; c.strokeStyle = "#ffcf70"; c.lineWidth = 2; c.beginPath(); c.moveTo(p[0].x,p[0].y); for (let i=1;i<p.length;i++) c.lineTo(p[i].x,p[i].y); c.closePath(); c.stroke() }
                Connections { target: nativeCanvas; function onViewChanged() { lassoGuide.requestPaint() } function onBrushSettingsChanged() { lassoGuide.requestPaint() } }
            }
            Canvas { id: gapGuide; objectName: "gapGuide"; x: nativeCanvas.x; y: nativeCanvas.y; width: nativeCanvas.width; height: nativeCanvas.height; z: 2; visible: nativeCanvas.visible && nativeCanvas.closeGapTool && nativeCanvas.closeGapGuide.length === 2
                onPaint: { const c=getContext("2d"); c.clearRect(0,0,width,height); const p=nativeCanvas.closeGapGuide;if(p.length!==2)return;c.strokeStyle="#ffcf70";c.lineWidth=2;c.beginPath();c.moveTo(p[0].x,p[0].y);c.lineTo(p[1].x,p[1].y);c.stroke() }
                Connections { target: nativeCanvas; function onViewChanged() { gapGuide.requestPaint() } function onBrushSettingsChanged() { gapGuide.requestPaint() } }
            }
            Label { objectName: "fillPreviewStatus"; anchors.top: nativeCanvas.top; anchors.horizontalCenter: nativeCanvas.horizontalCenter; visible: studio.fillPending; text: qsTr("操作预览 · 尚未提交 · 请接受或取消"); color: "#ffcf70"; z: 3 }
            Label { objectName: "strokePreviewStatus"; anchors.bottom: nativeCanvas.bottom; anchors.horizontalCenter: nativeCanvas.horizontalCenter; visible: nativeCanvas.strokePreviewActive; text: nativeCanvas.myPaintTool ? qsTr("MyPaint 输入记录中 · 无实时墨迹 · 松手计算预览，Esc 取消") : nativeCanvas.previewing ? qsTr("笔迹预览更新中 · 尚未提交") : qsTr("未提交预览 · 松手提交，Esc 取消"); color: "#ffffff"; padding: 6; background: Rectangle { color: "#c0202428" } }
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
            BusyIndicator { running: studio.workflow.busy && !studio.cubicDraftActive; visible: running }
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
            Button { objectName: "addFinishingLayerButton"; text: qsTr("新增手绘细节层（不作边界）"); enabled: studio.hasDocument && !studio.busy; Layout.fillWidth: true; onClicked: studio.addFinishingLayer() }
            Label { text: qsTr("在独立细节层画笔触，可继续给下面原线稿填色。默认不采样手绘线条；可明确指定一个细节层作边界，或用补线工具封口。"); wrapMode: Text.Wrap; color: "#dbba98"; Layout.fillWidth: true }
            ListView {
                objectName: "layerList"
                enabled: !studio.busy && !studio.workflow.pending
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
                            enabled: !studio.busy
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
            Button { objectName:"newCubicButton";text:qsTr("新建原生四点曲线草稿");enabled:studio.hasDocument && !studio.busy;Layout.fillWidth:true;onClicked:studio.beginCubicDraft(Math.max(.5,Math.min(128,nativeCanvas.brushWidth)),nativeCanvas.brushColor) }
            Button { objectName:"newCubicPathButton";text:qsTr("新建两段闭合轮廓（共享锚点）");enabled:studio.hasDocument && !studio.busy;Layout.fillWidth:true;onClicked:studio.beginCubicPathDraft(Math.max(.5,Math.min(128,nativeCanvas.brushWidth)),nativeCanvas.brushColor) }
            ComboBox { id:cubicSelector;objectName:"cubicSelector";Layout.fillWidth:true;model:studio.editableCubics;textRole:"name";enabled:!studio.busy;onActivated:studio.selectCubic(model[index].id) }
            Label { visible:studio.cubicDraftActive;text:studio.cubicPathDraft?qsTr("蓝色共享锚点、黄色独立手柄；细线仅几何指引。新稿可调全部点；已有轮廓本次只能改一个点，先接受或取消才能改另一点。闭合不保证可填色，填色后可能锁定轮廓编辑。中键平移、滚轮缩放。"):qsTr("蓝色端点1/4，黄色控制点2/3。细线仅几何指引，不是最终笔迹。拖柄松开才计算精确预览；接受或取消后才能继续。中键平移、滚轮缩放。");wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#edc79a" }
            Button { objectName:"previewCubicButton";text:qsTr("精确预览曲线");visible:studio.cubicDraftActive;enabled:!studio.fillBusy && !studio.fillPending;onClicked:studio.previewCubic() }
            Button { objectName:"previewSolidBakeButton";text:qsTr("预览为独立像素层");visible:studio.solidBakeAvailable && studio.cubicPathDraft && studio.cubicPathClosed && studio.cubicControls.length===6;enabled:!studio.fillBusy;onClicked:studio.previewCubicSolidBake() }
            Label { visible:studio.solidBakePreviewPending;text:qsTr("这是独立像素层预览。接受后该层不能调控制点；原轮廓和原图保持不变。");wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#edc79a" }
            Button { objectName:"acceptCubicButton";text:studio.solidBakePreviewPending?qsTr("接受像素层（不能再调控制点）"):qsTr("接受曲线（保留原控制点）");visible:studio.cubicPreviewPending;enabled:!nativeCanvas.rendering && nativeCanvas.renderError.length===0;onClicked:studio.acceptFill() }
            Button { objectName:"resumeCubicButton";text:qsTr("返回调柄（保留草稿）");visible:studio.cubicDraftActive && studio.fillPending;onClicked:studio.resumeCubicDraft() }
            Button { objectName:"cancelCubicButton";text:qsTr("取消曲线草稿（不改工程）");visible:studio.cubicDraftActive;onClicked:studio.cancelCubic() }
            Button { objectName: "importLineartButton"; text: qsTr("空白工程 · 嵌入原线稿"); enabled: !studio.busy; onClicked: lineartDialog.open() }
            CheckBox { objectName: "lineartBucketTool"; text: qsTr("原线稿填色桶（鼠标点击）"); checked: nativeCanvas.bucketTool; onToggled: nativeCanvas.bucketTool = checked }
            Button { objectName: "useActiveBoundaryButton"; text: qsTr("用所选手绘细节层作为填色边界"); enabled: studio.hasDocument && !studio.busy; Layout.fillWidth: true; onClicked: studio.useActiveBoundary() }
            Label { objectName:"fillProvenanceNotice"; text:studio.fillProvenanceNotice; wrapMode:Text.Wrap; Layout.fillWidth:true; color:"#edc79a" }
            Button { objectName:"closeStrokeGapButton"; text:qsTr("所选实墨首尾短缺口 · 闭合预览"); Layout.fillWidth:true; enabled:studio.hasDocument && !studio.busy && !studio.workflow.pending; onClicked:studio.previewCloseStrokeGap() }
            Button { objectName:"withdrawClosedInkFillButton"; text:qsTr("撤回最近已记录闭合填色组 · 先预览"); Layout.fillWidth:true; enabled:studio.hasDocument && !studio.busy && !studio.workflow.pending; onClicked:studio.previewWithdrawClosedInkFill() }
            Button { objectName:"closedInkFillButton"; text:qsTr("所选单笔闭合墨块 · 内区填充预览"); Layout.fillWidth:true; enabled:studio.hasDocument && !studio.busy && !studio.workflow.pending; onClicked:studio.previewClosedInkFill() }
            Label { text:qsTr("仅全白原稿、当前层一条不透明闭合实墨（最多256不同点，有效线宽至少1px），同墨色填几何内区。不会自动合拢开口；复杂/自交/过细线、已有底色重叠均拒绝。这不是任意线稿的自动白缝修复。");wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#edc79a" }
            Button { objectName: "clearBoundaryButton"; text: qsTr("停止采样手绘边界"); visible: studio.selectedBoundaryName.length > 0; enabled: !studio.busy; Layout.fillWidth: true; onClicked: studio.clearSelectedBoundary() }
            Label { objectName: "selectedBoundaryLabel"; visible: studio.selectedBoundaryName.length > 0; text: qsTr("边界层：") + studio.selectedBoundaryName + qsTr("。仅 alpha≥128/255 的覆盖阻挡；不改笔迹透明度、不采其他彩色细节。只填原稿白区，挖空区请停止采样后填。重开须重选。"); wrapMode: Text.Wrap; color: "#edc79a"; Layout.fillWidth: true }
            Label { text: qsTr("同尺寸黑白灰 PNG，最多 2000 万像素；原图锁定、色块独立、后台填色，不缩图。导入仍为同步操作。"); color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button { objectName: "cancelLineartFill"; text: qsTr("取消当前操作"); visible: studio.fillBusy || studio.fillPending; onClicked: studio.cancelFill() }
            Button { objectName: "acceptLineartFill"; text: studio.myPaintPreviewPending?qsTr("接受 MyPaint 独立笔层"):studio.solidBakePreviewPending?qsTr("接受独立像素层（不能再调控制点）"):qsTr("接受当前预览"); visible: studio.fillPending; enabled: !nativeCanvas.rendering && nativeCanvas.renderError.length === 0; onClicked: studio.acceptFill() }
            CheckBox { objectName: "closeGapTool"; text: qsTr("短断口补线（拖两个原墨端点）"); checked: nativeCanvas.closeGapTool; enabled: !studio.busy; onToggled: nativeCanvas.closeGapTool = checked }
            Label { text: qsTr("1像素黑线，最多64像素；两端须在原墨、跨过原白缺口，不自动找线。黄线仅指引，松开后看实际补线预览再接受；Esc/越界取消。原图不改，补线在可隐藏修正层，可撤销。"); color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            CheckBox { objectName: "hollowInkTool"; enabled: !studio.busy; text: qsTr("局部挖空（白纸，点墨块）"); checked: nativeCanvas.hollowTool; onToggled: nativeCanvas.hollowTool = checked }
            Label { text: qsTr("挖空只清除所选墨块内部，保留原轮廓和细笔触；可依次选择独立墨块，已挖区域不会重复覆盖；接受后切换填色桶可继续填内部或普通白区，自动分到上/下独立色层。不采旧颜色，不是中心线化或整页抽线。"); color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button { objectName: "dragHollowSelection"; text: nativeCanvas.hollowDragTool ? qsTr("结束框选模式") : qsTr("在画布拖拽框选范围"); enabled: !studio.busy; onClicked: nativeCanvas.hollowDragTool = !nativeCanvas.hollowDragTool }
            Button { objectName: "lassoHollowSelection"; text: nativeCanvas.hollowLassoTool ? qsTr("结束套索模式") : qsTr("拖拽自由套索范围"); enabled: !studio.busy; onClicked: nativeCanvas.hollowLassoTool = !nativeCanvas.hollowLassoTool }
            Connections { target: nativeCanvas; function onHollowSelectionFinished() { const r = nativeCanvas.hollowBounds; hollowX.text = String(r.x); hollowY.text = String(r.y); hollowW.text = String(r.width); hollowH.text = String(r.height) } }
            CheckBox { id: hollowLimit; objectName: "hollowLimit"; checked: nativeCanvas.hollowBounded; text: qsTr("限制挖空范围（原图像素）"); enabled: !studio.busy && !nativeCanvas.hollowDragTool; onToggled: nativeCanvas.hollowBounded = checked }
            GridLayout {
                columns: 2; Layout.fillWidth: true; enabled: !studio.busy && !nativeCanvas.hollowDragTool
                TextField { id: hollowX; objectName: "hollowX"; text: "0"; placeholderText: "X"; Layout.fillWidth: true }
                TextField { id: hollowY; objectName: "hollowY"; text: "0"; placeholderText: "Y"; Layout.fillWidth: true }
                TextField { id: hollowW; objectName: "hollowW"; text: "64"; placeholderText: qsTr("宽"); Layout.fillWidth: true }
                TextField { id: hollowH; objectName: "hollowH"; text: "64"; placeholderText: qsTr("高"); Layout.fillWidth: true }
            }
            Binding { target: nativeCanvas; property: "hollowBounds"; when: !nativeCanvas.hollowDragTool; value: Qt.rect(Number(hollowX.text.trim() || "NaN"),Number(hollowY.text.trim() || "NaN"),Number(hollowW.text.trim() || "NaN"),Number(hollowH.text.trim() || "NaN")) }
            Label { text: qsTr("顺序 X、Y、宽、高。可拖拽框选，按原图整像素向外吸附；完成后再点框内墨块。Esc 或越界取消本次拖动，可重拖；按钮可结束框选模式。黄框仅为范围，不修改画面，挖空仍须接受实际预览。自由套索自动闭合、最多512点，自交或越界拒绝；数字框为外接矩形，修改数字会切回矩形。"); color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { objectName: "lineartFillNotice"; text: studio.fillNotice; color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: studio.layerNotice; color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
            Label { text:qsTr("鼠标光标轮廓 = 满压力名义笔尖范围，中心十字是定位标记。压力、收笔、纹理及抗锯齿会改变实际墨迹；数位笔不显示名义范围，落笔时看真实预览。光标不会进入图层或导出。");color:"#bcbcc4";wrapMode:Text.Wrap;Layout.fillWidth:true }
            Label { text: qsTr("画笔 · 原生笔迹"); color: "#f5f5f7"; font.pixelSize: 18 }
            Label { text: qsTr("宽度：") + nativeCanvas.brushWidth.toFixed(1) + " px"; color: "#bcbcc4" }
            Button { text: qsTr("取消笔迹验收，保留原稿"); visible: studio.strokeCommitBusy; onClicked: nativeCanvas.cancelPendingStroke(); Layout.fillWidth: true }
            Switch { objectName:"controlledEraserSwitch";text:qsTr("连续橡皮 · 只擦当前手绘层");checked:nativeCanvas.controlledEraser;onToggled:{nativeCanvas.configureControlledEraser(checked);checked=Qt.binding(function(){return nativeCanvas.controlledEraser})} }
            Label { visible:nativeCanvas.controlledEraser;text:{for(var i=0;i<studio.layers.length;i++){var row=studio.layers[i];if(row.id===studio.activeLayerId)return qsTr("橡皮目标：")+row.name;}return qsTr("橡皮目标：未选择可用层");}color:"#edc79a";wrapMode:Text.Wrap;Layout.fillWidth:true }
            Label { visible:nativeCanvas.controlledEraser;text:qsTr("擦除当前层透明度，让下层透出，不是涂白。只适用尚无依赖填色的独立手绘层；导入原稿、其他层和已绑定边界不擦。宽度≤128px，不透明度控制擦除力度，颜色不参与。落笔可预览，Esc取消、Undo恢复。新格式22须另存新文件。");color:"#edc79a";wrapMode:Text.Wrap;Layout.fillWidth:true }
            Switch { objectName:"chiselInkSwitch"; text:qsTr("连续固定角扁笔尖（3:1椭圆）");checked:nativeCanvas.chiselInk;onToggled:{nativeCanvas.configureChiselInk(checked,nativeCanvas.chiselAngle);checked=Qt.binding(function(){return nativeCanvas.chiselInk})} }
            ComboBox { objectName:"chiselAngleChoice"; visible:nativeCanvas.chiselInk;model:["0°","22.5°","45°","67.5°","90°","112.5°","135°","157.5°"];currentIndex:nativeCanvas.chiselAngle;onActivated:{nativeCanvas.configureChiselInk(true,currentIndex);currentIndex=Qt.binding(function(){return nativeCanvas.chiselAngle})}Layout.fillWidth:true }
            Label { text:qsTr("固定方向的连续椭圆扫掠，压力控制大小；不响应倾角、不模拟笔毛纹理。≤128原像素，与圆实墨/干墨互斥。新格式21须另存新文件名，旧版不能打开；暂不支持圆实墨的局部改点/闭合工具。");visible:nativeCanvas.chiselInk;wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#dbba98" }
            Switch { objectName: "smoothInkSwitch"; text: qsTr("连续实墨（抗锯齿轮廓）"); checked: nativeCanvas.smoothInk; onToggled: { nativeCanvas.configureSmoothInk(checked); checked = Qt.binding(function() { return nativeCanvas.smoothInk }) } }
            Label { text: qsTr("连续圆形实墨，笔压控制宽度；固定实边、≤128原像素。与干墨互斥，切换取消未提交笔画；不自动磨皮或重画原图。橡皮仍用原模式。"); visible: nativeCanvas.smoothInk; wrapMode: Text.Wrap; color: "#dbba98"; Layout.fillWidth: true }
            Button { objectName:"newPersistentPaintLayer";visible:studio.myPaintAvailable;enabled:!studio.busy && !nativeCanvas.strokePreviewActive;text:qsTr("新建 MyPaint 持久画层（不合并旧图）");onClicked:studio.addPersistentPaintLayer() }
            Label { objectName:"persistentPaintStatus";visible:studio.myPaintAvailable;text:studio.persistentPaintStatus;wrapMode:Text.WordWrap;Layout.fillWidth:true;color:"#ffca80" }
            RowLayout { visible:studio.persistentPaintSelected
                Button { objectName:"withdrawPersistentStroke";text:qsTr("逐笔撤回（随文件保存）");enabled:!studio.busy && !nativeCanvas.strokePreviewActive;onClicked:studio.stepPersistentPaint(-1) }
                Button { objectName:"restorePersistentStroke";text:qsTr("恢复上一笔");enabled:!studio.busy && !nativeCanvas.strokePreviewActive;onClicked:studio.stepPersistentPaint(1) }
            }
            Switch { objectName:"myPaintSwitch";text:qsTr("MyPaint 真实笔刷（实验，松笔后预览）");visible:studio.myPaintAvailable;checked:nativeCanvas.myPaintTool;onToggled:{nativeCanvas.configureMyPaint(checked,nativeCanvas.myPaintPreset,nativeCanvas.myPaintRadiusScale);checked=Qt.binding(function(){return nativeCanvas.myPaintTool})} }
            Label { text:studio.myPaintStatus;wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#edc79a" }
            ComboBox { objectName:"myPaintPresetBox";visible:nativeCanvas.myPaintTool;enabled:!studio.busy && !nativeCanvas.strokePreviewActive;model:["pointy_ink","dry_brush"];currentIndex:nativeCanvas.myPaintPreset==="dry_brush"?1:0;onActivated:{nativeCanvas.configureMyPaint(true,currentText,nativeCanvas.myPaintRadiusScale);currentIndex=Qt.binding(function(){return nativeCanvas.myPaintPreset==="dry_brush"?1:0})} }
            Label { visible:nativeCanvas.myPaintTool;text:qsTr("预设半径倍率（不是像素宽度）：")+nativeCanvas.myPaintRadiusScale.toFixed(2) }
            Slider { objectName:"myPaintScaleSlider";visible:nativeCanvas.myPaintTool;enabled:!studio.busy && !nativeCanvas.strokePreviewActive;from:.25;to:8;stepSize:.25;value:nativeCanvas.myPaintRadiusScale;onMoved:{nativeCanvas.configureMyPaint(true,nativeCanvas.myPaintPreset,value);value=Qt.binding(function(){return nativeCanvas.myPaintRadiusScale})} }
            Button { objectName:"acceptMyPaintButton";visible:studio.myPaintPreviewPending;text:studio.persistentPaintSelected?qsTr("接受本笔到同一画层"):qsTr("接受 MyPaint 独立笔层");enabled:!nativeCanvas.rendering&&nativeCanvas.renderError.length===0;onClicked:studio.acceptFill() }
            Button { objectName:"cancelMyPaintButton";visible:nativeCanvas.myPaintTool&&(studio.fillBusy||studio.fillPending);text:qsTr("取消本笔（原稿不变）");onClicked:studio.cancelFill() }
            Switch { objectName: "dryInkSwitch"; text: qsTr("干墨纤维笔触（非整图滤镜）"); checked: nativeCanvas.dryInk; onToggled: { nativeCanvas.configureDryInk(checked); checked = Qt.binding(function() { return nativeCanvas.dryInk }) } }
            Label { text: qsTr("干墨固定硬边，压力控制宽度/断墨；≤128原像素，每笔≤4096输入点。橡皮仍用原橡皮模式。切换模式取消未提交笔画；不是原画笔触自动还原。"); visible: nativeCanvas.dryInk; wrapMode: Text.Wrap; color: "#dbba98"; Layout.fillWidth: true }
            Slider { objectName: "brushWidthSlider"; enabled:!nativeCanvas.myPaintTool; from: 0.5; to: 128; stepSize: 0.5; value: nativeCanvas.brushWidth; Layout.fillWidth: true; onMoved: nativeCanvas.configureBrush(value,nativeCanvas.brushColor,nativeCanvas.brushOpacity,nativeCanvas.brushHardness,nativeCanvas.mouseTaper) }
            RowLayout {
                Rectangle { width: 24; height: 24; color: nativeCanvas.brushColor; border.color: "#aaaaaa" }
                TextField { objectName: "brushColorInput"; text: nativeCanvas.brushColor.toString(); placeholderText: "#202020"; Layout.fillWidth: true; onEditingFinished: { if (!nativeCanvas.configureBrush(nativeCanvas.brushWidth,text,nativeCanvas.brushOpacity,nativeCanvas.brushHardness,nativeCanvas.mouseTaper)) text = nativeCanvas.brushColor.toString() } }
            }
            Label { text: qsTr("不透明度：") + Math.round(nativeCanvas.brushOpacity*100) + "%"; color: "#bcbcc4" }
            Slider { objectName: "brushOpacitySlider"; from: 0.01; to: 1; value: nativeCanvas.brushOpacity; Layout.fillWidth: true; onMoved: nativeCanvas.configureBrush(nativeCanvas.brushWidth,nativeCanvas.brushColor,value,nativeCanvas.brushHardness,nativeCanvas.mouseTaper) }
            Label { text: qsTr("硬度：") + Math.round(nativeCanvas.brushHardness*100) + "%"; color: "#bcbcc4" }
            Slider { objectName: "brushHardnessSlider"; enabled: !nativeCanvas.myPaintTool && !nativeCanvas.dryInk && !nativeCanvas.smoothInk && !nativeCanvas.chiselInk && !nativeCanvas.controlledEraser; from: 0; to: 1; value: nativeCanvas.brushHardness; Layout.fillWidth: true; onMoved: nativeCanvas.configureBrush(nativeCanvas.brushWidth,nativeCanvas.brushColor,nativeCanvas.brushOpacity,value,nativeCanvas.mouseTaper) }
            Label { text: qsTr("鼠标空间起收笔辅助（非真实压力）：") + Math.round(nativeCanvas.mouseTaper*100) + "%"; color: "#bcbcc4" }
            Slider { objectName: "mouseTaperSlider"; enabled:!studio.busy && !studio.fillPending && !nativeCanvas.strokePreviewActive; from: 0; to: 0.5; stepSize: 0.05; value: nativeCanvas.mouseTaper; Layout.fillWidth: true; onMoved: { if (enabled) nativeCanvas.configureBrush(nativeCanvas.brushWidth,nativeCanvas.brushColor,nativeCanvas.brushOpacity,nativeCanvas.brushHardness,value); value=Qt.binding(function(){return nativeCanvas.mouseTaper}) } }
            Label { text: qsTr("0 为关闭；辅助只改变鼠标笔画首尾压力，不替代数位笔真实压感。设置对下一笔生效。"); color: "#bcbcc4"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: nativeCanvas.brushNotice; visible: text.length > 0; color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            CheckBox { id: strokeEditMode; objectName: "strokeEditMode"; text: qsTr("修整所选实墨笔迹（不新增细节）"); enabled: !studio.busy
                onToggled: { nativeCanvas.cancelStroke();strokeEditOverlay.selectedIndex=-1 }
            }
            ColumnLayout { visible: strokeEditMode.checked; Layout.fillWidth: true
                ComboBox { id: editStrokeChoice; objectName:"editStrokeChoice"; Layout.fillWidth:true; model:studio.editableStrokes; textRole:"label"; valueRole:"id"; enabled:!studio.busy
                    currentIndex: { const list=studio.editableStrokes, id=studio.selectedEditStroke;for(let i=0;i<list.length;i++)if(list[i].id===id)return i;return -1 }
                    onActivated: { studio.selectEditStroke(currentValue);currentIndex=Qt.binding(function(){return indexOfValue(studio.selectedEditStroke)}) }
                }
                Label { text:qsTr("列出当前层近期安全实墨；有后续填色时最多检查128条候选。后续填色仅在与整条笔迹明确分离时允许修整；接触色块、挖空及未知依赖仍受保护，不会自动重填。");wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#bcbcc4" }
                Label { text: qsTr("蓝点为已有采样点。点击或拖动一点，再预览；范围按原笔迹弧长计算。只改选中笔迹，稀疏笔迹不会插点。"); wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#bcbcc4" }
                Label { text: strokeEditOverlay.selectedIndex<0 ? qsTr("未选中采样点") : qsTr("采样点 ")+(strokeEditOverlay.selectedIndex+1); color: "#5dd6ff" }
                GridLayout { columns: 2; enabled: !studio.busy; Layout.fillWidth: true
                    Label { text: qsTr("影响半径(px)"); color:"#bcbcc4" } TextField { id: editRadius; objectName:"editRadius"; text:"16"; Layout.fillWidth:true }
                    Label { text: qsTr("水平移动(px)"); color:"#bcbcc4" } TextField { id: editDx; objectName:"editDx"; text:"0"; Layout.fillWidth:true }
                    Label { text: qsTr("垂直移动(px)"); color:"#bcbcc4" } TextField { id: editDy; objectName:"editDy"; text:"0"; Layout.fillWidth:true }
                    Label { text: qsTr("笔压增量(-1~1)"); color:"#bcbcc4" } TextField { id: editPressure; objectName:"editPressure"; text:"0"; Layout.fillWidth:true }
                }
                Button { objectName:"previewStrokeEdit"; text:qsTr("渲染修整预览"); enabled:!studio.busy && strokeEditOverlay.selectedIndex>=0
                    onClicked: studio.previewLocalStrokeEdit(strokeEditOverlay.selectedIndex,Number(editRadius.text.trim()||"NaN"),Number(editDx.text.trim()||"NaN"),Number(editDy.text.trim()||"NaN"),Number(editPressure.text.trim()||"NaN"))
                }
                Label { text:qsTr("超出画布、笔压范围或渲染预算会拒绝，不会暗中截断。使用上方接受/取消；接受后可撤销。"); wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#bcbcc4" }
            }
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
            Button { text: qsTr("附加原彩（不改变画布）"); enabled: studio.hasDocument && !studio.busy; onClicked: originalReferenceDialog.open(); Layout.fillWidth: true }
            Button { text: qsTr("打开只读原彩对照窗"); enabled: studio.originalReferenceUrl.toString().length > 0; onClicked: originalReferenceWindow.show(); Layout.fillWidth: true }
            Label { text: studio.originalReferenceNotice; visible: text.length > 0; color: "#dbba98"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Switch { id: showReference; text: qsTr("Show reference overlay"); checked: true; enabled: studio.hasReference }
            Label { text: qsTr("Reference opacity"); color: "#b5b5bd"; visible: studio.hasReference }
            Slider { id: referenceOpacity; from: 0; to: 0.75; value: 0.35; enabled: studio.hasReference; visible: studio.hasReference; Layout.fillWidth: true }
            Label { text: studio.referenceFeedback; color: "#dbba98"; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: studio.referenceFeedback.length > 0 }
            Label { text: qsTr("Reference could not be displayed."); color: "#ffc2a1"; visible: studio.hasReference && referencePreview.status === Image.Error }
            Label { text: qsTr("This build includes the native tiled canvas, mouse/tablet stroke input, replay core, DPI coordinates and cache recovery foundation."); color: "#a5a5ae"; wrapMode: Text.Wrap; Layout.fillWidth: true; lineHeight: 1.45 }
            Button { text: qsTr("Check native worker"); enabled: studio.hasDocument && !studio.busy; onClicked: studio.probeWorker() }
            BusyIndicator { objectName:"operationSpinner"; running: studio.strokeCommitBusy || studio.fillBusy || (studio.busy && !studio.cubicDraftActive && !studio.fillPending); visible: running }
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
            CheckBox { id: whitePaperInput; objectName: "documentWhitePaper"; text: qsTr("白纸背景（局部挖空需要；不勾选为透明）"); checked: true }
            Label { text: qsTr("Unsaved work will be replaced."); visible: studio.dirty }
        }
        onAccepted: studio.createDocument(Number(widthInput.text), Number(heightInput.text), 300, whitePaperInput.checked)
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
        id: saveDialog; title: qsTr("保存工程（新格式须另存新文件名，保留原稿）"); fileMode: FileDialog.SaveFile; defaultSuffix: "draw"; nameFilters: ["Drawborad (*.draw)"]
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

import QtQuick
import QtQuick.Controls
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
    onClosing: function(event) {
        if (studio.dirty && !approvedClose) { event.accepted = false; closeDialog.open() }
    }
    header: ToolBar {
        padding: 12
        background: Rectangle { color: "#29292c" }
        RowLayout {
            anchors.fill: parent
            Label { text: "Drawborad"; color: "#fafafa"; font.pixelSize: 18; font.weight: Font.DemiBold }
            Label { text: "ALPHA 0.1  /  CANVAS TEST"; color: "#aaaab0"; font.pixelSize: 11; Layout.leftMargin: 12 }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("New"); onClicked: newDialog.open() }
            Button { text: qsTr("Import PNG/JPEG reference"); onClicked: {
                if (studio.dirty) replaceWithReferenceDialog.open()
                else referenceDialog.open()
            } }
            Button { text: qsTr("Open project"); onClicked: { if (studio.dirty) replaceDialog.open(); else openDialog.open() } }
            Button { text: qsTr("Save"); enabled: studio.hasDocument; onClicked: saveDialog.open() }
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
            NativeCanvas { id: nativeCanvas; objectName: "nativeCanvas"; anchors.fill: parent; anchors.margins: 24; visible: studio.hasDocument; bridge: studio }
            Image {
                id: referencePreview
                anchors.fill: nativeCanvas
                source: studio.referenceUrl
                visible: studio.hasReference && showReference.checked && studio.hasDocument
                opacity: referenceOpacity.value
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                // A preview overlay. It is deliberately not rasterized into paint events.
            }
            Label { anchors.centerIn: parent; visible: !studio.hasDocument; text: qsTr("Create a document to begin"); color: "#bcbcc4"; font.pixelSize: 18 }
        }
        ColumnLayout {
            Layout.minimumWidth: 280
            Layout.preferredWidth: 280
            Layout.maximumWidth: 280
            Layout.fillWidth: false
            Layout.alignment: Qt.AlignTop
            spacing: 18
            Label { text: qsTr("Document"); color: "#f5f5f7"; font.pixelSize: 23 }
            Label { text: qsTr("ALPHA / not an AI reconstruction build"); color: "#d9c59e"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("Reference image is preview-only; .draw saves paint events, not the imported source image."); color: "#d6b1a6"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Switch { id: showReference; text: qsTr("Show reference overlay"); checked: true; enabled: studio.hasReference }
            Label { text: qsTr("Reference opacity"); color: "#b5b5bd"; visible: studio.hasReference }
            Slider { id: referenceOpacity; from: 0; to: 0.75; value: 0.35; enabled: studio.hasReference; visible: studio.hasReference; Layout.fillWidth: true }
            Label { text: studio.referenceFeedback; color: "#dbba98"; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: studio.referenceFeedback.length > 0 }
            Label { text: qsTr("Reference could not be displayed."); color: "#ffc2a1"; visible: studio.hasReference && referencePreview.status === Image.Error }
            Label { text: studio.dimensions; color: "#b5b5bd"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTr("This build includes the native tiled canvas, mouse/tablet stroke input, replay core, DPI coordinates and cache recovery foundation."); color: "#a5a5ae"; wrapMode: Text.Wrap; Layout.fillWidth: true; lineHeight: 1.45 }
            Button { text: qsTr("Check native worker"); enabled: studio.hasDocument && !studio.busy; onClicked: studio.probeWorker() }
            Button { text: qsTr("Cancel worker"); enabled: studio.busy; onClicked: studio.cancelWorker() }
            BusyIndicator { running: studio.busy; visible: studio.busy }
            Label { text: studio.message; color: "#edc79a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    Dialog {
        id: newDialog; anchors.centerIn: parent; title: qsTr("New document"); modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            Label { text: qsTr("Dimensions in pixels / RGB 8-bit"); font.pixelSize: 14 }
            TextField { id: widthInput; text: "2048"; placeholderText: qsTr("Width"); validator: DoubleValidator { bottom: 1 } }
            TextField { id: heightInput; text: "2048"; placeholderText: qsTr("Height"); validator: DoubleValidator { bottom: 1 } }
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
    FileDialog { id: saveDialog; title: qsTr("Save a Drawborad project"); fileMode: FileDialog.SaveFile; defaultSuffix: "draw"; nameFilters: ["Drawborad (*.draw)"]; onAccepted: studio.saveUrl(selectedFile) }
    Dialog {
        id: closeDialog; anchors.centerIn: parent; modal: true; title: qsTr("Unsaved document")
        ColumnLayout {
            Label { text: qsTr("Save before closing, or explicitly discard changes.") }
            RowLayout {
                Button { text: qsTr("Cancel"); onClicked: closeDialog.close() }
                Button { text: qsTr("Save"); onClicked: { closeDialog.close(); saveDialog.open() } }
                Button { text: qsTr("Discard"); onClicked: { window.approvedClose = true; window.close() } }
            }
        }
    }
}

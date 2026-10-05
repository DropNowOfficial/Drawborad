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
    title: "Drawborad / Canvas R01-R06"
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
            Label { text: "CANVAS R01-R06"; color: "#aaaab0"; font.pixelSize: 11; Layout.leftMargin: 12 }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("New"); onClicked: newDialog.open() }
            Button { text: qsTr("Open project"); onClicked: { if (studio.dirty) replaceDialog.open(); else openDialog.open() } }
            Button { text: qsTr("Save"); enabled: studio.hasDocument; onClicked: saveDialog.open() }
        }
    }
    RowLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 24
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: "#323235"; radius: 10
            NativeCanvas { objectName: "nativeCanvas"; anchors.fill: parent; anchors.margins: 24; visible: studio.hasDocument; bridge: studio }
            Label { anchors.centerIn: parent; visible: !studio.hasDocument; text: qsTr("Create a document to begin"); color: "#bcbcc4"; font.pixelSize: 18 }
        }
        ColumnLayout {
            Layout.preferredWidth: 280; Layout.alignment: Qt.AlignTop; spacing: 18
            Label { text: qsTr("Document"); color: "#f5f5f7"; font.pixelSize: 23 }
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

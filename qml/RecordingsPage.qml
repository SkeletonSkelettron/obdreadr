pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import ObdReadr

// The saved sensor recordings (CSV), to export for analysis on a computer.
Page {
    id: page

    signal backRequested()
    signal viewRequested(string path)

    property string message

    Component.onCompleted: ObdSession.refreshRecordings()

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 0
            ToolButton {
                text: "‹"
                font.pixelSize: 30
                onClicked: page.backRequested()
            }
            Label {
                Layout.fillWidth: true
                font.bold: true
                text: qsTr("Recordings")
            }
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 8
        model: ObdSession.recordings
        ScrollIndicator.vertical: ScrollIndicator {}

        header: ColumnLayout {
            width: list.width
            spacing: 0

            Pane {
                Layout.fillWidth: true
                Layout.margins: 12
                padding: 14
                Material.elevation: 3

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Switch {
                        Layout.fillWidth: true
                        text: qsTr("Record all sensors")
                        checked: ObdSession.recordAll
                        onToggled: ObdSession.recordAll = checked
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        font.pixelSize: 12
                        text: ObdSession.recordAll
                              ? qsTr("Every sensor the car supports is read in the background, each at its own pace: fast-changing ones such as engine speed every second, slow ones such as temperatures every few seconds. Sensors on screen are read more often.")
                              : qsTr("Only the sensors on screen are read and recorded. They update faster this way.")
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                leftPadding: 16
                rightPadding: 16
                bottomPadding: 8
                wrapMode: Text.Wrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("Each connection is saved as a CSV file: a time column, a timestamp column and one column per sensor, one row per reading. Export a file to Downloads or a cloud drive and open it on a computer.")
            }

            Label {
                Layout.fillWidth: true
                leftPadding: 16
                rightPadding: 16
                bottomPadding: 8
                visible: page.message !== ""
                wrapMode: Text.Wrap
                color: "#A5D6A7"
                text: page.message
            }

            Label {
                Layout.fillWidth: true
                padding: 16
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                opacity: 0.7
                text: qsTr("No recordings yet. They start when the app is connected to a car.")
            }
        }

        delegate: Pane {
            id: item

            required property var modelData

            x: 12
            width: list.width - 24
            padding: 12
            Material.elevation: 2

            RowLayout {
                anchors.fill: parent
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    // Tap the recording to see it.
                    TapHandler {
                        onTapped: page.viewRequested(item.modelData.path)
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.bold: true
                        color: "#90CAF9"
                        text: item.modelData.title
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        opacity: 0.7
                        text: item.modelData.details
                    }
                    Label {
                        visible: item.modelData.current
                        font.pixelSize: 12
                        color: "#EF9A9A"
                        text: qsTr("● Recording now")
                    }
                }
                Button {
                    flat: true
                    text: qsTr("Export")
                    onClicked: {
                        page.message = ""
                        saveDialog.source = item.modelData.path
                        saveDialog.selectedFile = saveDialog.currentFolder + "/" + item.modelData.fileName
                        saveDialog.open()
                    }
                }
                ToolButton {
                    enabled: !item.modelData.current
                    text: "✕"
                    onClicked: {
                        deleteDialog.path = item.modelData.path
                        deleteDialog.name = item.modelData.title
                        deleteDialog.open()
                    }
                }
            }
        }
    }

    FileDialog {
        id: saveDialog

        property string source

        title: qsTr("Export recording")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("CSV files (*.csv)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: page.message = ObdSession.exportRecording(source, selectedFile)
    }

    Dialog {
        id: deleteDialog

        property string path
        property string name

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(page.width - 32, 420)
        modal: true
        title: qsTr("Delete this recording?")
        footer: ConfirmButtons {}

        Label {
            width: deleteDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("The recording from %1 is deleted from the phone. This cannot be undone.").arg(deleteDialog.name)
        }

        onAccepted: ObdSession.deleteRecording(path)
    }
}

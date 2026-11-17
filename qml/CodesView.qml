pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// Diagnostic trouble codes: read them, and clear them (mode 04).
Item {
    id: page

    readonly property bool running: ObdSession.state === ObdSession.Running
    readonly property var kindColors: ({ stored: "#EF9A9A", pending: "#FFE082", permanent: "#CE93D8" })
    readonly property var kindNames: ({ stored: qsTr("Stored"), pending: qsTr("Pending"), permanent: qsTr("Permanent") })
    readonly property color statusColor: ObdSession.milOn || ObdSession.dtcCodes.length > 0 ? "#FFAB91" : "#A5D6A7"

    Component.onCompleted: {
        if (page.running && !ObdSession.dtcRead)
            ObdSession.readCodes()
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 8
        model: ObdSession.dtcCodes
        ScrollIndicator.vertical: ScrollIndicator {}

        header: Pane {
            width: list.width
            padding: 12

            Pane {
                width: parent.width
                padding: 14
                Material.elevation: 3

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            implicitWidth: 14
                            implicitHeight: 14
                            radius: 7
                            visible: ObdSession.dtcRead
                            color: page.statusColor
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 15
                            color: ObdSession.dtcRead ? page.statusColor : Material.foreground
                            text: ObdSession.dtcStatus !== "" ? ObdSession.dtcStatus
                                  : page.running ? qsTr("Press Read to ask the car for its fault codes.")
                                  : qsTr("Connect to the car to read its fault codes.")
                        }
                        BusyIndicator {
                            implicitWidth: 32
                            implicitHeight: 32
                            running: ObdSession.dtcBusy
                            visible: running
                        }
                    }
                    RowLayout {
                        Button {
                            highlighted: true
                            text: qsTr("Read")
                            enabled: page.running && !ObdSession.dtcBusy
                            onClicked: ObdSession.readCodes()
                        }
                        Button {
                            flat: true
                            text: qsTr("Clear codes…")
                            enabled: page.running && !ObdSession.dtcBusy && ObdSession.dtcRead
                            onClicked: confirm.open()
                        }
                    }
                }
            }
        }

        delegate: Pane {
            id: codeItem

            required property var modelData
            readonly property color kindColor: page.kindColors[modelData.kind]

            x: 12
            width: list.width - 24
            padding: 12
            Material.elevation: 2

            RowLayout {
                anchors.fill: parent
                spacing: 14

                Label {
                    Layout.alignment: Qt.AlignTop
                    font.pixelSize: 20
                    font.bold: true
                    font.family: "monospace"
                    color: codeItem.kindColor
                    text: codeItem.modelData.code
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Qt.tint(codeItem.kindColor, "#99ffffff")
                        text: codeItem.modelData.description
                    }
                    Label {
                        font.pixelSize: 12
                        color: codeItem.kindColor
                        opacity: 0.8
                        text: page.kindNames[codeItem.modelData.kind]
                    }
                }
            }
        }

        footer: Label {
            width: list.width
            padding: 16
            wrapMode: Text.Wrap
            opacity: 0.6
            font.pixelSize: 12
            text: qsTr("Stored codes turned the check engine light on. Pending codes were seen once and wait for confirmation. Permanent codes stay until the car itself finds the fault gone; clearing does not remove them. Codes starting P1, B1, C1 or U1 are specific to the car's maker.")
        }
    }

    Dialog {
        id: confirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(page.width - 32, 420)
        modal: true
        title: qsTr("Clear the fault codes?")
        standardButtons: Dialog.Yes | Dialog.Cancel

        Label {
            width: confirm.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("This turns the check engine light off and erases the stored codes and their freeze frame data. The readiness monitors reset too, so the car needs some driving before an emissions inspection. If the fault is still there, the code comes back.\n\nSwitch the engine off and leave the ignition on.")
        }

        onAccepted: ObdSession.clearCodes()
    }
}

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// Which of the car's self-tests have run since the codes were last cleared.
Item {
    id: view

    readonly property bool running: ObdSession.state === ObdSession.Running

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 2
        bottomMargin: 12
        model: ObdSession.readiness
        ScrollIndicator.vertical: ScrollIndicator {}

        header: ColumnLayout {
            width: list.width
            spacing: 6

            Label {
                Layout.fillWidth: true
                padding: 16
                bottomPadding: 0
                wrapMode: Text.Wrap
                opacity: 0.75
                font.pixelSize: 12
                text: qsTr("The car tests its emission parts while driving. After the codes are cleared or the battery is disconnected these tests start over, and an inspection may fail until they are complete, which can take a few days of mixed driving.")
            }
            Button {
                Layout.leftMargin: 12
                Layout.bottomMargin: 8
                highlighted: true
                text: qsTr("Read")
                enabled: view.running && !ObdSession.dtcBusy
                onClicked: ObdSession.readCodes()
            }
            Label {
                Layout.fillWidth: true
                padding: 16
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                opacity: 0.7
                text: view.running ? qsTr("Press Read.") : qsTr("Connect to the car to read the monitors.")
            }
        }

        delegate: Rectangle {
            id: row

            required property var modelData
            required property int index
            readonly property color statusColor: !modelData.supported ? "#9E9E9E"
                                                 : modelData.complete ? "#A5D6A7" : "#FFE082"

            width: list.width
            height: 52
            color: index % 2 ? Qt.rgba(1, 1, 1, 0.035) : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Rectangle {
                    implicitWidth: 12
                    implicitHeight: 12
                    radius: 6
                    color: row.statusColor
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    opacity: row.modelData.supported ? 1 : 0.5
                    text: row.modelData.name
                }
                Label {
                    color: row.statusColor
                    font.pixelSize: 13
                    text: !row.modelData.supported ? qsTr("not on this car")
                          : row.modelData.complete ? qsTr("Complete") : qsTr("Not complete")
                }
            }
        }
    }
}

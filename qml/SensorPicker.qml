pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// A side panel that ticks sensors on and off for one selection:
// "active" (Charts and Values tabs) or "pinned" (Overlay and Selected tabs).
Drawer {
    id: picker

    property string role: "active"
    property string title
    property string hint

    edge: Qt.RightEdge

    function setAll(on) {
        if (picker.role === "pinned")
            ObdSession.setAllSensorsPinned(on)
        else
            ObdSession.setAllSensorsActive(on)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            Layout.fillWidth: true
            padding: 16
            bottomPadding: 4
            font.pixelSize: 18
            font.bold: true
            text: picker.title
        }
        Label {
            Layout.fillWidth: true
            leftPadding: 16
            rightPadding: 16
            visible: ObdSession.vin !== ""
            opacity: 0.7
            font.pixelSize: 12
            text: qsTr("VIN %1").arg(ObdSession.vin)
        }
        Label {
            Layout.fillWidth: true
            leftPadding: 16
            rightPadding: 16
            topPadding: 4
            wrapMode: Text.Wrap
            opacity: 0.7
            font.pixelSize: 12
            text: picker.hint
        }
        RowLayout {
            Layout.leftMargin: 8
            Button {
                flat: true
                text: qsTr("All")
                onClicked: picker.setAll(true)
            }
            Button {
                flat: true
                text: qsTr("None")
                onClicked: picker.setAll(false)
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: ObdSession.sensors
            ScrollIndicator.vertical: ScrollIndicator {}

            delegate: CheckDelegate {
                id: check

                required property var model
                required property string name
                required property bool active
                required property bool pinned
                required property bool available
                required property var series

                readonly property bool selected: picker.role === "pinned" ? pinned : active

                width: ListView.view.width
                enabled: available
                checked: selected
                text: available ? name : qsTr("%1 (no data)").arg(name)
                Material.foreground: Qt.tint(series.color, "#66ffffff")

                onToggled: {
                    model[picker.role] = checked
                    checked = Qt.binding(() => check.selected)
                }
            }
        }
    }
}

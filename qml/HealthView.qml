pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// Checks on the live sensor data for problems that come before fault codes.
Item {
    id: view

    readonly property var colors: ({ ok: "#A5D6A7", warn: "#FFE082", bad: "#EF9A9A", info: "#90CAF9", nodata: "#9E9E9E" })
    readonly property var labels: ({ ok: qsTr("OK"), warn: qsTr("Watch"), bad: qsTr("Problem"), info: qsTr("Waiting"), nodata: qsTr("No data") })

    onVisibleChanged: {
        if (visible)
            ObdSession.evaluateHealth()
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 8
        bottomMargin: 12
        model: ObdSession.healthChecks
        ScrollIndicator.vertical: ScrollIndicator {}

        header: ColumnLayout {
            width: list.width
            spacing: 0

            Label {
                Layout.fillWidth: true
                padding: 16
                bottomPadding: 6
                wrapMode: Text.Wrap
                opacity: 0.75
                font.pixelSize: 12
                text: qsTr("These checks watch the last minutes of sensor data for problems that develop before the car sets a fault code. They need a warm engine, some idling and some driving, and use general limits rather than your car maker's. Compare with recordings from when the car was healthy.")
            }
            Label {
                Layout.fillWidth: true
                leftPadding: 16
                rightPadding: 16
                bottomPadding: 8
                visible: !ObdSession.recordAll
                wrapMode: Text.Wrap
                color: "#FFE082"
                font.pixelSize: 12
                text: qsTr("\"Record all sensors\" is off, so only the sensors on screen are checked.")
            }
            Label {
                Layout.fillWidth: true
                padding: 16
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                opacity: 0.7
                text: ObdSession.state === ObdSession.Running ? qsTr("Collecting data…")
                                                               : qsTr("Connect to the car to run the checks.")
            }
        }

        delegate: Pane {
            id: card

            required property var modelData
            readonly property color statusColor: view.colors[modelData.status]

            x: 12
            width: list.width - 24
            padding: 12
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle {
                        implicitWidth: 12
                        implicitHeight: 12
                        radius: 6
                        color: card.statusColor
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.bold: true
                        color: Qt.tint(card.statusColor, "#99ffffff")
                        text: card.modelData.title
                    }
                    Label {
                        font.pixelSize: 12
                        color: card.statusColor
                        text: view.labels[card.modelData.status]
                    }
                }
                Label {
                    Layout.fillWidth: true
                    leftPadding: 22
                    visible: text !== ""
                    wrapMode: Text.Wrap
                    font.pixelSize: 16
                    color: card.statusColor
                    text: card.modelData.value
                }
                Label {
                    Layout.fillWidth: true
                    leftPadding: 22
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    opacity: 0.8
                    text: card.modelData.detail
                }
            }
        }
    }
}

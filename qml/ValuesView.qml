pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// The charted sensors as a list of names and current values, no charts.
Item {
    id: view

    signal chooseRequested()

    ListView {
        id: list
        anchors.fill: parent
        topMargin: 6
        bottomMargin: 6
        clip: true
        model: ObdSession.activeSensors
        ScrollIndicator.vertical: ScrollIndicator {}

        delegate: Rectangle {
            id: row

            required property int index
            required property string name
            required property string unit
            required property string pidText
            required property var series

            width: ListView.view.width
            height: 60
            color: index % 2 ? Qt.rgba(1, 1, 1, 0.035) : "transparent"

            Rectangle {
                x: 10
                width: 4
                height: parent.height - 18
                anchors.verticalCenter: parent.verticalCenter
                radius: 2
                color: row.series.color
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 16
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 15
                        color: Qt.tint(row.series.color, "#99ffffff")
                        text: row.name
                    }
                    Label {
                        opacity: 0.5
                        font.pixelSize: 11
                        text: row.pidText
                    }
                }
                Label {
                    font.pixelSize: 24
                    font.bold: true
                    color: row.series.color
                    text: row.series.valueText
                }
                Label {
                    Layout.preferredWidth: 46
                    font.pixelSize: 13
                    color: row.series.color
                    opacity: 0.75
                    text: row.unit
                }
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: ObdSession.state === ObdSession.Running && list.count === 0
        spacing: 12
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("No sensors selected.")
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Choose sensors")
            onClicked: view.chooseRequested()
        }
    }
}

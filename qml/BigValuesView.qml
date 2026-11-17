pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// The compared sensors' current values in large tiles.
Item {
    id: view

    signal chooseRequested()

    GridView {
        id: grid

        readonly property int columns: width > 700 ? 3 : 2

        anchors.fill: parent
        anchors.margins: 6
        clip: true
        cellWidth: Math.floor(width / columns)
        cellHeight: 150
        model: ObdSession.pinnedSensors
        ScrollIndicator.vertical: ScrollIndicator {}

        delegate: Item {
            id: tile

            required property string name
            required property string unit
            required property var series

            width: grid.cellWidth
            height: grid.cellHeight

            Pane {
                anchors.fill: parent
                anchors.margins: 6
                padding: 12
                Material.elevation: 3

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        color: Qt.tint(tile.series.color, "#99ffffff")
                        text: tile.name
                    }
                    Item { Layout.fillHeight: true }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 18
                        font.pixelSize: 46
                        font.bold: true
                        color: tile.series.color
                        text: tile.series.valueText
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        font.pixelSize: 13
                        color: tile.series.color
                        opacity: 0.75
                        text: tile.unit
                    }
                }
            }
        }

        footer: Item {
            width: grid.width
            height: 56
            visible: grid.count > 0
            Button {
                anchors.centerIn: parent
                flat: true
                text: qsTr("Choose sensors…")
                onClicked: view.chooseRequested()
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: ObdSession.state === ObdSession.Running && grid.count === 0
        spacing: 12
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("No sensors chosen.")
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Choose sensors")
            onClicked: view.chooseRequested()
        }
    }
}

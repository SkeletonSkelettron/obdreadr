pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// The compared sensors on one time axis, with a legend below.
Item {
    id: view

    signal chooseRequested()

    property int focusIndex: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Pane {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(220, view.height * 0.55)
            padding: 8
            Material.elevation: 3

            OverlayChart {
                anchors.fill: parent
                series: ObdSession.pinnedSeries
                focusIndex: view.focusIndex
                timeSpan: ObdSession.windowSeconds
                live: ObdSession.state === ObdSession.Running && !ObdSession.paused
                textColor: Material.hintTextColor
                gridColor: Material.dividerColor
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                leftPadding: 4
                wrapMode: Text.Wrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("Each line has its own scale. Tap a sensor to show its scale.")
            }
            Button {
                flat: true
                text: qsTr("Choose…")
                onClicked: view.chooseRequested()
            }
        }

        ListView {
            id: legend
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: ObdSession.pinnedSensors
            ScrollIndicator.vertical: ScrollIndicator {}

            onCountChanged: {
                if (view.focusIndex >= count)
                    view.focusIndex = 0
            }

            delegate: ItemDelegate {
                id: item

                required property int index
                required property string name
                required property string unit
                required property var series

                width: ListView.view.width
                highlighted: index === view.focusIndex
                onClicked: view.focusIndex = index

                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        implicitWidth: 14
                        implicitHeight: 14
                        radius: 3
                        color: item.series.color
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Qt.tint(item.series.color, "#99ffffff")
                            text: item.name
                        }
                        Label {
                            opacity: 0.55
                            font.pixelSize: 11
                            visible: text !== ""
                            text: item.series.rangeText
                        }
                    }
                    Label {
                        font.pixelSize: 18
                        font.bold: true
                        color: item.series.color
                        text: item.series.valueText
                    }
                    Label {
                        font.pixelSize: 12
                        color: item.series.color
                        opacity: 0.75
                        text: item.unit
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: ObdSession.state === ObdSession.Running && legend.count === 0
        spacing: 12
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("No sensors chosen for comparing.")
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Choose sensors")
            onClicked: view.chooseRequested()
        }
    }
}

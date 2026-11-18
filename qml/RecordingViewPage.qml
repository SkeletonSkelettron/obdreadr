pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// One recording: a summary of the drive and a chart per sensor.
Page {
    id: page

    signal backRequested()

    property string path

    RecordingViewer {
        id: viewer
        path: page.path
    }

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
                elide: Text.ElideRight
                font.bold: true
                text: viewer.title !== "" ? viewer.title : qsTr("Recording")
            }
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 10
        topMargin: 12
        bottomMargin: 12
        leftMargin: 12
        rightMargin: 12
        model: viewer.series
        ScrollIndicator.vertical: ScrollIndicator {}

        header: ColumnLayout {
            width: list.width - 24
            spacing: 0

            Label {
                Layout.fillWidth: true
                visible: viewer.error !== ""
                wrapMode: Text.Wrap
                color: "#EF9A9A"
                text: viewer.error
            }

            Pane {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                visible: viewer.loaded
                padding: 14
                Material.elevation: 2

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    rowSpacing: 10
                    columnSpacing: 12

                    Repeater {
                        model: viewer.summary
                        delegate: ColumnLayout {
                            id: tile
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            spacing: 0
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 12
                                opacity: 0.7
                                text: tile.modelData.label
                            }
                            Label {
                                Layout.fillWidth: true
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 12
                                font.pixelSize: 20
                                font.bold: true
                                color: "#90CAF9"
                                text: tile.modelData.value
                            }
                        }
                    }
                }
            }
        }

        delegate: Pane {
            id: card

            required property var modelData
            property bool expanded: false

            width: ListView.view.width - 24
            implicitHeight: expanded ? 320 : 180
            padding: 12
            Material.elevation: 2

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Qt.tint(card.modelData.color, "#99ffffff")
                        text: card.modelData.name
                    }
                    Label {
                        font.pixelSize: 12
                        color: card.modelData.color
                        opacity: 0.8
                        text: card.modelData.unit
                    }
                }
                LineChart {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    series: card.modelData
                    live: false
                    endTime: viewer.endTime
                    timeSpan: Math.max(5, viewer.endTime - viewer.startTime)
                    lineColor: card.modelData.color
                    textColor: Material.hintTextColor
                    gridColor: Material.dividerColor
                }
            }

            TapHandler {
                onTapped: card.expanded = !card.expanded
            }
        }
    }
}

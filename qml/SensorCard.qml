import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

Pane {
    id: card

    required property string name
    required property string unit
    required property string pidText
    required property var series

    property bool expanded: false
    readonly property color sensorColor: series ? series.color : Material.accent

    implicitHeight: expanded ? 340 : 180
    padding: 12
    Material.elevation: 3

    Behavior on implicitHeight {
        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    color: Qt.tint(card.sensorColor, "#99ffffff")
                    text: card.name
                }
                Label {
                    opacity: 0.5
                    font.pixelSize: 11
                    text: card.pidText
                }
            }
            Label {
                font.pixelSize: 28
                font.bold: true
                color: card.sensorColor
                text: card.series ? card.series.valueText : "–"
            }
            Label {
                Layout.alignment: Qt.AlignBottom
                bottomPadding: 5
                font.pixelSize: 14
                color: card.sensorColor
                opacity: 0.75
                text: card.unit
            }
        }

        LineChart {
            Layout.fillWidth: true
            Layout.fillHeight: true
            series: card.series
            timeSpan: ObdSession.windowSeconds
            live: ObdSession.state === ObdSession.Running && !ObdSession.paused
            lineColor: card.sensorColor
            textColor: Material.hintTextColor
            gridColor: Material.dividerColor
        }
    }

    TapHandler {
        onTapped: card.expanded = !card.expanded
    }
}

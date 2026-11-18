pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// A head-up display: speed, engine speed and consumption, large, on black;
// mirrored for reading as a reflection in the windscreen.
Page {
    id: page

    signal backRequested()

    property bool flipped: false

    readonly property var speed: {
        ObdSession.sensorCount // a new connection makes new series
        return ObdSession.seriesFor(0x0D)
    }
    readonly property var rpm: {
        ObdSession.sensorCount
        return ObdSession.seriesFor(0x0C)
    }

    Component.onCompleted: {
        ObdSession.focusPids = [0x0D, 0x0C]
        ObdSession.setFullBrightness(true)
    }
    Component.onDestruction: {
        ObdSession.focusPids = []
        ObdSession.setFullBrightness(false)
    }

    background: Rectangle {
        color: "black"
    }

    ColumnLayout {
        id: hud
        anchors.fill: parent
        anchors.margins: 16
        spacing: 0

        transform: Scale {
            origin.x: hud.width / 2
            origin.y: hud.height / 2
            yScale: page.flipped ? -1 : 1
        }

        Item { Layout.fillHeight: true }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 48
            font.pixelSize: 200
            font.bold: true
            color: "#80DEEA"
            text: page.speed && page.speed.hasValue ? page.speed.valueText : "–"
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            font.pixelSize: 28
            color: "#80DEEA"
            opacity: 0.8
            text: qsTr("km/h")
        }
        Item { Layout.preferredHeight: 24 }
        RowLayout {
            Layout.fillWidth: true
            spacing: 24
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 0
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    font.pixelSize: 56
                    font.bold: true
                    color: "#FFCC80"
                    text: page.rpm && page.rpm.hasValue ? page.rpm.valueText : "–"
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    font.pixelSize: 20
                    color: "#FFCC80"
                    opacity: 0.8
                    text: qsTr("rpm")
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 0
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    font.pixelSize: 56
                    font.bold: true
                    color: "#A5D6A7"
                    text: ObdSession.fuel.nowValue
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    font.pixelSize: 20
                    color: "#A5D6A7"
                    opacity: 0.8
                    text: ObdSession.fuel.nowUnit
                }
            }
        }
        Item { Layout.fillHeight: true }
    }

    // Controls, dim so they do not show in the reflection much.
    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        opacity: 0.45

        ToolButton {
            text: "‹"
            font.pixelSize: 30
            onClicked: page.backRequested()
        }
        Item { Layout.fillWidth: true }
        Switch {
            // Lying on the dashboard with its top toward the windscreen, the
            // phone shows upside down in the glass; this flips it back.
            text: qsTr("Mirror for the windscreen")
            checked: page.flipped
            onToggled: page.flipped = checked
        }
    }
}

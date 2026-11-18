pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// The 0–100 km/h timer.
Page {
    id: page

    signal backRequested()

    readonly property var accel: ObdSession.accel
    readonly property var speed: {
        ObdSession.sensorCount // a new connection makes new series
        return ObdSession.seriesFor(0x0D)
    }

    Component.onCompleted: {
        ObdSession.focusPids = [0x0D]
        ObdSession.accelActive = true
    }
    Component.onDestruction: {
        ObdSession.accelActive = false
        ObdSession.focusPids = []
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
                font.bold: true
                text: qsTr("0–100 km/h")
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: 18
            color: page.accel.state === "ready" ? "#A5D6A7" : page.accel.state === "aborted" ? "#FFE082" : Material.foreground
            text: ObdSession.state !== ObdSession.Running ? qsTr("Connect to the car first.")
                  : page.speed === null ? qsTr("The car does not report its speed.")
                  : page.accel.state === "idle" ? qsTr("Stop the car to arm the timer.")
                  : page.accel.state === "ready" ? qsTr("Ready: the timer starts when the car moves.")
                  : page.accel.state === "running" ? qsTr("Go!")
                  : page.accel.state === "done" ? qsTr("Done. Stop again for another run.")
                  : qsTr("Run stopped: the car slowed down or took over a minute. Stop again for another run.")
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            font.pixelSize: 96
            font.bold: true
            color: page.accel.state === "done" ? "#A5D6A7" : "#90CAF9"
            text: page.accel.state === "done" ? page.accel.t100.replace(" s", "") : page.accel.elapsed
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            font.pixelSize: 28
            opacity: 0.8
            text: page.speed && page.speed.hasValue ? page.speed.valueText + " km/h" : "– km/h"
        }

        Pane {
            Layout.fillWidth: true
            padding: 14
            Material.elevation: 2

            GridLayout {
                anchors.fill: parent
                columns: 2
                rowSpacing: 8
                Label { text: qsTr("0–60 km/h"); Layout.fillWidth: true }
                Label { text: page.accel.t60 !== "" ? page.accel.t60 : "–"; font.bold: true; color: "#FFCC80" }
                Label { text: qsTr("0–100 km/h"); Layout.fillWidth: true }
                Label { text: page.accel.t100 !== "" ? page.accel.t100 : "–"; font.bold: true; color: "#A5D6A7" }
                Label { text: qsTr("Best 0–100 km/h"); Layout.fillWidth: true }
                Label { text: page.accel.best100 !== "" ? page.accel.best100 : "–"; font.bold: true; color: "#CE93D8" }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            opacity: 0.65
            font.pixelSize: 12
            text: qsTr("Only where it is safe and legal. The speed comes a few times a second, so times are interpolated between readings and are good to about a tenth of a second.")
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Button {
                text: qsTr("Reset")
                onClicked: ObdSession.resetAccel()
            }
            Button {
                flat: true
                enabled: page.accel.best100 !== ""
                text: qsTr("Clear best")
                onClicked: ObdSession.clearAccelBest()
            }
        }

        Item { Layout.fillHeight: true }
    }
}

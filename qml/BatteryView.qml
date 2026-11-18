pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// The battery test around starting the engine.
Item {
    id: view

    readonly property var test: ObdSession.batteryTest
    readonly property string phase: test.phase !== undefined ? test.phase : "idle"
    readonly property bool testing: phase === "resting" || phase === "crank" || phase === "running"
    readonly property var colors: ({ ok: "#A5D6A7", watch: "#FFE082", bad: "#EF9A9A" })
    readonly property var verdictNames: ({ ok: qsTr("OK"), watch: qsTr("Watch"), bad: qsTr("Weak") })

    component Result: RowLayout {
        id: result

        required property string label
        required property string hint
        required property string value
        required property string verdict

        Layout.fillWidth: true
        spacing: 10

        Rectangle {
            implicitWidth: 12
            implicitHeight: 12
            radius: 6
            color: result.verdict !== "" ? view.colors[result.verdict] : "#9E9E9E"
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Label {
                Layout.fillWidth: true
                text: result.label
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                opacity: 0.65
                text: result.hint
            }
        }
        ColumnLayout {
            spacing: 0
            Label {
                Layout.alignment: Qt.AlignRight
                font.pixelSize: 18
                font.bold: true
                color: result.verdict !== "" ? view.colors[result.verdict] : Material.foreground
                text: result.value !== "" ? result.value : "–"
            }
            Label {
                Layout.alignment: Qt.AlignRight
                visible: result.verdict !== ""
                font.pixelSize: 12
                color: result.verdict !== "" ? view.colors[result.verdict] : Material.foreground
                text: result.verdict !== "" ? view.verdictNames[result.verdict] : ""
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight + 24
        ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: column
            x: 12
            y: 12
            width: flick.width - 24
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.75
                font.pixelSize: 12
                text: qsTr("Measures the battery while the engine starts: its voltage at rest, how far it drops while the starter turns, and whether the alternator charges afterwards. Switch the engine off and leave the ignition on, press Start, and start the engine when asked.")
            }

            Pane {
                Layout.fillWidth: true
                padding: 16
                Material.elevation: 3

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        font.pixelSize: view.phase === "crank" ? 30 : 18
                        font.bold: true
                        color: view.phase === "crank" ? "#FFE082" : Material.foreground
                        text: view.phase === "resting" ? qsTr("Measuring the resting voltage… do not start yet")
                              : view.phase === "crank" ? qsTr("Start the engine now")
                              : view.phase === "running" ? qsTr("Engine running: measuring the charging voltage…")
                              : view.phase === "done" ? qsTr("Done")
                              : qsTr("Ready")
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        visible: view.testing && view.test.volts !== ""
                        font.pixelSize: 40
                        font.bold: true
                        color: "#90CAF9"
                        text: view.test.volts !== undefined ? view.test.volts : ""
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: view.test.message !== undefined && view.test.message !== ""
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        opacity: 0.8
                        text: view.test.message !== undefined ? view.test.message : ""
                    }
                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        highlighted: !view.testing
                        enabled: view.testing || ObdSession.state === ObdSession.Running
                        text: view.testing ? qsTr("Cancel") : view.phase === "done" ? qsTr("Test again") : qsTr("Start")
                        onClicked: view.testing ? ObdSession.cancelBatteryTest() : ObdSession.startBatteryTest()
                    }
                }
            }

            Pane {
                Layout.fillWidth: true
                visible: view.phase !== "idle"
                padding: 14
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    Result {
                        label: qsTr("At rest")
                        hint: qsTr("Engine off, ignition on. 12.4 V or more is a charged battery.")
                        value: view.test.rest !== undefined ? view.test.rest : ""
                        verdict: view.test.restVerdict !== undefined ? view.test.restVerdict : ""
                    }
                    Result {
                        label: qsTr("While cranking")
                        hint: qsTr("The lowest voltage while the starter turns. Under 9.6 V the battery is weak or the cable connections are poor.")
                        value: view.test.cranking !== undefined ? view.test.cranking : ""
                        verdict: view.test.crankVerdict !== undefined ? view.test.crankVerdict : ""
                    }
                    Result {
                        label: qsTr("Charging")
                        hint: qsTr("5–10 s after the start. 13.5–14.8 V means the alternator charges.")
                        value: view.test.charging !== undefined ? view.test.charging : ""
                        verdict: view.test.chargeVerdict !== undefined ? view.test.chargeVerdict : ""
                    }
                }
            }
        }
    }
}

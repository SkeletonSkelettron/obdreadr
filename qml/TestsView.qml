pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// The car's own test results (mode 06): each monitored part's measured
// value against the limit at which the car would set a fault code.
Item {
    id: view

    readonly property bool running: ObdSession.state === ObdSession.Running
    readonly property var colors: ({ pass: "#A5D6A7", fail: "#EF9A9A", info: "#90CAF9" })

    onVisibleChanged: {
        if (visible && view.running && !ObdSession.testsRead && !ObdSession.testsBusy)
            ObdSession.readTests()
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 6
        bottomMargin: 12
        model: ObdSession.testResults
        ScrollIndicator.vertical: ScrollIndicator {}

        header: ColumnLayout {
            width: list.width
            spacing: 6

            Label {
                Layout.fillWidth: true
                padding: 16
                bottomPadding: 0
                wrapMode: Text.Wrap
                opacity: 0.75
                font.pixelSize: 12
                text: qsTr("The car's own measurements of its emission parts, each with the limit at which it would set a fault code. A value close to its limit shows a part that is wearing out before any code. Misfire counts per cylinder show misfires too rare for a code.")
            }
            RowLayout {
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Button {
                    highlighted: true
                    text: qsTr("Read")
                    enabled: view.running && !ObdSession.testsBusy
                    onClicked: ObdSession.readTests()
                }
                BusyIndicator {
                    implicitWidth: 32
                    implicitHeight: 32
                    running: ObdSession.testsBusy
                    visible: running
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    text: ObdSession.testsStatus !== "" ? ObdSession.testsStatus
                          : view.running ? "" : qsTr("Connect to the car to read the test results.")
                }
            }
        }

        delegate: ColumnLayout {
            id: item

            required property var modelData
            required property int index
            readonly property color statusColor: view.colors[modelData.status]
            // The results come grouped by monitor; name each group once.
            readonly property bool firstOfMonitor: index === 0
                                                   || ObdSession.testResults[index - 1].monitor !== modelData.monitor

            x: 12
            width: list.width - 24
            spacing: 6

            Label {
                Layout.fillWidth: true
                Layout.topMargin: 8
                visible: item.firstOfMonitor
                font.bold: true
                color: "#90CAF9"
                text: item.modelData.monitor
            }

            Pane {
                Layout.fillWidth: true
                padding: 12
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Qt.tint(item.statusColor, "#99ffffff")
                            text: item.modelData.test
                        }
                        Label {
                            font.bold: true
                            font.pixelSize: 16
                            color: item.statusColor
                            text: item.modelData.value
                        }
                    }
                    Label {
                        visible: text !== ""
                        font.pixelSize: 12
                        opacity: 0.7
                        text: item.modelData.limits
                    }
                    // Where the value is between the limits.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        visible: item.modelData.position >= 0
                        implicitHeight: 6
                        radius: 3
                        color: Qt.rgba(1, 1, 1, 0.12)
                        Rectangle {
                            width: Math.max(6, parent.width * item.modelData.position)
                            height: parent.height
                            radius: 3
                            color: item.statusColor
                        }
                    }
                }
            }
        }
    }
}

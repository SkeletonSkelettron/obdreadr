pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

Page {
    id: page

    signal backRequested()
    signal logRequested()
    signal diagnosticsRequested()
    signal recordingsRequested()
    signal accelRequested()
    signal settingsRequested()
    signal hudRequested()

    readonly property bool running: ObdSession.state === ObdSession.Running
    readonly property bool failed: ObdSession.state === ObdSession.Failed
    readonly property bool busy: ObdSession.state === ObdSession.Connecting
                                 || ObdSession.state === ObdSession.Initializing
                                 || ObdSession.state === ObdSession.Discovering
    readonly property var windows: [30, 60, 120, 300, 600]
    // The Charts and Overlay tabs show time, so they get the window selector.
    readonly property bool showsTime: viewTabs.currentIndex === 0 || viewTabs.currentIndex === 2

    header: ColumnLayout {
        spacing: 0

        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 0

                ToolButton {
                    text: "‹"
                    font.pixelSize: 30
                    onClicked: page.backRequested()
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.bold: true
                        text: ObdSession.connectionName
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        opacity: 0.75
                        text: {
                            let parts = [ObdSession.stateText]
                            if (page.running)
                                parts.push(qsTr("%1/s").arg(ObdSession.sampleRate.toFixed(0)))
                            if (ObdSession.recording)
                                parts.push(qsTr("● REC"))
                            if (ObdSession.protocol !== "")
                                parts.push(ObdSession.protocol)
                            return parts.join("  ·  ")
                        }
                    }
                }

                ToolButton {
                    visible: page.running
                    text: ObdSession.paused ? qsTr("Resume") : qsTr("Pause")
                    onClicked: ObdSession.paused = !ObdSession.paused
                }
                ToolButton {
                    text: "⋮"
                    font.pixelSize: 22
                    onClicked: menu.open()

                    Menu {
                        id: menu
                        y: parent.height
                        MenuItem {
                            text: qsTr("Charted sensors…")
                            enabled: ObdSession.sensorCount > 0
                            onTriggered: sensorPicker.open()
                        }
                        MenuItem {
                            text: qsTr("Compared sensors…")
                            enabled: ObdSession.sensorCount > 0
                            onTriggered: pinPicker.open()
                        }
                        MenuItem {
                            text: qsTr("Diagnostics…")
                            onTriggered: page.diagnosticsRequested()
                        }
                        MenuItem {
                            text: qsTr("Recordings…")
                            onTriggered: page.recordingsRequested()
                        }
                        MenuItem {
                            text: qsTr("0–100 km/h timer…")
                            onTriggered: page.accelRequested()
                        }
                        MenuItem {
                            text: qsTr("Head-up display…")
                            onTriggered: page.hudRequested()
                        }
                        MenuItem {
                            text: qsTr("Settings…")
                            onTriggered: page.settingsRequested()
                        }
                        MenuItem {
                            text: qsTr("Record all sensors")
                            checkable: true
                            checked: ObdSession.recordAll
                            onTriggered: ObdSession.recordAll = checked
                        }
                        MenuItem {
                            text: qsTr("Log")
                            onTriggered: page.logRequested()
                        }
                        MenuItem {
                            text: qsTr("Reconnect")
                            onTriggered: ObdSession.reconnect()
                        }
                    }
                }
            }
        }

        TabBar {
            id: viewTabs
            Layout.fillWidth: true
            currentIndex: ObdSession.viewTab

            Repeater {
                model: [qsTr("Charts"), qsTr("Values"), qsTr("Overlay"), qsTr("Selected"), qsTr("Fuel")]
                TabButton {
                    required property string modelData
                    required property int index
                    // Five tabs fit a narrow phone with less padding.
                    leftPadding: 4
                    rightPadding: 4
                    text: modelData
                    onClicked: ObdSession.viewTab = index
                }
            }
        }
    }

    footer: TabBar {
        visible: page.showsTime
        currentIndex: Math.max(0, page.windows.indexOf(ObdSession.windowSeconds))
        Repeater {
            model: [qsTr("30 s"), qsTr("1 min"), qsTr("2 min"), qsTr("5 min"), qsTr("10 min")]
            TabButton {
                required property string modelData
                required property int index
                text: modelData
                onClicked: ObdSession.windowSeconds = page.windows[index]
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.fillWidth: true
            visible: page.failed
            Material.background: "#5c2b29"

            ColumnLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#FFCDD2"
                    text: ObdSession.statusText
                }
                Label {
                    Layout.fillWidth: true
                    visible: ObdSession.reconnecting
                    wrapMode: Text.Wrap
                    color: "#FFCDD2"
                    opacity: 0.8
                    font.pixelSize: 12
                    text: qsTr("Reconnecting automatically in a few seconds. Go back to stop.")
                }
                RowLayout {
                    Button {
                        text: qsTr("Retry")
                        highlighted: true
                        onClicked: ObdSession.reconnect()
                    }
                    Button {
                        text: qsTr("Log")
                        flat: true
                        onClicked: page.logRequested()
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: viewTabs.currentIndex

            ChartsView {
                onChooseRequested: sensorPicker.open()
            }
            ValuesView {
                onChooseRequested: sensorPicker.open()
            }
            OverlayView {
                onChooseRequested: pinPicker.open()
            }
            BigValuesView {
                onChooseRequested: pinPicker.open()
            }
            FuelView {}
        }
    }

    // Progress while connecting.
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 48
        spacing: 16
        visible: page.busy

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: page.busy
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: ObdSession.statusText
        }
    }

    SensorPicker {
        id: sensorPicker
        width: Math.min(page.width * 0.88, 400)
        role: "active"
        title: qsTr("Charted sensors (%1)").arg(ObdSession.sensorCount)
        hint: qsTr("Shown on the Charts and Values tabs. Each extra sensor slows down the updates of the others.")
    }

    SensorPicker {
        id: pinPicker
        width: Math.min(page.width * 0.88, 400)
        role: "pinned"
        title: qsTr("Compared sensors")
        hint: qsTr("Shown together on the Overlay tab and large on the Selected tab. A few sensors update fastest.")
    }
}

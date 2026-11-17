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

    readonly property bool running: ObdSession.state === ObdSession.Running
    readonly property bool failed: ObdSession.state === ObdSession.Failed
    readonly property bool busy: ObdSession.state === ObdSession.Connecting
                                 || ObdSession.state === ObdSession.Initializing
                                 || ObdSession.state === ObdSession.Discovering
    readonly property var windows: [30, 60, 120, 300, 600]

    header: ToolBar {
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
                        text: qsTr("Sensors…")
                        enabled: ObdSession.sensorCount > 0
                        onTriggered: sensorDrawer.open()
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

    footer: TabBar {
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

    ListView {
        id: list
        anchors.fill: parent
        topMargin: 12
        bottomMargin: 12
        leftMargin: 12
        rightMargin: 12
        spacing: 12
        clip: true
        model: ObdSession.activeSensors
        cacheBuffer: 600
        ScrollIndicator.vertical: ScrollIndicator {}

        header: Pane {
            width: list.width - 24
            visible: page.failed
            height: visible ? implicitHeight : 0
            Material.background: "#5c2b29"

            ColumnLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: ObdSession.statusText
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

        delegate: SensorCard {
            width: ListView.view.width - 24
        }
    }

    // Progress while connecting, and hints when there is nothing to chart.
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 48
        spacing: 16
        visible: page.busy || (page.running && list.count === 0)

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: page.busy
            visible: running
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: page.busy ? ObdSession.statusText : qsTr("No sensors selected.")
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: page.running
            text: qsTr("Choose sensors")
            onClicked: sensorDrawer.open()
        }
    }

    Drawer {
        id: sensorDrawer
        edge: Qt.RightEdge
        width: Math.min(page.width * 0.88, 400)

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Label {
                Layout.fillWidth: true
                padding: 16
                font.pixelSize: 18
                font.bold: true
                text: qsTr("Sensors (%1)").arg(ObdSession.sensorCount)
            }
            Label {
                Layout.fillWidth: true
                leftPadding: 16
                rightPadding: 16
                visible: ObdSession.vin !== ""
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("VIN %1").arg(ObdSession.vin)
            }
            Label {
                Layout.fillWidth: true
                leftPadding: 16
                rightPadding: 16
                topPadding: 4
                wrapMode: Text.Wrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("Each extra sensor slows down the updates of the others.")
            }
            RowLayout {
                Layout.leftMargin: 8
                Button {
                    flat: true
                    text: qsTr("All")
                    onClicked: ObdSession.setAllSensorsActive(true)
                }
                Button {
                    flat: true
                    text: qsTr("None")
                    onClicked: ObdSession.setAllSensorsActive(false)
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: ObdSession.sensors
                ScrollIndicator.vertical: ScrollIndicator {}

                delegate: CheckDelegate {
                    id: check
                    required property var model
                    required property string name
                    required property string pidText
                    required property bool active
                    required property bool available

                    width: ListView.view.width
                    enabled: available
                    checked: active
                    text: available ? name : qsTr("%1 (no data)").arg(name)
                    onToggled: {
                        model.active = checked
                        checked = Qt.binding(() => check.active)
                    }
                }
            }
        }
    }
}

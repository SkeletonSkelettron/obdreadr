pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

ApplicationWindow {
    id: window

    width: 420
    height: 860
    visible: true
    title: qsTr("OBD Reader")

    Material.theme: Material.Dark
    Material.accent: Material.Teal
    Material.primary: Material.BlueGrey

    function leaveDashboard() {
        ObdSession.disconnectAdapter()
        stack.pop(null)
    }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: ConnectPage {
            onCustomSensorsRequested: stack.push(customSensorsPage)
            onRecordingsRequested: stack.push(recordingsPage)
            onFuelRequested: stack.push(fuelPage)
        }
    }

    Component {
        id: fuelPage
        FuelPage {
            onBackRequested: stack.pop()
        }
    }

    Component {
        id: dashboardPage
        DashboardPage {
            onBackRequested: window.leaveDashboard()
            onLogRequested: stack.push(logPage)
            onDiagnosticsRequested: stack.push(diagnosticsPage)
            onRecordingsRequested: stack.push(recordingsPage)
            onAccelRequested: stack.push(accelPage)
            onHudRequested: stack.push(hudPage)
        }
    }

    Component {
        id: accelPage
        AccelPage {
            onBackRequested: stack.pop()
        }
    }

    Component {
        id: hudPage
        HudPage {
            onBackRequested: stack.pop()
        }
    }

    Component {
        id: logPage
        LogPage {
            onBackRequested: stack.pop()
        }
    }

    Component {
        id: diagnosticsPage
        DiagnosticsPage {
            onBackRequested: stack.pop()
        }
    }

    Component {
        id: recordingsPage
        RecordingsPage {
            onBackRequested: stack.pop()
            onViewRequested: (path) => stack.push(recordingViewPage, { path: path })
        }
    }

    Component {
        id: customSensorsPage
        CustomSensorsPage {
            onBackRequested: stack.pop()
        }
    }

    Component {
        id: recordingViewPage
        RecordingViewPage {
            onBackRequested: stack.pop()
        }
    }

    // Open the dashboard as soon as a connection starts; it shows the progress.
    Connections {
        target: ObdSession
        function onStateChanged() {
            if (ObdSession.state === ObdSession.Connecting && stack.depth === 1)
                stack.push(dashboardPage)
        }
        function onAlertRaised(title, text) {
            alertBanner.show(title, text)
        }
    }

    // Alerts while driving, over every page; tap to dismiss.
    Pane {
        id: alertBanner

        property string title
        property string text

        function show(title, text) {
            alertBanner.title = title
            alertBanner.text = text
            visible = true
            hideTimer.restart()
        }

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        z: 100
        visible: false
        padding: 14
        Material.background: "#5c2b29"
        Material.elevation: 8

        ColumnLayout {
            anchors.fill: parent
            spacing: 4
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.bold: true
                font.pixelSize: 17
                color: "#FFCDD2"
                text: alertBanner.title
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#FFEBEE"
                text: alertBanner.text
            }
        }

        TapHandler {
            onTapped: alertBanner.visible = false
        }
        Timer {
            id: hideTimer
            interval: 12000
            onTriggered: alertBanner.visible = false
        }
    }

    // Android's back button.
    onClosing: (close) => {
        if (stack.depth > 2) {
            close.accepted = false
            stack.pop()
        } else if (stack.depth === 2) {
            close.accepted = false
            window.leaveDashboard()
        }
    }
}

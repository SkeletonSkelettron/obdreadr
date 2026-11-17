pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
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
        initialItem: ConnectPage {}
    }

    Component {
        id: dashboardPage
        DashboardPage {
            onBackRequested: window.leaveDashboard()
            onLogRequested: stack.push(logPage)
            onCodesRequested: stack.push(codesPage)
        }
    }

    Component {
        id: logPage
        LogPage {
            onBackRequested: stack.pop()
        }
    }

    Component {
        id: codesPage
        CodesPage {
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

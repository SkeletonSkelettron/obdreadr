pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// Fault codes, health checks on the live data, readiness monitors and the
// car's own test results.
Page {
    id: page

    signal backRequested()

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
                Label {
                    Layout.fillWidth: true
                    font.bold: true
                    text: qsTr("Diagnostics")
                }
            }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Codes") }
            TabButton { text: qsTr("Health") }
            TabButton { text: qsTr("Readiness") }
            TabButton { text: qsTr("Tests") }
            TabButton { text: qsTr("Battery") }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: tabs.currentIndex

        CodesView {}
        HealthView {}
        ReadinessView {}
        TestsView {}
        BatteryView {}
    }
}

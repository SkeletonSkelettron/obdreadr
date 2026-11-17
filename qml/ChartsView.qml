pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ObdReadr

// One chart per charted sensor.
Item {
    id: view

    signal chooseRequested()

    ListView {
        id: list
        anchors.fill: parent
        topMargin: 12
        bottomMargin: 12
        leftMargin: 12
        rightMargin: 12
        spacing: 12
        clip: true
        cacheBuffer: 600
        model: ObdSession.activeSensors
        ScrollIndicator.vertical: ScrollIndicator {}

        delegate: SensorCard {
            width: ListView.view.width - 24
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: ObdSession.state === ObdSession.Running && list.count === 0
        spacing: 12
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("No sensors selected.")
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Choose sensors")
            onClicked: view.chooseRequested()
        }
    }
}

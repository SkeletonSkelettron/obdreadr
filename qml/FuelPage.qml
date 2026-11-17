pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ObdReadr

// The fuel figures without a connection: total, measurements, settings.
Page {
    id: page

    signal backRequested()

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
                text: qsTr("Fuel consumption")
            }
        }
    }

    FuelView {
        anchors.fill: parent
    }
}

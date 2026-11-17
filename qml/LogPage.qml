pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

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
                text: qsTr("Adapter log")
            }
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        model: ObdSession.log
        ScrollIndicator.vertical: ScrollIndicator {}

        delegate: Label {
            required property string display
            width: ListView.view.width
            leftPadding: 10
            rightPadding: 10
            wrapMode: Text.WrapAnywhere
            font.family: "monospace"
            font.pixelSize: 12
            text: display
        }

        onCountChanged: positionViewAtEnd()
        Component.onCompleted: positionViewAtEnd()
    }
}

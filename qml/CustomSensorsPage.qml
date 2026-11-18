pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// The user's own sensors: a request (often mode 22) and a formula.
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
                text: qsTr("Custom sensors")
            }
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 8
        bottomMargin: 12
        model: ObdSession.customSensors
        ScrollIndicator.vertical: ScrollIndicator {}

        header: ColumnLayout {
            width: list.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                padding: 16
                bottomPadding: 0
                wrapMode: Text.Wrap
                opacity: 0.75
                font.pixelSize: 12
                text: qsTr("Sensors the car answers besides the standard ones, such as a gearbox temperature or an oil level from your make's list of mode 22 data identifiers. Enter the ECU's CAN header (e.g. 7E1 for the transmission, or empty), the request in hex and a formula over the answer's data bytes A to H. Changes apply on the next connection.")
            }
            Button {
                Layout.leftMargin: 12
                Layout.bottomMargin: 4
                highlighted: true
                text: qsTr("Add a sensor")
                onClicked: editor.edit(-1, { name: "", unit: "", header: "", command: "", formula: "", min: 0, max: 100 })
            }
        }

        delegate: Pane {
            id: item

            required property var modelData
            required property int index

            x: 12
            width: list.width - 24
            padding: 12
            Material.elevation: 2

            RowLayout {
                anchors.fill: parent
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    TapHandler {
                        onTapped: editor.edit(item.index, item.modelData)
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.bold: true
                        color: "#90CAF9"
                        text: item.modelData.name + (item.modelData.unit !== "" ? "  [" + item.modelData.unit + "]" : "")
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        font.family: "monospace"
                        opacity: 0.75
                        text: (item.modelData.header !== "" ? item.modelData.header + " · " : "")
                              + item.modelData.command + "  →  " + item.modelData.formula
                    }
                }
                ToolButton {
                    text: "✕"
                    onClicked: ObdSession.removeCustomSensor(item.index)
                }
            }
        }
    }

    Dialog {
        id: editor

        property int index: -1

        function edit(index, sensor) {
            editor.index = index
            nameField.text = sensor.name
            unitField.text = sensor.unit
            headerField.text = sensor.header
            commandField.text = sensor.command
            formulaField.text = sensor.formula
            minField.text = String(sensor.min)
            maxField.text = String(sensor.max)
            errorLabel.text = ""
            open()
        }

        function values() {
            return {
                name: nameField.text, unit: unitField.text, header: headerField.text,
                command: commandField.text, formula: formulaField.text,
                min: Number(minField.text.replace(",", ".")), max: Number(maxField.text.replace(",", "."))
            }
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 440) // the overlay
        modal: true
        title: editor.index < 0 ? qsTr("New sensor") : qsTr("Edit sensor")

        contentItem: ColumnLayout {
            spacing: 4

            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Name, e.g. Gearbox temperature")
            }
            TextField {
                id: unitField
                Layout.fillWidth: true
                placeholderText: qsTr("Unit, e.g. °C")
            }
            TextField {
                id: headerField
                Layout.fillWidth: true
                placeholderText: qsTr("CAN header, e.g. 7E1 (empty: any ECU)")
                inputMethodHints: Qt.ImhPreferUppercase | Qt.ImhNoPredictiveText
            }
            TextField {
                id: commandField
                Layout.fillWidth: true
                placeholderText: qsTr("Request in hex, e.g. 221105")
                inputMethodHints: Qt.ImhPreferUppercase | Qt.ImhNoPredictiveText
            }
            TextField {
                id: formulaField
                Layout.fillWidth: true
                placeholderText: qsTr("Formula, e.g. A-40 or (A*256+B)/4")
                inputMethodHints: Qt.ImhNoPredictiveText
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: minField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Chart minimum")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                TextField {
                    id: maxField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Chart maximum")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
            }
            Label {
                id: errorLabel
                Layout.fillWidth: true
                visible: text !== ""
                wrapMode: Text.Wrap
                color: "#EF9A9A"
            }
        }

        footer: DialogButtonBox {
            Button {
                flat: true
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                flat: true
                text: qsTr("Save")
                onClicked: {
                    const error = ObdSession.saveCustomSensor(editor.index, editor.values())
                    if (error === "")
                        editor.close()
                    else
                        errorLabel.text = error
                }
            }
        }
    }
}

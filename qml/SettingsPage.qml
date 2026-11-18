pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// Every setting that is not about one tab: connecting, recording, alerts,
// the car Bluetooth reminder, custom sensors and the language.
Page {
    id: page

    signal backRequested()
    signal customSensorsRequested()

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
                text: qsTr("Settings")
            }
        }
    }

    component Card: Pane {
        Layout.fillWidth: true
        padding: 14
        Material.elevation: 2
    }

    component Title: Label {
        font.pixelSize: 16
        font.bold: true
    }

    component Hint: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 12
        opacity: 0.65
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight + 24
        ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: column
            x: 12
            y: 12
            width: flick.width - 24
            spacing: 12

            // Connection -----------------------------------------------------
            Card {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Title {
                        text: qsTr("Connection")
                    }
                    Switch {
                        Layout.fillWidth: true
                        text: qsTr("Connect to the last adapter when the app starts")
                        checked: ObdSession.autoConnect
                        onToggled: ObdSession.autoConnect = checked
                    }
                    Switch {
                        Layout.fillWidth: true
                        visible: ObdSession.backgroundSupported
                        text: qsTr("Keep the screen on while connected")
                        checked: ObdSession.keepScreenOn
                        onToggled: ObdSession.keepScreenOn = checked
                    }
                    Switch {
                        Layout.fillWidth: true
                        visible: ObdSession.backgroundSupported
                        text: qsTr("Keep reading in the background")
                        checked: ObdSession.background
                        onToggled: ObdSession.background = checked
                    }
                    Hint {
                        visible: ObdSession.backgroundSupported
                        text: qsTr("While connected, a notification keeps the app reading the car with the screen off or another app open. A lost connection is retried for 15 minutes after the car last answered. Leave the dashboard to disconnect.")
                    }
                    Button {
                        visible: ObdSession.backgroundSupported && ObdSession.background && ObdSession.batteryRestricted
                        text: qsTr("Allow running in the background…")
                        onClicked: ObdSession.allowBackground()
                    }
                }
            }

            // Car Bluetooth reminder -----------------------------------------
            Card {
                visible: ObdSession.backgroundSupported

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Title {
                        text: qsTr("When the car starts")
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: ObdSession.reminderDevice !== ""
                              ? qsTr("Reminder when %1 connects").arg(ObdSession.reminderDeviceName || ObdSession.reminderDevice)
                              : qsTr("Reminder when the car's Bluetooth connects: off")
                    }
                    Hint {
                        text: qsTr("Android does not let an app open itself. Pick the car's own Bluetooth (the stereo, not the OBD adapter): when it connects, a notification opens this app, which then connects by itself.")
                    }
                    Button {
                        text: qsTr("Choose the car's Bluetooth…")
                        onClicked: {
                            ObdSession.loadPairedDevices()
                            reminderDialog.open()
                        }
                    }
                }
            }

            // Recording ------------------------------------------------------
            Card {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Title {
                        text: qsTr("Recording")
                    }
                    Switch {
                        Layout.fillWidth: true
                        text: qsTr("Record all sensors")
                        checked: ObdSession.recordAll
                        onToggled: ObdSession.recordAll = checked
                    }
                    Hint {
                        text: qsTr("Every sensor the car supports is read in the background, each at its own pace. Off: only the sensors on screen are read, and they update faster.")
                    }
                }
            }

            // Alerts ---------------------------------------------------------
            Card {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Title {
                        text: qsTr("Alerts")
                    }
                    Switch {
                        Layout.fillWidth: true
                        text: qsTr("Alerts while driving")
                        checked: ObdSession.alertsEnabled
                        onToggled: ObdSession.alertsEnabled = checked
                    }
                    Hint {
                        visible: ObdSession.alertsEnabled
                        text: qsTr("Overheating (108 °C), a battery that stops charging or overcharges, the check engine light, low fuel and, if set, an engine speed limit.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: ObdSession.alertsEnabled
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: ObdSession.alertRpm > 0 ? qsTr("Engine speed alert") : qsTr("Engine speed alert: off")
                        }
                        SpinBox {
                            from: 0
                            to: 9000
                            stepSize: 250
                            editable: true
                            value: ObdSession.alertRpm
                            onValueModified: ObdSession.alertRpm = value
                        }
                    }
                }
            }

            // Custom sensors ---------------------------------------------------
            Card {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Title {
                        text: qsTr("Custom sensors")
                    }
                    Hint {
                        text: qsTr("%n sensor(s) of your own, such as mode 22 data from your make's list.", "", ObdSession.customSensors.length)
                    }
                    Button {
                        text: qsTr("Custom sensors…")
                        onClicked: page.customSensorsRequested()
                    }
                }
            }

            // Language ---------------------------------------------------------
            Card {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Title {
                        text: qsTr("Language")
                    }
                    ComboBox {
                        id: languageBox
                        readonly property var codes: ["", "en", "ka"]
                        Layout.fillWidth: true
                        // The language names stay as they are in every language.
                        model: [qsTr("Phone's language"), "English", "ქართული"]
                        currentIndex: Math.max(0, codes.indexOf(ObdSession.language))
                        onActivated: (index) => ObdSession.language = codes[index]
                    }
                }
            }
        }
    }

    Dialog {
        id: reminderDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420) // the overlay
        height: Math.min(parent.height - 64, implicitHeight)
        modal: true
        title: qsTr("The car's Bluetooth")
        footer: ConfirmButtons {
            showAccept: false
            rejectText: qsTr("Close")
        }

        contentItem: ListView {
            implicitHeight: contentHeight
            clip: true
            model: [{ name: qsTr("Off"), address: "" }].concat(ObdSession.pairedDevices)
            delegate: RadioDelegate {
                required property var modelData
                width: ListView.view.width
                text: modelData.address !== "" ? modelData.name + "  ·  " + modelData.address : modelData.name
                checked: modelData.address === ObdSession.reminderDevice
                onClicked: {
                    ObdSession.setReminderDevice(modelData.address, modelData.name)
                    reminderDialog.close()
                }
            }
        }
    }
}

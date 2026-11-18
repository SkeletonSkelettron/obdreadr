pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

Page {
    id: page

    signal recordingsRequested()
    signal fuelRequested()
    signal settingsRequested()

    // Connect to the last adapter by itself, once per app start.
    // (Later, so that the stack can open the dashboard when it connects.)
    Component.onCompleted: Qt.callLater(() => ObdSession.connectOnStart())

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 0
            Label {
                Layout.fillWidth: true
                leftPadding: 16
                text: qsTr("OBD Reader")
                font.pixelSize: 20
                font.bold: true
            }
            ToolButton {
                text: qsTr("Settings")
                onClicked: page.settingsRequested()
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 32
        clip: true
        ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: column
            x: 16
            y: 16
            width: parent.width - 32
            spacing: 16

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.8
                text: qsTr("Plug an ELM327 adapter into the car's OBD-II port, turn the ignition on and pick the adapter below. The sensors your car supports are found automatically.")
            }

            Label {
                Layout.fillWidth: true
                visible: text !== ""
                wrapMode: Text.Wrap
                color: Material.accent
                text: ObdSession.statusText
            }

            // Bluetooth -------------------------------------------------------
            Pane {
                Layout.fillWidth: true
                visible: ObdSession.bluetoothAvailable
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: qsTr("Bluetooth adapter")
                        font.pixelSize: 16
                        font.bold: true
                    }

                    Button {
                        Layout.fillWidth: true
                        visible: ObdSession.lastBtAddress !== ""
                        highlighted: true
                        text: qsTr("Connect to %1").arg(ObdSession.lastBtName !== "" ? ObdSession.lastBtName : ObdSession.lastBtAddress)
                        onClicked: ObdSession.connectBluetooth(ObdSession.lastBtAddress, ObdSession.lastBtName, ObdSession.lastBtBle)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            text: ObdSession.scanning ? qsTr("Stop") : qsTr("Scan")
                            onClicked: ObdSession.scanning ? ObdSession.stopScan() : ObdSession.startScan()
                        }
                        BusyIndicator {
                            running: ObdSession.scanning
                            visible: running
                            implicitWidth: 36
                            implicitHeight: 36
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Repeater {
                        model: ObdSession.devices
                        delegate: ItemDelegate {
                            id: deviceItem
                            required property var modelData
                            Layout.fillWidth: true
                            onClicked: ObdSession.connectBluetooth(modelData.address, modelData.name, modelData.ble)
                            contentItem: ColumnLayout {
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font.bold: deviceItem.modelData.likelyObd
                                    text: deviceItem.modelData.name + (deviceItem.modelData.ble ? "  ·  BLE" : "")
                                }
                                Label {
                                    opacity: 0.6
                                    font.pixelSize: 12
                                    text: deviceItem.modelData.address
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !ObdSession.scanning && ObdSession.devices.length === 0
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        text: qsTr("Press Scan to find the adapter. A classic Bluetooth adapter may ask for a PIN, usually 1234 or 0000.")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: addressField
                            Layout.fillWidth: true
                            placeholderText: qsTr("or type its address, e.g. 00:1D:A5:68:98:8B")
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhPreferUppercase
                        }
                        Button {
                            text: qsTr("Connect")
                            enabled: addressField.text.trim().length >= 17
                            onClicked: ObdSession.connectBluetooth(addressField.text.trim(), "", false)
                        }
                    }
                }
            }

            // Wi-Fi ----------------------------------------------------------
            Pane {
                Layout.fillWidth: true
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: qsTr("Wi-Fi adapter")
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        text: qsTr("Join the adapter's Wi-Fi network first. If the phone will not reach it, turn mobile data off.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: hostField
                            Layout.fillWidth: true
                            text: ObdSession.lastHost
                            placeholderText: "192.168.0.10"
                            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                        }
                        TextField {
                            id: portField
                            Layout.preferredWidth: 90
                            text: String(ObdSession.lastPort)
                            placeholderText: "35000"
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 65535 }
                        }
                    }
                    Button {
                        text: qsTr("Connect")
                        onClicked: ObdSession.connectWifi(hostField.text.trim(), parseInt(portField.text))
                    }
                }
            }

            // Demo -----------------------------------------------------------
            Pane {
                Layout.fillWidth: true
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: qsTr("No car nearby?")
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Button {
                        text: qsTr("Start the demo car")
                        onClicked: ObdSession.connectDemo()
                    }
                }
            }

            // Fuel -----------------------------------------------------------
            Pane {
                Layout.fillWidth: true
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: qsTr("Fuel consumption")
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        text: {
                            const total = ObdSession.fuel.total
                            return qsTr("Total %1, %2 L/100 km on average.").arg(total.distance).arg(total.average)
                        }
                    }
                    Button {
                        text: qsTr("Fuel…")
                        onClicked: page.fuelRequested()
                    }
                }
            }

            // Recordings -----------------------------------------------------
            Pane {
                Layout.fillWidth: true
                Material.elevation: 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: qsTr("Recordings")
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        text: qsTr("%n saved drive(s). Export them as CSV for a computer.", "", ObdSession.recordings.length)
                    }
                    Button {
                        text: qsTr("Recordings…")
                        onClicked: page.recordingsRequested()
                    }
                }
            }
        }
    }
}

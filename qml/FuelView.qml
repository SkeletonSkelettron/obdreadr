pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ObdReadr

// Fuel consumption: right now, a measurement started and stopped by hand,
// this drive, the total over every drive, earlier measurements, and the
// settings the estimate needs.
Item {
    id: view

    readonly property FuelMeter fuel: ObdSession.fuel
    readonly property var trip: view.fuel.trip
    readonly property var drive: view.fuel.drive
    readonly property var total: view.fuel.total
    readonly property var history: view.fuel.history
    readonly property var drives: view.fuel.drives
    // The latest finished measurement is in the measurement card.
    readonly property var pastTrips: view.fuel.tripRunning ? view.history : view.history.slice(1)

    // The meter's sensors are read faster while this is on screen.
    Binding {
        target: view.fuel
        property: "onScreen"
        value: view.visible
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

    component Stat: ColumnLayout {
        id: stat

        required property string label
        required property string value

        Layout.fillWidth: true
        Layout.preferredWidth: 1
        spacing: 0

        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.pixelSize: 12
            opacity: 0.7
            text: stat.label
        }
        Label {
            Layout.fillWidth: true
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 14
            font.pixelSize: 24
            font.bold: true
            text: stat.value
        }
    }

    component TotalsGrid: GridLayout {
        id: grid

        required property var totals

        Layout.fillWidth: true
        columns: 2
        rowSpacing: 10
        columnSpacing: 12

        Stat {
            label: qsTr("Distance")
            value: grid.totals.distance
        }
        Stat {
            label: qsTr("Fuel")
            value: grid.totals.fuel
        }
        Stat {
            label: qsTr("Average, L/100 km")
            value: grid.totals.average
        }
        Stat {
            label: qsTr("Time")
            value: grid.totals.time
        }
        Stat {
            visible: grid.totals.cost !== ""
            label: qsTr("Cost")
            value: grid.totals.cost
        }
    }

    // One line for a finished measurement or drive.
    component PastLine: ColumnLayout {
        id: past

        required property var modelData

        Layout.fillWidth: true
        spacing: 0

        Hint {
            text: past.modelData.period
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: {
                let parts = [past.modelData.distance, past.modelData.fuel,
                             qsTr("%1 L/100 km").arg(past.modelData.average), past.modelData.time]
                if (past.modelData.cost !== "")
                    parts.push(past.modelData.cost)
                return parts.join(" · ")
            }
        }
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

            // Now ------------------------------------------------------------
            Card {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 2

                    Title {
                        text: qsTr("Now")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            font.pixelSize: 56
                            font.bold: true
                            color: view.fuel.hasNow ? Material.accent : Material.hintTextColor
                            text: view.fuel.nowValue
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignBaseline
                            font.pixelSize: 18
                            opacity: 0.8
                            text: view.fuel.nowUnit
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        opacity: 0.85
                        text: view.fuel.nowDetail
                    }
                    Hint {
                        Layout.topMargin: 6
                        text: view.fuel.methodText
                    }
                }
            }

            // Measurement ----------------------------------------------------
            Card {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Title {
                            text: view.fuel.tripRunning ? qsTr("Measuring") : qsTr("Measurement")
                        }
                        Hint {
                            visible: text !== ""
                            text: view.trip.period
                        }
                    }
                    TotalsGrid {
                        visible: view.trip.has
                        totals: view.trip
                    }
                    Hint {
                        visible: !view.trip.has
                        text: qsTr("Press Start where the stretch you want to measure begins and Stop where it ends. The measurement carries on across stops and reconnections until you stop it.")
                    }
                    Button {
                        Layout.fillWidth: true
                        highlighted: true
                        Material.accent: view.fuel.tripRunning ? Material.Red : Material.Teal
                        text: view.fuel.tripRunning ? qsTr("Stop measurement") : qsTr("Start measurement")
                        onClicked: view.fuel.tripRunning ? view.fuel.stopTrip() : view.fuel.startTrip()
                    }
                }
            }

            // This drive -----------------------------------------------------
            Card {
                visible: view.fuel.connected

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Title {
                            text: qsTr("This drive")
                        }
                        Hint {
                            text: qsTr("Since connecting")
                        }
                    }
                    TotalsGrid {
                        totals: view.drive
                    }
                }
            }

            // Drives ---------------------------------------------------------
            Card {
                visible: view.drives.length > 0

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Title {
                                text: qsTr("Drives")
                            }
                            Hint {
                                text: qsTr("Each connection to the car, from connecting to leaving the dashboard")
                            }
                        }
                        Button {
                            flat: true
                            text: qsTr("Clear")
                            onClicked: clearDrivesDialog.open()
                        }
                    }
                    Repeater {
                        model: view.drives
                        delegate: PastLine {}
                    }
                }
            }

            // Total ----------------------------------------------------------
            Card {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Title {
                                text: qsTr("Total")
                            }
                            Hint {
                                text: qsTr("Every drive since %1").arg(view.total.since)
                            }
                        }
                        Button {
                            flat: true
                            text: qsTr("Reset")
                            onClicked: resetDialog.open()
                        }
                    }
                    TotalsGrid {
                        totals: view.total
                    }
                }
            }

            // Earlier measurements -------------------------------------------
            Card {
                visible: view.pastTrips.length > 0

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Title {
                            Layout.fillWidth: true
                            text: qsTr("Earlier measurements")
                        }
                        Button {
                            flat: true
                            text: qsTr("Clear")
                            onClicked: clearDialog.open()
                        }
                    }
                    Repeater {
                        model: view.pastTrips
                        delegate: PastLine {}
                    }
                }
            }

            // Settings -------------------------------------------------------
            Card {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Title {
                        text: qsTr("Settings")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Fuel")
                        }
                        ComboBox {
                            model: [qsTr("Petrol"), qsTr("Diesel"), qsTr("LPG")]
                            currentIndex: view.fuel.fuelType
                            onActivated: (index) => view.fuel.fuelType = index
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Engine size")
                        }
                        SpinBox {
                            from: 5
                            to: 100
                            value: Math.round(view.fuel.engineLiters * 10)
                            textFromValue: (value, locale) => (value / 10).toFixed(1) + " L"
                            valueFromText: (text, locale) => Math.round(parseFloat(text) * 10)
                            onValueModified: view.fuel.engineLiters = value / 10
                        }
                    }
                    Hint {
                        text: qsTr("Used only for cars without an air flow sensor.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Correction")
                        }
                        SpinBox {
                            from: 50
                            to: 150
                            value: view.fuel.correction
                            textFromValue: (value, locale) => value + " %"
                            valueFromText: (text, locale) => parseInt(text)
                            onValueModified: view.fuel.correction = value
                        }
                    }
                    Hint {
                        text: qsTr("Fill the tank, measure until the next fill-up, and compare. If the app counted 40 L and the pump 44 L, set 110 %. It applies to every figure shown, the earlier ones too.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Fuel price")
                        }
                        SpinBox {
                            from: 0
                            to: 1000
                            stepSize: 5
                            editable: true
                            value: Math.round(view.fuel.price * 100)
                            textFromValue: (value, locale) => (value / 100).toFixed(2) + " ₾/L"
                            valueFromText: (text, locale) => Math.round(parseFloat(text.replace(",", ".")) * 100)
                            onValueModified: view.fuel.price = value / 100
                        }
                    }
                    Hint {
                        text: qsTr("Shows what the fuel cost; 0 hides it.")
                    }

                    Switch {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        visible: ObdSession.backgroundSupported
                        text: qsTr("Keep counting in the background")
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
        }
    }

    Dialog {
        id: resetDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420) // the overlay
        modal: true
        title: qsTr("Reset the total?")
        standardButtons: Dialog.Yes | Dialog.Cancel

        // As the content item, so that the wrapped text does not make a binding loop.
        contentItem: Label {
            wrapMode: Text.Wrap
            text: qsTr("The total distance and fuel start again from zero. Measurements are kept.")
        }

        onAccepted: view.fuel.resetTotal()
    }

    Dialog {
        id: clearDrivesDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420) // the overlay
        modal: true
        title: qsTr("Clear the drives?")
        standardButtons: Dialog.Yes | Dialog.Cancel

        contentItem: Label {
            wrapMode: Text.Wrap
            text: qsTr("The list of finished drives is deleted. The total stays.")
        }

        onAccepted: view.fuel.clearDrives()
    }

    Dialog {
        id: clearDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420) // the overlay
        modal: true
        title: qsTr("Clear earlier measurements?")
        standardButtons: Dialog.Yes | Dialog.Cancel

        contentItem: Label {
            wrapMode: Text.Wrap
            text: qsTr("The list of earlier measurements is deleted. The latest measurement and the total stay.")
        }

        onAccepted: view.fuel.clearHistory()
    }
}

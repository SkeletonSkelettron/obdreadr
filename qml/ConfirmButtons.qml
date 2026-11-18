import QtQuick
import QtQuick.Controls

// A dialog's buttons with texts of our own, so they follow the app's
// language (the standard buttons take theirs from Qt's translations).
DialogButtonBox {
    id: box

    property string acceptText: qsTr("Yes")
    property string rejectText: qsTr("Cancel")
    property bool showAccept: true

    Button {
        flat: true
        text: box.rejectText
        DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
    }
    Button {
        flat: true
        visible: box.showAccept
        text: box.acceptText
        DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
    }
}

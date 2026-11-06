import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string text: ""
    property string stateId: ""
    property bool value: false

    signal toggleRequested(string stateId, bool newValue)

    implicitHeight: 34
    implicitWidth: row.implicitWidth

    opacity: root.enabled ? 1.0 : 0.45

    RowLayout {
        id: row
        anchors.fill: parent
        spacing: 8

        CheckBox {
            id: checkBox
            checked: root.value
            enabled: root.enabled
            text: root.text
            font.pixelSize: 13
            Layout.fillWidth: true
            Layout.preferredWidth: 260

            onClicked: {
                root.toggleRequested(root.stateId, checked)
            }
        }
    }
}

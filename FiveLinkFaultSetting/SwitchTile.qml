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

        Label {
            text: root.text

            font.pixelSize: 13
            color: "#111111"

            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideRight

            Layout.preferredWidth: 220
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        }

        Item {
            id: swBox

            width: 58
            height: 30

            Layout.preferredWidth: width
            Layout.preferredHeight: height

            property color offColor: "#e9e2ee"
            property color onColor: "#ff4438"
            property color borderColor: "#7f7f7f"

            Rectangle {
                anchors.fill: parent

                radius: height / 2

                color: root.value ? swBox.onColor : swBox.offColor

                border.color: root.value ? swBox.onColor : swBox.borderColor
                border.width: root.value ? 0 : 3
            }

            Rectangle {
                id: knob

                width: 22
                height: 22

                radius: width / 2

                anchors.verticalCenter: parent.verticalCenter

                x: root.value
                   ? swBox.width - width - 4
                   : 4

                color: root.value ? "#ffffff" : "#7a7a7a"

                Behavior on x {
                    NumberAnimation {
                        duration: 150
                        easing.type: Easing.OutCubic
                    }
                }
            }

            MouseArea {
                anchors.fill: parent

                enabled: root.enabled
                cursorShape: Qt.PointingHandCursor

                onClicked: {
                    root.toggleRequested(root.stateId, !root.value)
                }
            }
        }
    }
}

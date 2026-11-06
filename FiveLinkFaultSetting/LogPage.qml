import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    signal requestBack()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "操作日志"
                font.pixelSize: 30
                font.bold: true
                color: "#111111"
                Layout.fillWidth: true
            }

            Button {
                text: "清空日志"
                enabled: Backend.logText.length > 0
                onClicked: Backend.clearLog()
            }

            Button {
                text: "返回"
                onClicked: root.requestBack()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 4
            color: "transparent"
            border.color: "#8c8c8c"
            border.width: 1

            ScrollView {
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                visible: Backend.logText.length > 0

                TextArea {
                    id: logArea
                    text: Backend.logText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextArea.WrapAnywhere
                    font.pixelSize: 14
                    color: "#222222"
                    background: null

                    onTextChanged: {
                        cursorPosition = length
                    }
                }
            }
        }
    }
}

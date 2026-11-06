import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    anchors.fill: parent

    Rectangle { anchors.fill: parent; color: "#fbfbfb" }

    readonly property int rowH: 56
    readonly property int headerH: 48
    readonly property int tableMaxW: Math.min(width - 80, 1260)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: "诊断结果"
                font.pixelSize: 28
                font.bold: true
                color: "#111111"
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#dddddd" }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: "white"
            border.color: "#d0d0d0"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Rectangle {
                        id: tableFrame
                        width: tableMaxW
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        color: "transparent"
                        border.color: "#3a3a3a"
                        border.width: 1
                        radius: 6
                        clip: true

                        Column {
                            id: detailRoot
                            anchors.fill: parent
                            spacing: 0

                            readonly property int colIndexW: 70
                            readonly property int colCategoryW: 120
                            readonly property int colSetW: 140
                            readonly property int colJudgeW: 140
                            readonly property int colResultW: 130
                            readonly property int colPointW: tableFrame.width - colIndexW - colCategoryW - colSetW - colJudgeW - colResultW - 5

                            Row {
                                height: headerH
                                spacing: 1
                                HeaderCell { w: detailRoot.colIndexW; text: "序号" }
                                HeaderCell { w: detailRoot.colCategoryW; text: "故障类别" }
                                HeaderCell { w: detailRoot.colPointW; text: "故障点位" }
                                HeaderCell { w: detailRoot.colSetW; text: "故障类型" }
                                HeaderCell { w: detailRoot.colJudgeW; text: "故障诊断" }
                                HeaderCell { w: detailRoot.colResultW; text: "诊断结果" }
                            }

                            Rectangle { height: 1; width: parent.width; color: "#3a3a3a" }

                            ListView {
                                id: detailList
                                width: parent.width
                                height: parent.height - headerH - 1
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                model: Backend.detailResultModel

                                delegate: Row {
                                    width: detailList.width
                                    height: rowH
                                    spacing: 1

                                    readonly property string _category: (modelData && modelData["categoryText"]) ? modelData["categoryText"] : ""
                                    readonly property string _point: (modelData && modelData["pointText"]) ? modelData["pointText"] : ""
                                    readonly property string _set: (modelData && modelData["setTypeText"]) ? modelData["setTypeText"] : "等待返回"
                                    readonly property string _judge: (modelData && modelData["judgeTypeText"]) ? modelData["judgeTypeText"] : "未选择"
                                    readonly property bool _ok: (modelData && modelData["ok"] !== undefined) ? Boolean(modelData["ok"]) : false
                                    readonly property bool _received: (modelData && modelData["received"] !== undefined) ? Boolean(modelData["received"]) : false

                                    BodyCell { w: detailRoot.colIndexW; text: (index + 1).toString(); bold: true }
                                    BodyCell { w: detailRoot.colCategoryW; text: _category }
                                    BodyCell { w: detailRoot.colPointW; text: _point; alignLeft: true }
                                    BodyCell { w: detailRoot.colSetW; text: _set }
                                    BodyCell { w: detailRoot.colJudgeW; text: _judge }
                                    ResultCell { w: detailRoot.colResultW; ok: _ok; received: _received }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 70
                    radius: 10
                    color: "#f5f5f5"
                    border.color: "#e0e0e0"
                    border.width: 1

                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 18
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14

                        Button {
                            text: "继续诊断"
                            height: 44
                            width: 120
                            onClicked: {
                                Backend.continueDiagnosis(true)
                                if (typeof win !== "undefined") win.selectionLocked = false
                            }
                        }
                        Button {
                            text: "退出诊断"
                            height: 44
                            width: 120
                            onClicked: Qt.quit()
                        }
                    }
                }
            }
        }
    }

    component HeaderCell: Rectangle {
        property int w: 100
        property string text: ""
        width: w
        height: headerH
        color: "#2b2b2b"
        border.color: "#3a3a3a"
        border.width: 1
        Text { anchors.centerIn: parent; text: parent.text; color: "white"; font.bold: true; font.pixelSize: 16 }
    }

    component BodyCell: Rectangle {
        property int w: 100
        property string text: ""
        property bool bold: false
        property bool alignLeft: false
        width: w
        height: rowH
        color: "#ffffff"
        border.color: "#3a3a3a"
        border.width: 1
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.alignLeft ? parent.left : undefined
            anchors.leftMargin: parent.alignLeft ? 12 : 0
            anchors.horizontalCenter: parent.alignLeft ? undefined : parent.horizontalCenter
            width: parent.alignLeft ? parent.width - 24 : parent.width - 8
            text: parent.text
            color: "#111111"
            font.pixelSize: 16
            font.bold: parent.bold
            elide: Text.ElideRight
            horizontalAlignment: parent.alignLeft ? Text.AlignLeft : Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component ResultCell: Rectangle {
        property int w: 100
        property bool ok: false
        property bool received: true
        width: w
        height: rowH
        color: !received ? "#f2f2f2" : (ok ? "#e8f6ec" : "#fdecea")
        border.color: "#3a3a3a"
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: !parent.received ? "等待" : (parent.ok ? "正确" : "错误")
            font.pixelSize: 18
            font.bold: true
            color: !parent.received ? "#666666" : (parent.ok ? "#2ecc71" : "#e74c3c")
        }
    }
}

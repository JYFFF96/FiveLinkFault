import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#fbfbfb"
    border.color: "#d0d0d0"
    border.width: 1

    readonly property int headerH: 34
    readonly property int rowH: 36

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "诊断结果"
                font.pixelSize: 18
                font.bold: true
                color: "#111111"
            }
            Text {
                text: "（R=电源故障，C=通讯故障；ID0 对应 R1-20/C1-8，ID1 对应 R21-40/C9-16）"
                font.pixelSize: 13
                color: "#666666"
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            ResultTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "电源故障诊断结果"
                rows: Backend.powerResultRows
            }

            ResultTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "通讯故障诊断结果"
                rows: Backend.commResultRows
            }
        }
    }

    component ResultTable: Rectangle {
        id: table
        property string title: ""
        property var rows: []

        color: "white"
        radius: 6
        border.color: "#3a3a3a"
        border.width: 1
        clip: true

        readonly property int colNoW: 54
        readonly property int colIdxW: 72
        readonly property int colTypeW: 86
        readonly property int colJudgeW: 86
        readonly property int colResultW: 80
        readonly property int colPointW: Math.max(140, width - colNoW - colIdxW - colTypeW - colJudgeW - colResultW - 5)

        Column {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                width: parent.width
                height: 36
                color: "#f0f0f0"
                border.color: "#3a3a3a"
                Text {
                    anchors.centerIn: parent
                    text: table.title
                    font.pixelSize: 15
                    font.bold: true
                    color: "#111111"
                }
            }

            Row {
                width: parent.width
                height: root.headerH
                spacing: 1
                HeaderCell { w: table.colNoW; text: "序号" }
                HeaderCell { w: table.colIdxW; text: "点位" }
                HeaderCell { w: table.colPointW; text: "故障点位" }
                HeaderCell { w: table.colTypeW; text: "故障类型" }
                HeaderCell { w: table.colJudgeW; text: "故障诊断" }
                HeaderCell { w: table.colResultW; text: "结果" }
            }

            ListView {
                id: list
                width: parent.width
                height: parent.height - 36 - root.headerH
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: table.rows || []

                delegate: Row {
                    width: list.width
                    height: root.rowH
                    spacing: 1

                    readonly property string _point: modelData && modelData.pointName ? modelData.pointName : ""
                    readonly property string _type: modelData && modelData.faultType ? modelData.faultType : "未设置"
                    readonly property string _judge: modelData && modelData.faultDiagnosis ? modelData.faultDiagnosis : "未选择"
                    readonly property string _result: modelData && modelData.diagnosisResult ? modelData.diagnosisResult : "等待"
                    readonly property bool _ok: modelData && modelData.ok !== undefined ? Boolean(modelData.ok) : false
                    readonly property bool _waiting: modelData && modelData.waiting !== undefined ? Boolean(modelData.waiting) : false
                    readonly property int _globalIndex: modelData && modelData.globalIndex !== undefined ? Number(modelData.globalIndex) : (index + 1)

                    BodyCell { w: table.colNoW; text: (index + 1).toString(); bold: true }
                    BodyCell { w: table.colIdxW; text: _globalIndex.toString() }
                    BodyCell { w: table.colPointW; text: _point; alignLeft: true }
                    BodyCell { w: table.colTypeW; text: _type }
                    BodyCell { w: table.colJudgeW; text: _judge }
                    ResultCell { w: table.colResultW; text: _result; ok: _ok; waiting: _waiting }
                }
            }
        }
    }

    component HeaderCell: Rectangle {
        property int w: 80
        property string text: ""
        width: w
        height: root.headerH
        color: "#2b2b2b"
        border.color: "#3a3a3a"
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: "white"
            font.bold: true
            font.pixelSize: 13
        }
    }

    component BodyCell: Rectangle {
        property int w: 80
        property string text: ""
        property bool bold: false
        property bool alignLeft: false
        width: w
        height: root.rowH
        color: "#ffffff"
        border.color: "#3a3a3a"
        border.width: 1
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.alignLeft ? parent.left : undefined
            anchors.leftMargin: parent.alignLeft ? 8 : 0
            anchors.horizontalCenter: parent.alignLeft ? undefined : parent.horizontalCenter
            width: parent.alignLeft ? parent.width - 16 : parent.width - 6
            text: parent.text
            color: "#111111"
            font.pixelSize: 13
            font.bold: parent.bold
            elide: Text.ElideRight
            horizontalAlignment: parent.alignLeft ? Text.AlignLeft : Text.AlignHCenter
        }
    }

    component ResultCell: Rectangle {
        property int w: 80
        property string text: "等待"
        property bool ok: false
        property bool waiting: false
        width: w
        height: root.rowH
        color: waiting ? "#f5f5f5" : (ok ? "#e8f6ec" : "#fdecea")
        border.color: "#3a3a3a"
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: parent.text
            font.pixelSize: 14
            font.bold: true
            color: parent.waiting ? "#666666" : (parent.ok ? "#2ecc71" : "#e74c3c")
        }
    }
}

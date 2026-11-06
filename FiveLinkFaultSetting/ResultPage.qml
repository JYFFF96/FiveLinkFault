import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    anchors.fill: parent

    signal requestBack()

    property int level: 0               // 0: 总表，1: 详情
    property int selectedDiagIndex: -1
    property var detailRows: []

    function resultColor(v) {
        if (v === "正确" || v === "收到") return "#1f8f3a"
        if (v === "错误") return "#d03030"
        return "#111111"
    }

    function openDetail(diagIndex) {
        selectedDiagIndex = diagIndex
        detailRows = Backend.diagDetailResults(diagIndex, "五联动系统")
        level = 1
    }

    function back() {
        level = 0
        selectedDiagIndex = -1
        detailRows = []
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: root.level === 0 ? "诊断结果" : "诊断详情"
                font.pixelSize: 30
                font.bold: true
                color: "#111111"
                Layout.fillWidth: true
            }

            Button {
                text: root.level === 0 ? "返回" : "返回总表"
                onClicked: {
                    if (root.level === 0)
                        root.requestBack()
                    else
                        root.back()
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.level

            TableViewBlock {
                headers: ["序号", "设备名称", "诊断时间", "诊断结果"]
                weights: [1, 4, 3, 2]
                rows: Backend.diagResults || []
                cellText: function(row, col) {
                    if (col === 0) return row.sequence || ""
                    if (col === 1) return row.deviceName || ""
                    if (col === 2) return row.diagnosisTime || ""
                    return row.diagnosisResult || ""
                }
                cellColor: function(row, col) {
                    return col === 3 ? root.resultColor(row.diagnosisResult) : "#111111"
                }
                clickableColumn: 3
                onCellClicked: function(rowIndex, col, row) {
                    if (col === 3)
                        root.openDetail(rowIndex)
                }
            }

            TableViewBlock {
                headers: ["序号", "故障点位", "故障类型", "故障诊断", "诊断结果"]
                weights: [1, 5, 2, 2, 2]
                rows: root.detailRows || []
                cellText: function(row, col) {
                    if (col === 0) return row.index || ""
                    if (col === 1) return row.pointName || ""
                    if (col === 2) return row.faultType || ""
                    if (col === 3) return row.faultDiagnosis || ""
                    return row.diagnosisResult || ""
                }
                cellColor: function(row, col) {
                    return col === 4 ? root.resultColor(row.diagnosisResult) : "#111111"
                }
                clickableColumn: -1
            }
        }
    }

    component TableViewBlock: Item {
        id: tableRoot
        property var headers: []
        property var weights: []
        property var rows: []
        property int clickableColumn: -1
        property var cellText: function(row, col) { return "" }
        property var cellColor: function(row, col) { return "#111111" }
        signal cellClicked(int rowIndex, int col, var row)

        readonly property int headerH: 48
        readonly property int rowH: 54
        readonly property int gap: 1
        readonly property int totalWeight: {
            var s = 0
            for (var i = 0; i < weights.length; ++i) s += weights[i]
            return s <= 0 ? headers.length : s
        }

        function colWidth(col) {
            var usable = width - gap * (headers.length - 1)
            return Math.floor(usable * (weights[col] || 1) / totalWeight)
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: "#3a3a3a"
            border.width: 1
            radius: 6

            Column {
                anchors.fill: parent
                spacing: 0

                Row {
                    height: tableRoot.headerH
                    spacing: tableRoot.gap
                    Repeater {
                        model: tableRoot.headers.length
                        delegate: HeaderCell {
                            w: index === tableRoot.headers.length - 1
                               ? tableRoot.width - tableRoot.gap * (tableRoot.headers.length - 1) - x
                               : tableRoot.colWidth(index)
                            text: tableRoot.headers[index]
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: "#3a3a3a" }

                Item {
                    width: parent.width
                    height: parent.height - tableRoot.headerH - 1

                    ListView {
                        anchors.fill: parent
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: tableRoot.rows || []

                        delegate: Row {
                            id: rowItem
                            property int rowIndex: index
                            property var rowData: modelData
                            width: ListView.view.width
                            height: tableRoot.rowH
                            spacing: tableRoot.gap

                            Repeater {
                                model: tableRoot.headers.length
                                delegate: BodyCell {
                                    w: index === tableRoot.headers.length - 1
                                       ? parent.width - tableRoot.gap * (tableRoot.headers.length - 1) - x
                                       : tableRoot.colWidth(index)
                                    text: tableRoot.cellText(rowItem.rowData, index)
                                    textColor: tableRoot.cellColor(rowItem.rowData, index)
                                    clickable: index === tableRoot.clickableColumn
                                    onClicked: tableRoot.cellClicked(rowItem.rowIndex, index, rowItem.rowData)
                                }
                            }
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
        height: 48
        color: "#f0f0f0"
        border.color: "#3a3a3a"
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: parent.text
            font.pixelSize: 15
            font.bold: true
            color: "#111111"
        }
    }

    component BodyCell: Rectangle {
        id: cell
        property int w: 100
        property string text: ""
        property color textColor: "#111111"
        property bool clickable: false
        signal clicked()
        width: w
        height: 54
        color: mouse.pressed ? "#e8f0ff" : (mouse.containsMouse && clickable ? "#f6f8ff" : "#ffffff")
        border.color: "#3a3a3a"
        border.width: 1

        Text {
            anchors.centerIn: parent
            width: parent.width - 14
            text: cell.text
            color: cell.textColor
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: cell.clickable
            cursorShape: cell.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: cell.clicked()
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    function sections() {
        return AppConfig.sections()
    }

    function faultClickable(v) {
        if (v === undefined || v === null)
            return true
        if (v === true)
            return true
        if (v === false)
            return false
        var s = String(v).toLowerCase().trim()
        return s === "true" || s === "1" || s === "yes"
    }

    function stateId(fault) {
        if (!fault)
            return ""
        return String(fault.channel || "").toUpperCase() + ":"
                + String(fault.index || 0) + ":"
                + String(fault.faultType || "").toLowerCase()
    }

    function faultTypeName(t) {
        if (t === "short") return "短路"
        if (t === "open") return "断路"
        if (t === "loose") return "虚接"
        return t || ""
    }

    function pointIdentity(fault) {
        if (fault && fault.channel !== undefined && fault.index !== undefined)
            return String(fault.channel).toUpperCase() + ":" + String(fault.index)

        var t = String((fault && fault.text) || "")
        var idx = t.lastIndexOf("-")
        return idx > 0 ? t.substring(0, idx) : t
    }

    function sendFaultCommand(fault, on) {
        if (typeof Backend === "undefined")
            return
        Backend.sendFaultFrame(fault, on)
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.implicitHeight + 60
        clip: true

        Column {
            id: contentColumn
            width: parent.width
            spacing: 18
            padding: 16

            Repeater {
                model: root.sections()

                delegate: Column {
                    width: contentColumn.width - contentColumn.padding * 2
                    spacing: 10

                    Text {
                        text: modelData.title
                        font.pixelSize: 16
                        font.bold: true
                        color: "#111111"
                    }

                    Rectangle {
                        width: parent.width
                        height: grid.implicitHeight + 24
                        radius: 4
                        color: "transparent"
                        border.color: "#9b9b9b"
                        border.width: 1

                        GridLayout {
                            id: grid
                            anchors.fill: parent
                            anchors.margins: 12
                            columns: 3
                            rowSpacing: 12
                            columnSpacing: 8

                            Repeater {
                                model: modelData.items

                                delegate: SwitchTile {
                                    text: modelData.text
                                    stateId: root.stateId(modelData)
                                    enabled: root.faultClickable(modelData.clickable)
                                    value: typeof Backend !== "undefined"
                                           && Backend.states
                                           && Backend.states[root.stateId(modelData)] === true

                                    onToggleRequested: function(k, newValue) {
                                        if (!enabled || typeof Backend === "undefined")
                                            return

                                        var cur = modelData
                                        var allFaultItems = AppConfig.allFaults()
                                        var batch = ({})

                                        // 同一故障点的“短路/断路/虚接”三选一，避免一个点位同时打开多种故障。
                                        var curPoint = root.pointIdentity(cur)
                                        for (var i = 0; i < allFaultItems.length; ++i) {
                                            var f = allFaultItems[i]
                                            if (root.pointIdentity(f) === curPoint) {
                                                batch[root.stateId(f)] = false
                                            }
                                        }
                                        batch[root.stateId(cur)] = newValue

                                        Backend.applyStates(batch)
                                        Backend.appLog((newValue ? "[操作] 设置了【" : "[操作] 取消了【")
                                                       + cur.text
                                                       + "】"
                                                       + (cur.faultType ? "（" + root.faultTypeName(cur.faultType) + "）" : ""))
                                        root.sendFaultCommand(cur, newValue)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

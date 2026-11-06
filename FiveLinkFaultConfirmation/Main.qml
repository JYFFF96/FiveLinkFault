import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.settings 1.1
import "."

ApplicationWindow {
    id: win
    width: 1280
    height: 720
    visible: true
    title: AppConfig.appTitle

    readonly property bool serialOn: (typeof Serial !== "undefined")
                                     && ((Serial.opened !== undefined && Serial.opened)
                                         || (Serial.isOpened !== undefined && Serial.isOpened))
    readonly property bool wifiOn: typeof Wifi !== "undefined" && Wifi.listening
    readonly property bool connected: serialOn || wifiOn
    property bool _didSerialReset: false
    property bool selectionLocked: false
    // 0: 主界面，1: 操作日志界面
    property int topPageIndex: 0

    Settings {
        id: nameDB
        category: "wifi.names"
        property var map: ({})
    }

    function peerToIp(peer) {
        var s = peer || ""
        if (s.startsWith("::ffff:"))
            s = s.substring(7)
        var i = s.indexOf(":")
        return i >= 0 ? s.substring(0, i) : s
    }

    function ipToName(ip) {
        if (!ip) return ""
        var m = nameDB.map || {}
        return m[ip] && m[ip].length ? m[ip] : ip
    }

    function setName(ip, newName) {
        if (!ip) return
        var m = nameDB.map || {}
        m[ip] = newName
        nameDB.map = m
        if (typeof Backend !== "undefined" && Backend.setDeviceNameMap)
            Backend.setDeviceNameMap(m)
    }

    property var _pendingFrames: []
    property string _pendingTag: ""
    property string _pendingTarget: ""

    Timer {
        id: frameSendTimer
        interval: 80
        repeat: false
        onTriggered: win._sendNextQueuedFrame()
    }

    function _toArray(v) {
        var arr = []
        if (v === undefined || v === null) return arr
        for (var i = 0; i < v.length; ++i) {
            if (String(v[i]).length > 0) arr.push(String(v[i]))
        }
        return arr
    }

    function _sendOneFrame(tag, frame) {
        if (win.wifiOn && typeof Wifi !== "undefined") {
            const idx = Wifi.activeClient !== undefined ? Wifi.activeClient : -1
            const sent = Wifi.sendTextTo(idx, frame)
            Backend.appLog("[" + tag + "] Wi-Fi 发送 " + (sent > 0 ? "OK" : "FAIL")
                           + "，目标=" + (idx < 0 ? "全部" : ("index " + idx))
                           + "，帧=" + frame)
        } else if (win.serialOn && typeof Serial !== "undefined") {
            Serial.sendText(frame)
            Backend.appLog("[" + tag + "] 串口发送 OK，帧=" + frame)
        } else if (typeof Backend !== "undefined" && Backend.sendRaw) {
            Backend.sendRaw(frame)
            Backend.appLog("[" + tag + "] Backend.sendRaw()，帧=" + frame)
        }
    }

    function _sendNextQueuedFrame() {
        if (!_pendingFrames || _pendingFrames.length <= 0) return
        var frame = _pendingFrames.shift()
        _sendOneFrame(_pendingTag, frame)
        if (_pendingFrames.length > 0) frameSendTimer.restart()
    }

    function sendFramesSeparated(tag, frames) {
        var arr = _toArray(frames)
        if (arr.length <= 0) {
            Backend.appLog("[" + tag + "] 无可发送指令")
            return
        }
        _pendingFrames = arr
        _pendingTag = tag
        frameSendTimer.stop()
        _sendNextQueuedFrame()
    }

    Component.onCompleted: {
        if (typeof Backend !== "undefined" && Backend.setDeviceNameMap)
            Backend.setDeviceNameMap(nameDB.map || {})
    }

    Connections {
        target: nameDB
        function onMapChanged() {
            if (typeof Backend !== "undefined" && Backend.setDeviceNameMap)
                Backend.setDeviceNameMap(nameDB.map || {})
        }
    }

    Action {
        id: resetAction
        text: "复位"
        onTriggered: {
            if (typeof Backend !== "undefined") {
                Backend.resetAllStates()
                win.selectionLocked = false
                Backend.appLog("[复位] 已取消所有已勾选的故障项，未发送任何指令")
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: win.topPageIndex

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                    ToolBar {
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            spacing: 8

                            ToolButton {
                                text: "连接"
                                enabled: !win.connected
                                ToolTip.visible: hovered
                                ToolTip.text: "连接设备（串口/Wi-Fi）"
                                onClicked: {
                                    _didSerialReset = false
                                    connectDialog.open()
                                }
                            }

                            ToolButton {
                                text: "断开"
                                enabled: win.connected
                                ToolTip.visible: hovered
                                ToolTip.text: "断开当前连接"
                                onClicked: {
                                    if (typeof Serial !== "undefined" && ((Serial.opened !== undefined && Serial.opened) || (Serial.isOpened !== undefined && Serial.isOpened))) {
                                        var portName = Serial.currentPort !== undefined ? Serial.currentPort : "串口设备"
                                        Backend.appLog("[连接] " + portName + " 设备断开")
                                        Serial.close()
                                    }
                                    if (typeof Wifi !== "undefined" && Wifi.listening) {
                                        Backend.appLog("[连接] Wi-Fi 设备断开，停止监听 " + Wifi.bindAddress)
                                        Wifi.stopListening()
                                    }
                                    _didSerialReset = false
                                }
                            }

                            ToolButton {
                                action: resetAction
                                ToolTip.visible: hovered
                                ToolTip.text: "取消所有已勾选的故障项"
                            }

                            ToolButton {
                                text: "提交"
                                enabled: win.connected && !win.selectionLocked
                                ToolTip.visible: hovered
                                ToolTip.text: "提交当前勾选，等待设备返回判断结果"
                                onClicked: submitConfirmDlg.open()
                            }

                            ToolButton {
                                text: "日志"
                                ToolTip.visible: hovered
                                ToolTip.text: "进入操作日志界面"
                                onClicked: win.topPageIndex = 1
                            }

                            RowLayout {
                                id: boardPickerRow
                                spacing: 6
                                visible: typeof Wifi !== "undefined" && Wifi.listening && Wifi.clientList && Wifi.clientList.length > 0
                                onVisibleChanged: {
                                    if (!visible && typeof Wifi !== "undefined")
                                        Wifi.activeClient = -1
                                }
                                Label { text: "选择故障板：" }
                                ComboBox {
                                    id: boardCombo
                                    Layout.preferredWidth: 240
                                    function _names() {
                                        if (typeof Wifi === "undefined" || !Wifi.clientList) return []
                                        var arr = []
                                        for (var i = 0; i < Wifi.clientList.length; ++i) {
                                            var ip = peerToIp(Wifi.clientList[i])
                                            arr.push(ipToName(ip))
                                        }
                                        return arr
                                    }
                                    model: ["全部"].concat(_names())
                                    currentIndex: typeof Wifi !== "undefined" && Wifi.activeClient >= 0 ? Wifi.activeClient + 1 : 0
                                    onActivated: function(i) {
                                        if (typeof Wifi === "undefined") return
                                        Wifi.activeClient = i === 0 ? -1 : i - 1
                                        Backend.appLog("[选择故障板] activeClient = " + Wifi.activeClient)
                                        if (Wifi.activeClient >= 0)
                                            Backend.appLog("[WiFi] 切换板卡")
                                    }
                                    Connections {
                                        target: Wifi
                                        function onClientsChanged() {
                                            boardCombo.model = ["全部"].concat(boardCombo._names())
                                            boardCombo.currentIndex = Wifi.activeClient >= 0 ? Wifi.activeClient + 1 : 0
                                        }
                                        function onActiveClientChanged() {
                                            boardCombo.currentIndex = Wifi.activeClient >= 0 ? Wifi.activeClient + 1 : 0
                                        }
                                    }
                                    Connections {
                                        target: nameDB
                                        function onMapChanged() {
                                            boardCombo.model = ["全部"].concat(boardCombo._names())
                                            boardCombo.currentIndex = typeof Wifi !== "undefined" && Wifi.activeClient >= 0 ? Wifi.activeClient + 1 : 0
                                        }
                                    }
                                }
                                Button { text: "重命名…"; enabled: typeof Wifi !== "undefined" && Wifi.activeClient >= 0; onClicked: renameDlg.open() }
                                Dialog {
                                    id: renameDlg
                                    title: "重命名故障板"
                                    modal: true
                                    standardButtons: Dialog.Ok | Dialog.Cancel
                                    property string editingIp: ""
                                    contentItem: ColumnLayout {
                                        anchors.margins: 16
                                        TextField { id: nameEdit; Layout.preferredWidth: 260; placeholderText: "请输入名称"; selectByMouse: true }
                                    }
                                    onOpened: {
                                        if (typeof Wifi === "undefined" || Wifi.activeClient < 0) { nameEdit.text = ""; editingIp = ""; return }
                                        var ip = peerToIp(Wifi.clientList[Wifi.activeClient])
                                        editingIp = ip
                                        nameEdit.text = ipToName(ip)
                                    }
                                    onAccepted: {
                                        var newName = (nameEdit.text || "").trim()
                                        if (!editingIp || !newName.length) return
                                        setName(editingIp, newName)
                                        Backend.appLog("[命名] " + editingIp + " -> " + newName)
                                        if (typeof Wifi !== "undefined" && Wifi.activeClient >= 0) {
                                            var frame = "#SET NAME:" + newName + "%"
                                            var n = Wifi.sendTextTo(Wifi.activeClient, frame)
                                            Backend.appLog("[WiFi] 发送重命名命令: \"" + frame + "\" -> " + (n > 0 ? "OK" : "FAIL"))
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: typeof Backend !== "undefined" ? Backend.pageIndex : 0

                        FaultPage {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            locked: win.selectionLocked
                        }

                        ResultPage {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                    }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: win.connected ? 28 : 0
                    Layout.minimumHeight: win.connected ? 28 : 0
                    Layout.maximumHeight: win.connected ? 28 : 0
                    color: "transparent"
                    border.color: win.connected ? "#d0d0d0" : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        Item { Layout.fillWidth: true }
                        Label {
                            visible: win.connected
                            text: typeof Backend !== "undefined" && Backend.statusText ? Backend.statusText : "设备已连接"
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
                    }
                }
            }
        }

        LogPage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            onBackRequested: win.topPageIndex = 0
        }
    }

    Dialog {
        id: submitConfirmDlg
        modal: true
        title: "提示"
        standardButtons: Dialog.NoButton
        width: Math.min(720, win.width * 0.80)
        height: 240
        x: (win.width - width) / 2
        y: (win.height - height) / 2

        contentItem: Item {
            implicitWidth: 660
            implicitHeight: 150

            Label {
                id: submitConfirmText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                anchors.topMargin: 8
                text: "请核实所报故障点位详情，提交后将锁定当前勾选并等待设备返回诊断结果。"
                wrapMode: Text.WordWrap
                font.pixelSize: 16
                color: "#222222"
            }

            RowLayout {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 22
                anchors.bottomMargin: 4
                spacing: 12

                Button {
                    text: "返回"
                    onClicked: submitConfirmDlg.close()
                }

                Button {
                    text: "确认提交"
                    onClicked: {
                        submitConfirmDlg.close()
                        if (typeof Backend !== "undefined") Backend.commitSelection()
                        win.selectionLocked = true
                        sendFramesSeparated("READ", AppConfig.readFrames)
                        if (typeof Backend !== "undefined") Backend.appLog("[提交] 已发送 READ 指令，等待设备返回诊断结果")
                    }
                }
            }
        }
    }

    Dialog {
        id: connectDialog

        modal: true
        title: "连接设备"
        standardButtons: Dialog.NoButton

        width: 560
        height: 460

        x: (win.width - width) / 2
        y: (win.height - height) / 2

        property int mode: 0
        property string _err: ""

        Timer {
            id: checkTimer

            interval: 400
            repeat: false

            onTriggered: {
                if (connectDialog.mode === 0) {
                    if (!win.serialOn) {
                        connectDialog._err = "串口连接失败，请检查串口号和波特率。"
                        errDlg.open()
                    }
                } else {
                    if (!win.wifiOn) {
                        connectDialog._err = "Wi-Fi 监听失败，请检查端口或绑定地址。"
                        errDlg.open()
                    }
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 14

            RowLayout {
                spacing: 8

                Label {
                    text: "连接方式："
                }

                ComboBox {
                    id: cbMode

                    model: Qt.platform.os === "android" ? ["Wi-Fi"] : ["串口", "Wi-Fi"]
                    Layout.preferredWidth: 180

                    currentIndex: Qt.platform.os === "android" ? 0 : connectDialog.mode

                    onActivated: function(i) {
                        if (Qt.platform.os === "android") {
                            connectDialog.mode = 1
                            currentIndex = 0
                        } else {
                            connectDialog.mode = i
                        }
                    }

                    Component.onCompleted: {
                        if (Qt.platform.os === "android") {
                            connectDialog.mode = 1
                            currentIndex = 0
                        }
                    }

                    onModelChanged: {
                        if (Qt.platform.os === "android") {
                            currentIndex = 0
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            StackLayout {
                Layout.fillWidth: true
                currentIndex: connectDialog.mode

                Item {
                    Layout.fillWidth: true

                    GridLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right

                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 10

                        Label {
                            text: "串口号："
                        }

                        ComboBox {
                            id: portCombo

                            Layout.fillWidth: true
                            model: typeof Serial !== "undefined" ? Serial.portNames : []

                            Component.onCompleted: {
                                if (typeof Serial !== "undefined")
                                    Serial.refresh()
                            }

                            onPressedChanged: {
                                if (pressed && typeof Serial !== "undefined")
                                    Serial.refresh()
                            }
                        }

                        Label {
                            text: "波特率："
                        }

                        ComboBox {
                            id: baudCombo

                            Layout.fillWidth: true
                            model: ["9600", "19200", "38400", "57600", "115200", "230400"]
                            currentIndex: 0
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true

                    GridLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right

                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 10

                        Label {
                            text: "监听端口："
                        }

                        TextField {
                            id: portEdit

                            Layout.fillWidth: true
                            text: "8088"

                            validator: IntValidator {
                                bottom: 1
                                top: 65535
                            }
                        }

                        Label {
                            text: "绑定地址："
                        }

                        ComboBox {
                            id: bindCombo

                            Layout.fillWidth: true

                            function refreshModel() {
                                if (typeof Wifi === "undefined")
                                    return ["所有地址 (0.0.0.0)"]

                                if (Wifi.refreshHostIPs)
                                    Wifi.refreshHostIPs()

                                return ["所有地址 (0.0.0.0)"].concat(Wifi.hostIPv4List || [])
                            }

                            model: refreshModel()

                            Component.onCompleted: currentIndex = 0

                            onPressedChanged: {
                                if (pressed) {
                                    model = refreshModel()

                                    if (currentIndex < 0)
                                        currentIndex = 0
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "取消"
                    onClicked: connectDialog.close()
                }

                Button {
                    text: "连接"

                    onClicked: {
                        if (connectDialog.mode === 0) {
                            if (typeof Serial === "undefined")
                                return

                            var serialOk = Serial.open(portCombo.currentText, parseInt(baudCombo.currentText))
                            Backend.appLog("[连接] "
                                           + portCombo.currentText
                                           + " 设备"
                                           + (serialOk ? "连接成功" : "连接失败")
                                           + "，波特率="
                                           + baudCombo.currentText)
                            connectDialog.close()

                        } else {
                            if (typeof Wifi === "undefined")
                                return

                            if (Wifi.refreshHostIPs) {
                                Wifi.refreshHostIPs()
                                bindCombo.model = bindCombo.refreshModel()

                                if (bindCombo.currentIndex < 0)
                                    bindCombo.currentIndex = 0
                            }

                            Wifi.bindAddress = bindCombo.currentIndex === 0
                                               ? "0.0.0.0"
                                               : bindCombo.currentText

                            var wifiOk = Wifi.startListening(parseInt(portEdit.text))
                            Backend.appLog("[连接] Wi-Fi设备"
                                           + (wifiOk ? "连接成功" : "连接失败")
                                           + "，监听="
                                           + Wifi.bindAddress
                                           + ":"
                                           + portEdit.text)
                            connectDialog.close()
                        }

                        checkTimer.start()
                    }
                }
            }
        }
    }

    Dialog {
        id: errDlg

        modal: true
        title: "连接失败"
        standardButtons: Dialog.Ok
        x: (Overlay.overlay.width - width) / 2
        y: (Overlay.overlay.height - height) / 2
        contentItem: Label {
            text: connectDialog._err.length ? connectDialog._err : "连接未建立。"
            wrapMode: Text.WordWrap
            padding: 16
            width: 420
        }
    }

    Connections {
        target: Serial

        function onLog(s) {
            Backend.appLog(s)
        }
    }

    Connections {
        target: Wifi

        function onLog(s) {
            Backend.appLog(s)
        }

        function onNewClientConnected(peer) {
            var ip = peerToIp(peer)

            Backend.appLog("[WiFi] 新客户端接入: "
                           + peer
                           + " (ip="
                           + ip
                           + ")")
        }
    }

    Connections {
        target: Hub

        function onLog(s) {
            Backend.appLog(s)
        }
    }

    Connections {
        target: Backend

        function onLastBoardNameChanged() {
            if (typeof Wifi === "undefined")
                return

            if (Wifi.activeClient === undefined || Wifi.activeClient < 0)
                return

            if (!Wifi.clientList || Wifi.activeClient >= Wifi.clientList.length)
                return

            var peer = Wifi.clientList[Wifi.activeClient]
            var ip = peerToIp(peer)

            var rawName = Backend.lastBoardName || ""
            rawName = rawName.trim()

            var m = nameDB.map || {}

            if (!rawName.length || rawName.toUpperCase() === "NULL") {
                if (m.hasOwnProperty(ip)) {
                    delete m[ip]
                    nameDB.map = m
                }

                Backend.appLog("[命名] 板卡上报 NULL/空名称，使用 IP 显示: " + ip)
            } else {
                m[ip] = rawName
                nameDB.map = m

                Backend.appLog("[命名] 从板卡自报获取名称 "
                               + ip
                               + " -> "
                               + rawName)
            }

            if (boardCombo) {
                boardCombo.model = ["全部"].concat(boardCombo._names())
                boardCombo.currentIndex = Wifi.activeClient >= 0
                                          ? Wifi.activeClient + 1
                                          : 0
            }
        }
    }

}

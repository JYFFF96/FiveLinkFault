#include "wifimanager.h"
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>

static QString ts()
{
    return QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss.zzz");
}

WifiManager::WifiManager(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    refreshHostIPs();
    connect(m_server, &QTcpServer::newConnection,
            this, &WifiManager::onNewConnection);
}

WifiManager::~WifiManager()
{
    // 停止监听
    if (m_server->isListening()) {
        m_server->close();
    }

    // 断开所有客户端连接
    for (QTcpSocket* sock : m_clients) {
        if (sock) {
            sock->disconnectFromHost();
            sock->close();
            sock->deleteLater();
        }
    }
    m_clients.clear();

    // 删除心跳定时器
    for (HeartbeatInfo &hb : m_heartbeats) {
        if (hb.timer30) hb.timer30->deleteLater();
        if (hb.timer3)  hb.timer3->deleteLater();
    }
    m_heartbeats.clear();

    qDebug() << "[WiFi] WifiManager destroyed, server closed, clients cleared.";
}

// ★ 现在是 Q_INVOKABLE，可被 QML 调用
void WifiManager::refreshHostIPs() {
    QStringList list;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;

        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            const QHostAddress ip = e.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol)
                list << ip.toString();
        }
    }
    list.removeDuplicates();
    list.sort();
    if (list.isEmpty())
        list << QStringLiteral("127.0.0.1");

    if (m_hostIPv4List != list) {
        m_hostIPv4List = list;
        emit hostListChanged();
    }
}

void WifiManager::setBindAddress(const QString &addr) {
    if (m_bindAddress == addr) return;
    m_bindAddress = addr;
    emit bindAddressChanged();
}

bool WifiManager::startListening(quint16 port) {
    if (m_server->isListening())
        m_server->close();

    const QHostAddress bind =
        (m_bindAddress == QLatin1String("0.0.0.0") || m_bindAddress.isEmpty())
            ? QHostAddress::AnyIPv4
            : QHostAddress(m_bindAddress);

    const bool ok = m_server->listen(bind, port);
    emit listeningChanged();
    emit log(QString("[%1][WiFi] startListening %2:%3 -> %4")
                 .arg(ts()).arg(bind.toString()).arg(port)
                 .arg(ok ? "OK" : "FAIL"));
    return ok;
}

void WifiManager::stopListening() {
    if (m_server->isListening()) {
        emit log(QString("[%1][WiFi] stopListening").arg(ts()));
        m_server->close();
    }
    emit listeningChanged();
}

void WifiManager::onNewConnection() {
    if (!m_server) return;
    while (auto *sock = m_server->nextPendingConnection()) {
        m_clients << sock;
        connect(sock, &QTcpSocket::readyRead,    this, &WifiManager::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &WifiManager::onClientDisconnected);

        const QString peer = peerName(sock);
        emit log(QString("[%1][WiFi] client connected: %2").arg(ts()).arg(peer));
        emit newClientConnected(peer);

        // 为这个客户端启动心跳监测
        startHeartbeat(sock);
    }
    emit clientsChanged();

    // 第一次有客户端时，如当前为群发模式(-1)，可选地锁定第一个
    if (m_activeIndex == -1 && !m_clients.isEmpty()) {
        m_activeIndex = 0;
        emit activeClientChanged();
        emit log(QString("[%1][WiFi] activeClient -> %2").arg(ts()).arg(m_activeIndex));
    }
}

void WifiManager::onClientDisconnected() {
    auto *sock = qobject_cast<QTcpSocket*>(sender());
    const QString peer = peerName(sock);

    // 停止并清理这个客户端的心跳
    stopHeartbeat(sock);

    int idx = m_clients.indexOf(sock);
    if (idx >= 0) {
        m_clients.removeAt(idx);
        sock->deleteLater();
        emit log(QString("[%1][WiFi] client disconnected: %2 (idx=%3)")
                     .arg(ts()).arg(peer).arg(idx));
        emit clientsChanged();

        // 调整 activeIndex
        if (m_clients.isEmpty()) {
            if (m_activeIndex != -1) { m_activeIndex = -1; emit activeClientChanged(); }
        } else if (m_activeIndex == idx) {
            m_activeIndex = qMin(m_activeIndex, m_clients.size()-1);
            emit activeClientChanged();
        } else if (m_activeIndex > idx) {
            m_activeIndex -= 1;
            emit activeClientChanged();
        }
    }
}

void WifiManager::onReadyRead() {
    if (QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender())) {
        const QByteArray data = sock->readAll();
        const QString ip = sock->peerAddress().toString();

        // ===== 心跳应答检测 =====
        if (data.contains("HEART")) {
            handleHeartbeatReply(sock);
            emit log(QString("[%1][WiFi] HEART reply from %2: \"%3\"")
                         .arg(ts()).arg(peerName(sock),
                              QString::fromUtf8(data).trimmed()));

            // 如果完全就是心跳包，不再往业务层透传
            if (QString::fromUtf8(data).trimmed() == QStringLiteral("HEART")) {
                return;
            }
        }

        // ===== 正常业务数据 =====
        emit log(QString("[%1][WiFi] recv %2 bytes from %3: \"%4\"")
                     .arg(ts()).arg(data.size()).arg(peerName(sock),
                          QString::fromUtf8(data)));
        emit dataReceived(data);
        emit frameDecoded(data); // 如需解帧，可在此解析后再发
        emit frameDecodedWithIp(ip, data);
    }
}

QString WifiManager::ipForIndex(int index) const
{
    if (index < 0 || index >= m_clients.size()) return QString();
    QTcpSocket *s = m_clients.at(index);
    if (!s) return QString();
    return s->peerAddress().toString();
}

qint64 WifiManager::sendToAll(const QByteArray &data) {
    qint64 total = 0;
    for (QTcpSocket *s : m_clients) {
        if (!s || s->state() != QAbstractSocket::ConnectedState) continue;
        qint64 n = s->write(data);
        s->flush();   // 立即冲刷
        if (n > 0) total += n;
        emit log(QString("[%1][WiFi] send %2 bytes -> %3: \"%4\"")
                     .arg(ts()).arg(n).arg(peerName(s),
                          QString::fromUtf8(data)));
    }
    return total;
}

qint64 WifiManager::sendTo(int index, const QByteArray &data) {
    if (index < 0) return sendToAll(data); // -1 群发
    if (index >= 0 && index < m_clients.size()) {
        QTcpSocket *s = m_clients.at(index);
        if (s && s->state() == QAbstractSocket::ConnectedState) {
            qint64 n = s->write(data);
            s->flush();  // 立即冲刷
            emit log(QString("[%1][WiFi] send %2 bytes -> [%3]%4: \"%5\"")
                         .arg(ts()).arg(n).arg(index).arg(peerName(s),
                              QString::fromUtf8(data)));
            return n;
        }
    }
    emit log(QString("[%1][WiFi] send failed: invalid index=%2 or no client")
                 .arg(ts()).arg(index));
    return 0;
}

// 根据 activeClient 自动路由
qint64 WifiManager::sendText(const QString &s) {
    const QByteArray data = s.toUtf8();
    if (m_activeIndex >= 0)
        return sendTo(m_activeIndex, data);
    return sendToAll(data);
}

int WifiManager::indexForIp(const QString &ip) const
{
    for (int i = 0; i < m_clients.size(); ++i) {
        const QTcpSocket* s = m_clients.at(i);
        if (!s) continue;

        QString host = s->peerAddress().toString();
        // 兼容 IPv4-mapped IPv6 形如 "::ffff:192.168.1.12"
        if (host.startsWith("::ffff:"))
            host = host.mid(7);

        if (host == ip)
            return i;
    }
    return -1;
}

// 客户端列表
QStringList WifiManager::clientList() const {
    QStringList list;
    list.reserve(m_clients.size());
    for (QTcpSocket *s : m_clients) list << peerName(s);
    return list;
}

void WifiManager::setActiveClient(int idx) {
    int newIdx = idx;
    if (m_clients.isEmpty()) newIdx = -1;
    else if (idx >= m_clients.size()) newIdx = m_clients.size()-1;
    // 允许 -1(群发)
    if (m_activeIndex == newIdx) return;
    m_activeIndex = newIdx;
    emit activeClientChanged();
}

QString WifiManager::peerName(QTcpSocket *s) {
    if (!s) return QString();
    return QStringLiteral("%1:%2")
        .arg(s->peerAddress().toString())
        .arg(s->peerPort());
}

// =================== 心跳实现 ===================

void WifiManager::startHeartbeat(QTcpSocket *sock)
{
    if (!sock) return;
    if (m_heartbeats.contains(sock)) return;   // 防止重复

    HeartbeatInfo info;
    info.timer30 = new QTimer(this);
    info.timer3  = new QTimer(this);

    info.timer30->setInterval(30000);   // 30s
    info.timer30->setSingleShot(false);

    info.timer3->setInterval(3000);     // 3s
    info.timer3->setSingleShot(false);

    info.retryCnt = 0;
    info.awaiting = false;

    // 30s 周期心跳
    connect(info.timer30, &QTimer::timeout, this, [this, sock]() {
        heartbeat30Timeout(sock);
    });
    // 3s 重试心跳
    connect(info.timer3, &QTimer::timeout, this, [this, sock]() {
        heartbeatRetryTimeout(sock);
    });

    m_heartbeats.insert(sock, info);
    info.timer30->start();

    emit log(QString("[%1][WiFi] start heartbeat for %2")
                 .arg(ts()).arg(peerName(sock)));
}

void WifiManager::stopHeartbeat(QTcpSocket *sock)
{
    auto it = m_heartbeats.find(sock);
    if (it == m_heartbeats.end())
        return;

    HeartbeatInfo &info = it.value();
    if (info.timer30) {
        info.timer30->stop();
        info.timer30->deleteLater();
        info.timer30 = nullptr;
    }
    if (info.timer3) {
        info.timer3->stop();
        info.timer3->deleteLater();
        info.timer3 = nullptr;
    }

    m_heartbeats.erase(it);
    emit log(QString("[%1][WiFi] stop heartbeat for %2")
                 .arg(ts()).arg(peerName(sock)));
}

void WifiManager::sendHeartbeatPacket(QTcpSocket *sock)
{
    if (!sock || sock->state() != QAbstractSocket::ConnectedState)
        return;

    const QByteArray pkt("HEART");
    qint64 n = sock->write(pkt);
    sock->flush();
    emit log(QString("[%1][WiFi] send HEART -> %2 (%3 bytes)")
                 .arg(ts()).arg(peerName(sock)).arg(n));
}

// 30s 周期：如果当前不在等待 HEART 回复，就发一个 HEART 并开始 3s 重试计时
void WifiManager::heartbeat30Timeout(QTcpSocket *sock)
{
    if (!sock || sock->state() != QAbstractSocket::ConnectedState) {
        stopHeartbeat(sock);
        return;
    }

    auto it = m_heartbeats.find(sock);
    if (it == m_heartbeats.end()) return;
    HeartbeatInfo &info = it.value();

    if (info.awaiting) {
        // 还在等待上一次 HEART 的回复，这时候 3s 重试逻辑会负责，不再重复发
        return;
    }

    info.awaiting = true;
    info.retryCnt = 0;
    sendHeartbeatPacket(sock);

    if (info.timer3)
        info.timer3->start();
}

// 3s 重试：最多重试 3 次，还是没收到 HEART 回复就断开这条 socket
void WifiManager::heartbeatRetryTimeout(QTcpSocket *sock)
{
    if (!sock || sock->state() != QAbstractSocket::ConnectedState) {
        stopHeartbeat(sock);
        return;
    }

    auto it = m_heartbeats.find(sock);
    if (it == m_heartbeats.end()) return;
    HeartbeatInfo &info = it.value();

    if (!info.awaiting) {
        // 已经收到 HEART 回复，不需要再重试
        if (info.timer3 && info.timer3->isActive())
            info.timer3->stop();
        return;
    }

    if (info.retryCnt >= 3) {
        emit log(QString("[%1][WiFi] HEART timeout, close client %2")
                     .arg(ts()).arg(peerName(sock)));
        // 只断开这一条连接
        sock->disconnectFromHost();
        if (sock->state() != QAbstractSocket::UnconnectedState)
            sock->abort();
        // onClientDisconnected() 里会调用 stopHeartbeat()
        return;
    }

    info.retryCnt += 1;
    sendHeartbeatPacket(sock);
}

// 收到 HEART 回复
void WifiManager::handleHeartbeatReply(QTcpSocket *sock)
{
    auto it = m_heartbeats.find(sock);
    if (it == m_heartbeats.end()) return;
    HeartbeatInfo &info = it.value();

    info.awaiting = false;
    info.retryCnt = 0;
    if (info.timer3 && info.timer3->isActive())
        info.timer3->stop();

    emit log(QString("[%1][WiFi] HEART ok for %2")
                 .arg(ts()).arg(peerName(sock)));
}

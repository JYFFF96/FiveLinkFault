#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QList>
#include <QHash>
#include <QTimer>

class WifiManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList hostIPv4List READ hostIPv4List NOTIFY hostListChanged)
    Q_PROPERTY(QString     bindAddress  READ bindAddress  WRITE setBindAddress NOTIFY bindAddressChanged)
    Q_PROPERTY(bool        listening    READ listening    NOTIFY listeningChanged)
    Q_PROPERTY(bool        hasClient    READ hasClient    NOTIFY clientsChanged)

    // 客户端选择（下拉框）
    Q_PROPERTY(QStringList clientList   READ clientList   NOTIFY clientsChanged)
    Q_PROPERTY(int         activeClient READ activeClient WRITE setActiveClient NOTIFY activeClientChanged)

public:
    explicit WifiManager(QObject *parent = nullptr);
    ~WifiManager();

    // 基础属性
    QStringList hostIPv4List() const { return m_hostIPv4List; }
    QString     bindAddress()  const { return m_bindAddress; }
    void        setBindAddress(const QString &addr);

    bool listening() const { return m_server && m_server->isListening(); }

    // 业务接口
    Q_INVOKABLE bool startListening(quint16 port);
    Q_INVOKABLE void stopListening();

    // ★ 新增：暴露给 QML 调用的实时刷新 IP 接口
    Q_INVOKABLE void refreshHostIPs();

    // 客户端
    bool hasClient() const { return !m_clients.isEmpty(); }
    QStringList clientList() const;
    int  activeClient() const { return m_activeIndex; }
    void setActiveClient(int idx);

    // 发送
    Q_INVOKABLE qint64 sendToAll(const QByteArray &data);
    Q_INVOKABLE qint64 sendTo(int index, const QByteArray &data);

    // 根据 activeClient 自动路由（-1 群发，>=0 单发）
    Q_INVOKABLE qint64 sendText(const QString &s);
    Q_INVOKABLE qint64 sendTextTo(int index, const QString &s) { return sendTo(index, s.toUtf8()); }

    Q_INVOKABLE int indexForIp(const QString& ip) const;
    // ★ 新增：按 index 获取纯 IP（不带端口）
    Q_INVOKABLE QString ipForIndex(int index) const;

signals:
    void hostListChanged();
    void bindAddressChanged();
    void listeningChanged();
    void clientsChanged();
    void activeClientChanged();
    void newClientConnected(const QString &peer);

    void dataReceived(const QByteArray &data);
    void frameDecoded(const QByteArray &frame);
    // ★ 新增：携带来源 IP 的帧（不影响原有 frameDecoded）
    void frameDecodedWithIp(const QString &ip, const QByteArray &frame);

    // 日志（QML 里 Backend.appLog 已经在用）
    void log(const QString& line);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    static QString peerName(QTcpSocket *s);

    // ===== 心跳相关 =====
    struct HeartbeatInfo {
        QTimer *timer30  = nullptr;   // 30s 周期心跳
        QTimer *timer3   = nullptr;   // 3s 重试心跳
        int     retryCnt = 0;         // 已重试次数
        bool    awaiting = false;     // 正在等待本轮 HEART 回复
    };

    void startHeartbeat(QTcpSocket *sock);
    void stopHeartbeat(QTcpSocket *sock);
    void heartbeat30Timeout(QTcpSocket *sock);
    void heartbeatRetryTimeout(QTcpSocket *sock);
    void handleHeartbeatReply(QTcpSocket *sock);
    void sendHeartbeatPacket(QTcpSocket *sock);

    QTcpServer  *m_server = nullptr;
    QStringList  m_hostIPv4List;
    QString      m_bindAddress = QStringLiteral("0.0.0.0");
    QList<QTcpSocket*> m_clients;
    int m_activeIndex = -1; // -1=群发，其它=clientList 下标

    QHash<QTcpSocket*, HeartbeatInfo> m_heartbeats;
};

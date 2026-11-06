#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>

class Backend;
class SerialManager;
class WifiManager;

class TransportHub : public QObject
{
    Q_OBJECT
public:
    explicit TransportHub(Backend* backend,
                          SerialManager* serial,
                          WifiManager* wifi,
                          QObject* parent = nullptr);

    Q_INVOKABLE void sendRaw(const QString& frame);

public slots:
    void applyIncoming(const QByteArray& frame);
    void applyIncomingWithIp(const QString& ip, const QByteArray& frame);

signals:
    void log(const QString& line);

private:
    Backend*       m_backend { nullptr };
    SerialManager* m_serial  { nullptr };
    WifiManager*   m_wifi    { nullptr };
    QByteArray     m_rxBuffer;
    QHash<QString, QByteArray> m_wifiRxBuffers;
};

#include "transporthub.h"

#include "backend.h"
#include "serialmanager.h"
#include "wifimanager.h"

TransportHub::TransportHub(Backend* backend,
                           SerialManager* serial,
                           WifiManager* wifi,
                           QObject* parent)
    : QObject(parent)
    , m_backend(backend)
    , m_serial(serial)
    , m_wifi(wifi)
{
}

void TransportHub::sendRaw(const QString& frame)
{
    if (frame.isEmpty()) {
        return;
    }

    bool sent = false;

    if (m_serial && m_serial->isOpened()) {
        sent = m_serial->sendText(frame) > 0;
    } else if (m_wifi && m_wifi->listening()) {
        sent = m_wifi->sendText(frame) > 0;
    }

    emit log(QStringLiteral("[报文] 发送: %1").arg(frame));
    if (!sent) {
        emit log(QStringLiteral("[报文] 发送失败: 未连接，未发送"));
    }
}

void TransportHub::applyIncoming(const QByteArray& data)
{
    if (!m_backend) {
        return;
    }

    m_rxBuffer.append(data);

    while (true) {
        const int start = m_rxBuffer.indexOf('#');
        if (start < 0) {
            m_rxBuffer.clear();
            break;
        }

        if (start > 0) {
            m_rxBuffer.remove(0, start);
        }

        const int end = m_rxBuffer.indexOf('%', 1);
        if (end < 0) {
            break;
        }

        const QByteArray oneFrame = m_rxBuffer.left(end + 1);
        const QString text = QString::fromUtf8(oneFrame);
        if (text.trimmed().startsWith(QStringLiteral("DIAG"), Qt::CaseInsensitive)) {
            m_backend->applyDiagFrame(QString(), text);
        } else {
            m_backend->applyIreadFrame(text);
        }
        m_rxBuffer.remove(0, end + 1);
    }
}

void TransportHub::applyIncomingWithIp(const QString& ip, const QByteArray& data)
{
    if (!m_backend) {
        return;
    }

    QByteArray& buffer = m_wifiRxBuffers[ip];
    buffer.append(data);

    while (true) {
        const int start = buffer.indexOf('#');
        if (start < 0) {
            buffer.clear();
            break;
        }

        if (start > 0) {
            buffer.remove(0, start);
        }

        const int end = buffer.indexOf('%', 1);
        if (end < 0) {
            break;
        }

        const QByteArray oneFrame = buffer.left(end + 1);
        const QString text = QString::fromUtf8(oneFrame);
        if (text.trimmed().startsWith(QStringLiteral("DIAG"), Qt::CaseInsensitive)) {
            m_backend->applyDiagFrame(ip, text);
        } else {
            m_backend->applyIreadFrame(text);
        }
        buffer.remove(0, end + 1);
    }
}

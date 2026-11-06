#include "serialmanager.h"
#include <QDebug>

SerialManager::SerialManager(QObject *parent) : QObject(parent)
{
    connect(&m_port, &QSerialPort::readyRead,
            this, &SerialManager::onReadyRead);

    // 1s 轮询串口热插拔
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &SerialManager::pollPorts);
    m_poll.start();

    pollPorts(); // 启动先获取一次
}

void SerialManager::pollPorts()
{
    QStringList list;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const auto &info : infos) {
        if (!info.portName().isEmpty())
            list << info.portName();
    }
    list.removeDuplicates();
    list.sort();
    setPorts(list);
}

void SerialManager::setPorts(const QStringList &list)
{
    if (m_ports == list) return;
    m_ports = list;
    emit portsChanged();
    emit portNamesChanged();
}

void SerialManager::refresh()
{
    pollPorts();
}

bool SerialManager::open(const QString &name, int baud)
{
    if (m_port.isOpen())
        m_port.close();

    m_port.setPortName(name);
    m_port.setBaudRate(baud);
#ifdef Q_OS_WIN
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);
#endif
    const bool ok = m_port.open(QIODevice::ReadWrite);
    emit connectedChanged();
    if (ok) {
        m_opened = true;                 // 如果你有 opened
        emit openedChanged(m_opened);    // 对应 Q_PROPERTY opened
        emit isOpenedChanged(true);      // 如果你暴露的是 isOpened
    }
    return ok;
}

void SerialManager::close()
{
    if (m_port.isOpen())
        m_port.close();
    emit connectedChanged();
    m_opened = false;
    emit openedChanged(m_opened);
    emit isOpenedChanged(false);
}

qint64 SerialManager::send(const QByteArray &data)
{
    if (!m_port.isOpen()) return -1;
    const qint64 n = m_port.write(data);
    m_port.flush();
    return n;
}

void SerialManager::onReadyRead()
{
    const QByteArray d = m_port.readAll();
    emit dataReceived(d);
    emit frameDecoded(d);     // [NEW] 兼容：先把原始数据抛给旧逻辑
    // TODO: 需要解帧/粘包时，在这里解析后再 emit frameDecoded(decodedFrame);
}

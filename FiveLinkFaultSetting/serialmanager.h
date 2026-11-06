#pragma once
#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>

class SerialManager : public QObject {
    Q_OBJECT
    // 串口列表
    Q_PROPERTY(QStringList portNames READ portNames NOTIFY portsChanged)
    // 兼容：两套名字都暴露
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)     // [NEW]
    Q_PROPERTY(bool isOpened  READ isOpened  NOTIFY connectedChanged)     // [NEW]
    Q_PROPERTY(QString currentPort READ currentPort NOTIFY connectedChanged)
    Q_PROPERTY(int baudRate READ baudRate NOTIFY connectedChanged)
    // 你原来如果有 isOpened/opened 就保留；这里新增一个兼容 alias：connected
    Q_PROPERTY(bool connected READ isOpened NOTIFY connectedChanged)
    Q_PROPERTY(QStringList portNames READ portNames NOTIFY portNamesChanged)
public:
    explicit SerialManager(QObject *parent=nullptr);

    // 属性读取
    QStringList portNames() const { return m_ports; }
    bool connected() const { return m_port.isOpen(); }    // [NEW]
    bool isOpened()  const { return m_port.isOpen(); }     // [NEW]
    QString currentPort() const { return m_port.portName(); }
    int baudRate() const { return m_port.baudRate(); }

    // 接口
    Q_INVOKABLE void refresh();                             // 手动刷新串口列表
    Q_INVOKABLE bool open(const QString &name, int baud);
    Q_INVOKABLE void close();
    Q_INVOKABLE qint64 send(const QByteArray &data);
    Q_INVOKABLE qint64 sendText(const QString &s) { return send(s.toUtf8()); } // 便于 QML

signals:
    void portsChanged();
    void connectedChanged();
    void dataReceived(const QByteArray &data);
    void frameDecoded(const QByteArray &frame);            // [NEW] 兼容你现有连接
    void portNamesChanged();
    void openedChanged(bool ok);
    void isOpenedChanged(bool ok);

private slots:
    void pollPorts();    // 定时轮询热插拔
    void onReadyRead();

private:
    void setPorts(const QStringList &list);

    QSerialPort m_port;
    QStringList m_ports;
    QTimer      m_poll;
    bool m_opened = false;
};

#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class AppConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appTitle READ appTitle NOTIFY loaded)
    Q_PROPERTY(QString resetFrame READ resetFrame NOTIFY loaded)
    Q_PROPERTY(QVariantList resetFrames READ resetFrames NOTIFY loaded)
    Q_PROPERTY(QString readFrame READ readFrame NOTIFY loaded)
    Q_PROPERTY(QVariantList readFrames READ readFrames NOTIFY loaded)

public:
    explicit AppConfig(QObject *parent = nullptr);

    bool load(const QString &filePath);

    QString appTitle() const;
    QString resetFrame() const;
    QVariantList resetFrames() const;
    QString readFrame() const;
    QVariantList readFrames() const;

    Q_INVOKABLE QVariantList sections() const;
    Q_INVOKABLE QVariantList allFaults() const;

signals:
    void loaded();

private:
    QString m_appTitle;
    QString m_resetFrame;
    QVariantList m_resetFrames;
    QString m_readFrame;
    QVariantList m_readFrames;
    QVariantList m_sections;
    QVariantList m_allFaults;
};

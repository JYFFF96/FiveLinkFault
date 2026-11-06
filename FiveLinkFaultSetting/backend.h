#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QDateTime>
#include <QHash>
#include <QVector>

class TransportHub;

class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap states READ states NOTIFY statesChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QString lastBoardName READ lastBoardName NOTIFY lastBoardNameChanged)
    Q_PROPERTY(QVariantList diagResults READ diagResults NOTIFY diagResultsChanged)

public:
    explicit Backend(QObject* parent = nullptr);

    QVariantMap states() const { return m_states; }
    QString statusText() const { return m_statusText; }
    QString logText() const;
    QString lastBoardName() const { return m_lastBoardName; }
    QVariantList diagResults() const { return m_diagResults; }

    void setHub(TransportHub* hub) { m_hub = hub; }
    void setFaultDefinitions(const QVariantList& faults);

    Q_INVOKABLE bool setFaultSilent(const QString& stateId, bool on);
    Q_INVOKABLE void sendRaw(const QString& frame);
    Q_INVOKABLE bool sendFaultFrame(const QVariantMap& fault, bool on);
    Q_INVOKABLE void resetAllStates();
    Q_INVOKABLE void requestIRead();
    Q_INVOKABLE bool applyIreadFrame(const QString& frame);
    Q_INVOKABLE bool applyStatusFrame(const QString& frame);
    Q_INVOKABLE bool applyDiagFrame(const QString& ip, const QString& frame);
    Q_INVOKABLE QVariantList diagSystemResults(int index) const;
    Q_INVOKABLE QVariantList diagDetailResults(int index, const QString& systemName) const;
    Q_INVOKABLE void setDeviceNameMap(const QVariantMap& names);
    Q_INVOKABLE void appLog(const QString& line);
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void setLogMax(int n);

public slots:
    void applyStates(const QVariantMap& batch);

signals:
    void statesChanged();
    void statusTextChanged();
    void logTextChanged();
    void lastBoardNameChanged();
    void diagResultsChanged();

private:
    struct FaultMeta {
        QString stateId;
        QString text;
        QString point;
        QString systemName;
        QString channel;
        QString faultType;
        int index { 0 };
    };

    void loadAll();
    bool setFaultCommon(const QString& stateId, bool on);
    void markConfigChanged();
    void updateStatusText();
    void updateLastBoardNameFromFrame(const QString& frame);
    QString normalizeIp(const QString& ip) const;
    QString deviceNameForIp(const QString& ip) const;
    QString formatDuration(qint64 seconds) const;
    QString faultTypeName(const QString& faultType) const;
    int boardIdForIndex(const QString& channel, int index) const;
    int localIndexForIndex(const QString& channel, int index) const;
    int faultCodeForType(const QString& channel, const QString& faultType) const;
    bool statusCodeMatches(const QString& channel, const QString& faultType, int statusCode) const;
    bool parseStatusValues(const QString& text, int* id, QVector<int>* rValues, QVector<int>* cValues) const;
    QString diagKey(const QString& channel, int index) const;
    QHash<QString, int> parseDiagValueMap(const QString& text, bool selectedValues) const;
    QString buildFaultFrame(const QString& channel, int index, const QString& faultType, bool on) const;
    QString makeStateId(const QString& channel, int index, const QString& faultType) const;
    QString pointNameFromText(const QString& text) const;
    QVariantList buildSystemResults(const QVariantList& details) const;
    QVariantList buildDetailResults(const QHash<QString, int>& expected,
                                    const QHash<QString, int>& actual) const;
    bool splitDiagFrame(const QString& frame, QString* expectedText, QString* actualText) const;

    QVariantMap m_states;
    TransportHub* m_hub { nullptr };

    QString m_statusText;
    QString m_lastBoardName;

    int m_logMax { 500 };
    QStringList m_logBuffer;
    QVariantList m_faultDefinitions;
    QVector<FaultMeta> m_faultMetas;
    QDateTime m_lastConfigTime;
    QVariantMap m_deviceNameMap;
    QVariantList m_diagResults;
};

#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QDateTime>
#include <QHash>
#include <QVector>
#include <QSet>

class TransportHub;

class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap states READ states NOTIFY statesChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QString lastBoardName READ lastBoardName NOTIFY lastBoardNameChanged)
    Q_PROPERTY(QVariantList diagResults READ diagResults NOTIFY diagResultsChanged)
    Q_PROPERTY(QVariantList powerDiagRows READ powerDiagRows NOTIFY resultRowsChanged)
    Q_PROPERTY(QVariantList commDiagRows READ commDiagRows NOTIFY resultRowsChanged)
    Q_PROPERTY(int pageIndex READ pageIndex WRITE setPageIndex NOTIFY pageIndexChanged)
    Q_PROPERTY(QVariantList resultModel READ resultModel NOTIFY resultModelChanged)
    Q_PROPERTY(QVariantList detailResultModel READ detailResultModel NOTIFY detailResultModelChanged)
    Q_PROPERTY(QString resultDetailTitle READ resultDetailTitle NOTIFY resultDetailTitleChanged)
    Q_PROPERTY(bool detailMode READ detailMode NOTIFY detailModeChanged)

public:
    explicit Backend(QObject* parent = nullptr);

    QVariantMap states() const { return m_states; }
    QString statusText() const { return m_statusText; }
    QString logText() const;
    QString lastBoardName() const { return m_lastBoardName; }
    QVariantList diagResults() const { return m_diagResults; }
    QVariantList powerDiagRows() const { return m_powerDiagRows; }
    QVariantList commDiagRows() const { return m_commDiagRows; }
    int pageIndex() const { return m_pageIndex; }
    QVariantList resultModel() const { return m_resultModel; }
    QVariantList detailResultModel() const { return m_detailResultModel; }
    QString resultDetailTitle() const { return m_resultDetailTitle; }
    bool detailMode() const { return m_detailMode; }

    void setHub(TransportHub* hub) { m_hub = hub; }
    void setFaultDefinitions(const QVariantList& faults);

    Q_INVOKABLE bool setFaultSilent(const QString& stateId, bool on);
    Q_INVOKABLE void sendRaw(const QString& frame);
    Q_INVOKABLE bool sendFaultFrame(const QVariantMap& fault, bool on);
    Q_INVOKABLE void resetAllStates();
    Q_INVOKABLE bool applyIreadFrame(const QString& frame);
    Q_INVOKABLE bool applyStatusFrame(const QString& frame);
    Q_INVOKABLE bool applyDiagFrame(const QString& ip, const QString& frame);
    Q_INVOKABLE QString commitSelection();
    Q_INVOKABLE void setPageIndex(int idx);
    Q_INVOKABLE void openResultDetail(const QString& systemName);
    Q_INVOKABLE void closeResultDetail();
    Q_INVOKABLE void continueDiagnosis(bool clearSelections = true);
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
    void resultRowsChanged();
    void pageIndexChanged();
    void resultModelChanged();
    void detailResultModelChanged();
    void resultDetailTitleChanged();
    void detailModeChanged();

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
    QString buildFaultFrame(const QString& channel, int index, const QString& faultType, bool on) const;
    QString makeStateId(const QString& channel, int index, const QString& faultType) const;
    QString pointNameFromText(const QString& text) const;
    QVariantList buildSystemResults(const QVariantList& details) const;
    QVariantList buildDetailResults(const QHash<QString, int>& expected,
                                    const QHash<QString, int>& actual) const;
    bool splitDiagFrame(const QString& frame, QString* expectedText, QString* actualText) const;
    QString pointKey(const QString& channel, int index) const;
    QHash<QString, int> expectedStatusMapFromSelection() const;
    QHash<QString, int> statusMapFromFrame(const QString& frame, int* boardId = nullptr) const;
    QString buildExpectedFrame(const QHash<QString, int>& expected) const;
    QString buildForwardDiagFrame(const QString& actualFrameRaw) const;
    void forwardDiagResult(const QString& actualFrameRaw);
    QVariantList buildDetailResultsFromMaps(const QHash<QString, int>& expected, const QHash<QString, int>& actual) const;
    QVariantList buildChannelResultRows(const QString& channel) const;
    void rebuildResultRows();
    void buildResultModel();
    void buildMergedDetailResultModel();
    void buildDetailResultModel(const QString& systemName);
    QSet<int> requiredBoardIds() const;
    QString normalizedInnerFrame(const QString& frame) const;
    QString buildCollectedActualFrame() const;
    void tryForwardDiagResult();
    QString typeTextForCode(const QString& channel, int code) const;

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
    QString m_lastExpectedFrame;
    QHash<QString, int> m_lastExpectedStatus;
    QHash<QString, int> m_lastActualStatus;
    QSet<int> m_receivedBoardIds;
    QHash<int, QString> m_lastActualFrameByBoard;
    bool m_diagForwarded { false };
    QVariantList m_powerDiagRows;
    QVariantList m_commDiagRows;
    int m_pageIndex { 0 };
    QVariantList m_resultModel;
    QVariantList m_detailResultModel;
    QString m_resultDetailTitle;
    QString m_currentDetailSystem;
    bool m_detailMode { false };
};

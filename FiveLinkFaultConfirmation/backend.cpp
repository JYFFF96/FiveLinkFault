#include "backend.h"
#include "transporthub.h"

#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

Backend::Backend(QObject* parent)
    : QObject(parent)
{
    m_lastConfigTime = QDateTime::currentDateTime();
    loadAll();
    updateStatusText();
}

void Backend::loadAll()
{
    m_states.clear();
    emit statesChanged();
}

void Backend::markConfigChanged()
{
    m_lastConfigTime = QDateTime::currentDateTime();
}

bool Backend::setFaultCommon(const QString& stateId, bool on)
{
    if (stateId.isEmpty()) {
        return false;
    }

    const bool old = m_states.value(stateId, false).toBool();
    if (old == on) {
        return false;
    }

    m_states.insert(stateId, on);
    markConfigChanged();

    emit statesChanged();
    updateStatusText();
    return true;
}

bool Backend::setFaultSilent(const QString& stateId, bool on)
{
    const bool changed = setFaultCommon(stateId, on);
    if (changed) {
        qDebug() << "[Backend] setFaultSilent" << stateId << on;
    }
    return changed;
}

void Backend::applyStates(const QVariantMap& batch)
{
    if (batch.isEmpty()) {
        return;
    }

    bool anyChanged = false;

    for (auto it = batch.constBegin(); it != batch.constEnd(); ++it) {
        const QString stateId = it.key();
        const bool on = it.value().toBool();
        const bool old = m_states.value(stateId, false).toBool();
        if (old == on) {
            continue;
        }

        m_states.insert(stateId, on);
        anyChanged = true;
    }

    if (anyChanged) {
        markConfigChanged();
        emit statesChanged();
        updateStatusText();
    }
}

void Backend::sendRaw(const QString& frame)
{
    if (m_hub) {
        m_hub->sendRaw(frame);
    }
}

bool Backend::sendFaultFrame(const QVariantMap& fault, bool on)
{
    const QString channel = fault.value(QStringLiteral("channel")).toString().trimmed().toUpper();
    const int index = fault.value(QStringLiteral("index")).toInt();
    const QString faultType = fault.value(QStringLiteral("faultType")).toString().trimmed();
    const QString frame = buildFaultFrame(channel, index, faultType, on);
    if (frame.isEmpty()) {
        appLog(QStringLiteral("[TX ERR] 无法生成故障指令: channel=%1 index=%2 faultType=%3")
               .arg(channel)
               .arg(index)
               .arg(faultType));
        return false;
    }

    sendRaw(frame);
    appLog(QStringLiteral("[TX] %1").arg(frame));
    return true;
}

void Backend::resetAllStates()
{
    bool anyChanged = false;

    for (auto it = m_states.begin(); it != m_states.end(); ++it) {
        if (it.value().toBool()) {
            it.value() = false;
            anyChanged = true;
        }
    }

    if (anyChanged) {
        markConfigChanged();
        emit statesChanged();
        updateStatusText();
    }
}


bool Backend::applyIreadFrame(const QString& frame)
{
    const QString text = frame.trimmed();
    if (text.isEmpty()) {
        return false;
    }

    appLog(QStringLiteral("[报文] 收到: %1").arg(text));
    updateLastBoardNameFromFrame(text);

    int boardId = -1;
    QVector<int> rValues;
    QVector<int> cValues;
    if (!parseStatusValues(text, &boardId, &rValues, &cValues)) {
        appLog(QStringLiteral("[诊断] 返回报文未识别为 R/C 状态帧，未进入诊断结果页"));
        return true;
    }

    m_receivedBoardIds.insert(boardId);
    m_lastActualFrameByBoard.insert(boardId, normalizedInnerFrame(text));

    for (int i = 0; i < rValues.size(); ++i) {
        const int globalIndex = boardId * 20 + i + 1;
        m_lastActualStatus.insert(pointKey(QStringLiteral("R"), globalIndex), rValues.at(i));
    }

    for (int i = 0; i < cValues.size(); ++i) {
        const int globalIndex = boardId * 8 + i + 1;
        m_lastActualStatus.insert(pointKey(QStringLiteral("C"), globalIndex), cValues.at(i));
    }

    rebuildResultRows();
    buildMergedDetailResultModel();
    setPageIndex(1);
    appLog(QStringLiteral("[诊断] 已刷新 ID:%1 返回结果表格").arg(boardId));
    tryForwardDiagResult();
    return true;
}

void Backend::setFaultDefinitions(const QVariantList& faults)
{
    m_faultDefinitions = faults;
    m_faultMetas.clear();
    m_faultMetas.reserve(faults.size());

    for (const QVariant& v : faults) {
        const QVariantMap f = v.toMap();
        FaultMeta meta;
        meta.text = f.value(QStringLiteral("text")).toString();
        meta.point = pointNameFromText(meta.text);
        meta.systemName = f.value(QStringLiteral("channel")).toString().trimmed().toUpper() == QStringLiteral("C")
                ? QStringLiteral("通讯系统")
                : QStringLiteral("电源系统");
        meta.channel = f.value(QStringLiteral("channel")).toString().trimmed().toUpper();
        meta.faultType = f.value(QStringLiteral("faultType")).toString();
        meta.index = f.value(QStringLiteral("index")).toInt();
        meta.stateId = makeStateId(meta.channel, meta.index, meta.faultType);
        if (!meta.stateId.isEmpty()) {
            m_faultMetas.push_back(meta);
        }
    }
}

bool Backend::applyStatusFrame(const QString& frame)
{
    const QString text = frame.trimmed();
    if (text.isEmpty()) {
        return false;
    }

    if (text.startsWith(QStringLiteral("DIAG"), Qt::CaseInsensitive)) {
        return applyDiagFrame(QString(), text);
    }

    int boardId = -1;
    QVector<int> rValues;
    QVector<int> cValues;
    if (!parseStatusValues(text, &boardId, &rValues, &cValues)) {
        appLog(QStringLiteral("[报文] 收到: %1").arg(text));
        updateLastBoardNameFromFrame(text);
        return true;
    }

    QVariantMap batch;
    for (const FaultMeta& meta : m_faultMetas) {
        if (boardIdForIndex(meta.channel, meta.index) != boardId) {
            continue;
        }
        const int localIndex = localIndexForIndex(meta.channel, meta.index);
        if (localIndex <= 0) {
            continue;
        }

        int statusCode = 0;
        if (meta.channel == QStringLiteral("C")) {
            if (localIndex > cValues.size()) continue;
            statusCode = cValues.at(localIndex - 1);
        } else {
            if (localIndex > rValues.size()) continue;
            statusCode = rValues.at(localIndex - 1);
        }
        batch.insert(meta.stateId, statusCodeMatches(meta.channel, meta.faultType, statusCode));
    }

    if (!batch.isEmpty()) {
        applyStates(batch);
    }

    appLog(QStringLiteral("[报文] 收到: %1").arg(text));
    updateLastBoardNameFromFrame(QStringLiteral("#ID:%1;").arg(boardId));
    return true;
}


int Backend::boardIdForIndex(const QString& channel, int index) const
{
    if (index <= 0) return -1;
    const QString ch = channel.trimmed().toUpper();
    const int perBoard = (ch == QStringLiteral("C")) ? 8 : 20;
    return (index - 1) / perBoard;
}

int Backend::localIndexForIndex(const QString& channel, int index) const
{
    if (index <= 0) return -1;
    const QString ch = channel.trimmed().toUpper();
    const int perBoard = (ch == QStringLiteral("C")) ? 8 : 20;
    return ((index - 1) % perBoard) + 1;
}

int Backend::faultCodeForType(const QString& channel, const QString& faultType) const
{
    const QString ch = channel.trimmed().toUpper();
    const QString type = faultType.trimmed().toLower();
    if (type == QStringLiteral("open")) return 1;
    if (type == QStringLiteral("short")) return ch == QStringLiteral("C") ? 3 : 2;
    if (type == QStringLiteral("loose")) return ch == QStringLiteral("C") ? 4 : 3;
    return 0;
}

bool Backend::statusCodeMatches(const QString& channel, const QString& faultType, int statusCode) const
{
    const QString ch = channel.trimmed().toUpper();
    const QString type = faultType.trimmed().toLower();
    if (statusCode <= 0) return false;
    if (ch == QStringLiteral("C") && type == QStringLiteral("short")) {
        return statusCode == 2 || statusCode == 3;
    }
    return statusCode == faultCodeForType(ch, type);
}

QString Backend::buildFaultFrame(const QString& channel, int index, const QString& faultType, bool on) const
{
    const QString ch = channel.trimmed().toUpper();
    if (ch != QStringLiteral("R") && ch != QStringLiteral("C")) return QString();
    const int boardId = boardIdForIndex(ch, index);
    const int localIndex = localIndexForIndex(ch, index);
    if (boardId < 0 || boardId > 1 || localIndex <= 0) return QString();

    const int code = on ? faultCodeForType(ch, faultType) : 0;
    if (on && code <= 0) return QString();

    if (ch == QStringLiteral("C")) {
        return QStringLiteral("#ID:%1;C:%2;CS:%3;%").arg(boardId).arg(localIndex).arg(code);
    }
    return QStringLiteral("#ID:%1;R:%2;RS:%3;%").arg(boardId).arg(localIndex).arg(code);
}

bool Backend::parseStatusValues(const QString& text, int* id, QVector<int>* rValues, QVector<int>* cValues) const
{
    if (!id || !rValues || !cValues) return false;
    QString s = text.trimmed();
    if (s.startsWith(QLatin1Char('#'))) s.remove(0, 1);
    if (s.endsWith(QLatin1Char('%'))) s.chop(1);

    // 兼容 #NULL:ID:0;R:...;C:...;% 这类前缀，ID 前面不一定是分号。
    const QRegularExpression idRe(QStringLiteral(R"(\bID\s*:\s*(\d+))"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch idMatch = idRe.match(s);
    if (!idMatch.hasMatch()) return false;
    *id = idMatch.captured(1).toInt();

    auto parseList = [&s](const QString& name) -> QVector<int> {
        QVector<int> values;
        const QRegularExpression re(QStringLiteral(R"((^|;)\s*%1\s*:\s*([^;]+))").arg(name), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = re.match(s);
        if (!m.hasMatch()) return values;
        const QStringList parts = m.captured(2).split(QLatin1Char(','), Qt::SkipEmptyParts);
        values.reserve(parts.size());
        for (const QString& part : parts) {
            values.push_back(part.trimmed().toInt());
        }
        return values;
    };

    *rValues = parseList(QStringLiteral("R"));
    *cValues = parseList(QStringLiteral("C"));
    return !rValues->isEmpty() || !cValues->isEmpty();
}

QString Backend::normalizeIp(const QString& ip) const
{
    QString s = ip.trimmed();
    if (s.startsWith(QStringLiteral("::ffff:"))) {
        s = s.mid(7);
    }
    const int colon = s.indexOf(QLatin1Char(':'));
    if (colon > 0 && s.count(QLatin1Char(':')) == 1) {
        s = s.left(colon);
    }
    return s;
}

QString Backend::deviceNameForIp(const QString& ip) const
{
    const QString normalizedIp = normalizeIp(ip);
    const QString name = m_deviceNameMap.value(normalizedIp).toString().trimmed();
    return name.isEmpty() ? normalizedIp : name;
}

void Backend::setDeviceNameMap(const QVariantMap& names)
{
    m_deviceNameMap = names;
    bool changed = false;
    for (QVariant& itemVar : m_diagResults) {
        QVariantMap item = itemVar.toMap();
        const QString ip = item.value(QStringLiteral("ip")).toString();
        const QString name = deviceNameForIp(ip);
        if (item.value(QStringLiteral("deviceName")).toString() != name) {
            item.insert(QStringLiteral("deviceName"), name);
            itemVar = item;
            changed = true;
        }
    }
    if (changed) {
        emit diagResultsChanged();
    }
}

QString Backend::formatDuration(qint64 seconds) const
{
    if (seconds < 0) {
        seconds = 0;
    }
    const qint64 minutes = seconds / 60;
    const qint64 secs = seconds % 60;
    return QStringLiteral("%1′%2″").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

QString Backend::faultTypeName(const QString& faultType) const
{
    if (faultType == QStringLiteral("short")) return QStringLiteral("短路");
    if (faultType == QStringLiteral("open")) return QStringLiteral("断路");
    if (faultType == QStringLiteral("loose")) return QStringLiteral("虚接");
    return QStringLiteral("未设置");
}

QString Backend::makeStateId(const QString& channel, int index, const QString& faultType) const
{
    const QString ch = channel.trimmed().toUpper();
    const QString type = faultType.trimmed().toLower();
    if ((ch != QStringLiteral("R") && ch != QStringLiteral("C")) || index <= 0 || type.isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1:%2:%3").arg(ch).arg(index).arg(type);
}

QString Backend::pointNameFromText(const QString& text) const
{
    QString p = text;
    const int idx = p.lastIndexOf(QLatin1Char('-'));
    if (idx > 0) {
        p = p.left(idx);
    }
    return p.trimmed();
}

QVariantList Backend::buildDetailResults(const QHash<QString, int>& expected,
                                         const QHash<QString, int>& actual) const
{
    return buildDetailResultsFromMaps(expected, actual);
}

QVariantList Backend::buildDetailResultsFromMaps(const QHash<QString, int>& expected,
                                                 const QHash<QString, int>& actual) const
{
    QVariantList rows;
    QSet<QString> seen;

    for (const FaultMeta& meta : m_faultMetas) {
        const QString pk = pointKey(meta.channel, meta.index);
        const QString unique = meta.systemName + QStringLiteral("|") + pk;
        if (seen.contains(unique)) continue;
        seen.insert(unique);

        const int expCode = expected.value(pk, 0);
        const int actCode = actual.value(pk, 0);

        QString expType = QStringLiteral("未选择");
        QString actType = QStringLiteral("未设置");
        for (const FaultMeta& m : m_faultMetas) {
            if (pointKey(m.channel, m.index) != pk) continue;
            if (expCode > 0 && statusCodeMatches(m.channel, m.faultType, expCode)) expType = faultTypeName(m.faultType);
            if (actCode > 0 && statusCodeMatches(m.channel, m.faultType, actCode)) actType = faultTypeName(m.faultType);
        }

        QVariantMap row;
        row.insert(QStringLiteral("index"), rows.size() + 1);
        row.insert(QStringLiteral("systemName"), meta.systemName);
        row.insert(QStringLiteral("pointName"), meta.point);
        row.insert(QStringLiteral("faultType"), actType);
        row.insert(QStringLiteral("faultDiagnosis"), expType);
        row.insert(QStringLiteral("diagnosisResult"), expCode == actCode ? QStringLiteral("正确") : QStringLiteral("错误"));
        row.insert(QStringLiteral("ok"), expCode == actCode);
        rows.append(row);
    }
    return rows;
}

QVariantList Backend::buildSystemResults(const QVariantList& details) const
{
    QVariantList rows;
    if (details.isEmpty()) {
        return rows;
    }

    bool ok = true;
    for (const QVariant& v : details) {
        if (!v.toMap().value(QStringLiteral("ok")).toBool()) {
            ok = false;
            break;
        }
    }

    QVariantMap row;
    row.insert(QStringLiteral("index"), 1);
    row.insert(QStringLiteral("systemName"), QStringLiteral("五联动系统"));
    row.insert(QStringLiteral("diagnosisResult"), ok ? QStringLiteral("正确") : QStringLiteral("错误"));
    row.insert(QStringLiteral("ok"), ok);
    rows.append(row);
    return rows;
}

bool Backend::splitDiagFrame(const QString& frame, QString* expectedText, QString* actualText) const
{
    QString text = frame.trimmed();
    if (text.startsWith(QLatin1Char('#'))) text.remove(0, 1);
    if (text.endsWith(QLatin1Char('%'))) text.chop(1);

    if (text.startsWith(QStringLiteral("DIAG"), Qt::CaseInsensitive)) {
        text = text.mid(4);
    }
    while (text.startsWith(QLatin1Char(';')) || text.startsWith(QLatin1Char(':'))) {
        text.remove(0, 1);
    }

    // 支持 E:... / A:...，也兼容更短的 E#ID... / A#ID... 写法。
    const QRegularExpression markerRe(QStringLiteral(R"((^|[;$])\s*([EA])\s*([:=])?)"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = markerRe.globalMatch(text);
    struct Marker { int start; int valueStart; QString name; };
    QVector<Marker> markers;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        int valueStart = static_cast<int>(m.capturedEnd(2));
        if (!m.captured(3).isEmpty()) {
            valueStart = static_cast<int>(m.capturedEnd(0));
        }
        markers.push_back({static_cast<int>(m.capturedStart(2)), valueStart, m.captured(2).toUpper()});
    }
    if (markers.size() < 2) {
        return false;
    }

    QString e;
    QString a;
    for (int i = 0; i < markers.size(); ++i) {
        const int start = markers.at(i).valueStart;
        const int end = (i + 1 < markers.size()) ? markers.at(i + 1).start : static_cast<int>(text.size());
        QString payload = text.mid(start, end - start).trimmed();
        while (payload.startsWith(QLatin1Char(';')) || payload.startsWith(QLatin1Char('$'))) {
            payload.remove(0, 1);
        }
        if (markers.at(i).name == QStringLiteral("E")) e = payload;
        if (markers.at(i).name == QStringLiteral("A")) a = payload;
    }

    if (e.isEmpty() || a.isEmpty()) {
        return false;
    }

    if (expectedText) *expectedText = e;
    if (actualText) *actualText = a;
    return true;
}

bool Backend::applyDiagFrame(const QString& ip, const QString& frame)
{
    QString expectedText;
    QString actualText;
    if (!splitDiagFrame(frame, &expectedText, &actualText)) {
        appLog(QStringLiteral("[诊断] DIAG报文解析失败"));
        return false;
    }

    const QDateTime recvTime = QDateTime::currentDateTime();
    Q_UNUSED(expectedText)
    Q_UNUSED(actualText)
    const QVariantList details = buildDetailResults({}, {});
    const QVariantList systems = buildSystemResults(details);

    bool allOk = true;
    for (const QVariant& v : systems) {
        if (!v.toMap().value(QStringLiteral("ok")).toBool()) {
            allOk = false;
            break;
        }
    }

    const QString cleanIp = normalizeIp(ip);
    QVariantMap item;
    item.insert(QStringLiteral("sequence"), m_diagResults.size() + 1);
    item.insert(QStringLiteral("ip"), cleanIp);
    item.insert(QStringLiteral("deviceName"), deviceNameForIp(cleanIp));
    item.insert(QStringLiteral("diagnosisTime"), formatDuration(m_lastConfigTime.secsTo(recvTime)));
    item.insert(QStringLiteral("diagnosisResult"), allOk ? QStringLiteral("收到") : QStringLiteral("错误"));
    item.insert(QStringLiteral("ok"), allOk);
    item.insert(QStringLiteral("receiveTime"), recvTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    item.insert(QStringLiteral("rawDiagPacket"), frame.trimmed());
    item.insert(QStringLiteral("systemResults"), systems);
    item.insert(QStringLiteral("detailResults"), details);
    m_diagResults.append(item);
    emit diagResultsChanged();

    appLog(QStringLiteral("[诊断] %1 %2").arg(item.value(QStringLiteral("deviceName")).toString(), item.value(QStringLiteral("diagnosisResult")).toString()));
    return true;
}

QVariantList Backend::diagSystemResults(int index) const
{
    if (index < 0 || index >= m_diagResults.size()) {
        return {};
    }
    return m_diagResults.at(index).toMap().value(QStringLiteral("systemResults")).toList();
}

QVariantList Backend::diagDetailResults(int index, const QString& systemName) const
{
    QVariantList ret;
    if (index < 0 || index >= m_diagResults.size()) {
        return ret;
    }
    const QVariantList all = m_diagResults.at(index).toMap().value(QStringLiteral("detailResults")).toList();
    for (const QVariant& v : all) {
        const QVariantMap row = v.toMap();
        if (row.value(QStringLiteral("systemName")).toString() == systemName) {
            ret.append(row);
        }
    }
    return ret;
}


QString Backend::pointKey(const QString& channel, int index) const
{
    return QStringLiteral("%1:%2").arg(channel.trimmed().toUpper()).arg(index);
}

QHash<QString, int> Backend::expectedStatusMapFromSelection() const
{
    QHash<QString, int> expected;
    for (const FaultMeta& meta : m_faultMetas) {
        const QString pk = pointKey(meta.channel, meta.index);
        if (!expected.contains(pk)) expected.insert(pk, 0);
        if (m_states.value(meta.stateId, false).toBool()) {
            expected.insert(pk, faultCodeForType(meta.channel, meta.faultType));
        }
    }
    return expected;
}

QHash<QString, int> Backend::statusMapFromFrame(const QString& frame, int* boardIdOut) const
{
    int boardId = -1;
    QVector<int> rValues;
    QVector<int> cValues;
    QHash<QString, int> ret;
    if (!parseStatusValues(frame, &boardId, &rValues, &cValues)) {
        if (boardIdOut) *boardIdOut = -1;
        return ret;
    }
    if (boardIdOut) *boardIdOut = boardId;

    for (const FaultMeta& meta : m_faultMetas) {
        if (boardIdForIndex(meta.channel, meta.index) != boardId) continue;
        const int local = localIndexForIndex(meta.channel, meta.index);
        int code = 0;
        if (meta.channel == QStringLiteral("C")) {
            if (local > 0 && local <= cValues.size()) code = cValues.at(local - 1);
        } else {
            if (local > 0 && local <= rValues.size()) code = rValues.at(local - 1);
        }
        ret.insert(pointKey(meta.channel, meta.index), code);
    }
    return ret;
}

QString Backend::buildExpectedFrame(const QHash<QString, int>& expected) const
{
    QStringList parts;
    for (int board = 0; board <= 1; ++board) {
        QStringList rIdx, rVals, cIdx, cVals;
        for (const FaultMeta& meta : m_faultMetas) {
            if (boardIdForIndex(meta.channel, meta.index) != board) continue;
            const int local = localIndexForIndex(meta.channel, meta.index);
            if (local <= 0) continue;
            const QString pk = pointKey(meta.channel, meta.index);
            if (meta.channel == QStringLiteral("C")) {
                if (!cIdx.contains(QString::number(local))) {
                    cIdx << QString::number(local);
                    cVals << QString::number(expected.value(pk, 0));
                }
            } else {
                if (!rIdx.contains(QString::number(local))) {
                    rIdx << QString::number(local);
                    rVals << QString::number(expected.value(pk, 0));
                }
            }
        }
        if (!rIdx.isEmpty() || !cIdx.isEmpty()) {
            QString p = QStringLiteral("ID:%1;").arg(board);
            if (!rIdx.isEmpty()) p += QStringLiteral("RS:%1;").arg(rVals.join(','));
            if (!cIdx.isEmpty()) p += QStringLiteral("CS:%1;").arg(cVals.join(','));
            parts << p;
        }
    }
    return parts.join(QStringLiteral("$"));
}

QString Backend::normalizedInnerFrame(const QString& frame) const
{
    QString s = frame.trimmed();
    if (s.startsWith(QLatin1Char('#'))) s.remove(0, 1);
    if (s.endsWith(QLatin1Char('%'))) s.chop(1);
    return s.trimmed();
}

QString Backend::buildCollectedActualFrame() const
{
    QStringList parts;
    const QList<int> boards = m_lastActualFrameByBoard.keys();
    QList<int> ordered = boards;
    std::sort(ordered.begin(), ordered.end());
    for (const int board : ordered) {
        const QString part = m_lastActualFrameByBoard.value(board).trimmed();
        if (!part.isEmpty()) parts << part;
    }
    return parts.join(QStringLiteral("$"));
}

QString Backend::buildForwardDiagFrame(const QString& actualFrameRaw) const
{
    // 参考底盘故障确认软件：把“提交时勾选生成的预期状态 E”与“设备返回的实际状态 A”组合后转发。
    const QString e = normalizedInnerFrame(m_lastExpectedFrame);
    const QString a = normalizedInnerFrame(actualFrameRaw);
    return QStringLiteral("DIAG;E:%1;A:%2;").arg(e, a);
}

void Backend::forwardDiagResult(const QString& actualFrameRaw)
{
    const QString frame = buildForwardDiagFrame(actualFrameRaw);
    if (frame.isEmpty()) return;
    sendRaw(frame);
    appLog(QStringLiteral("[TX] 诊断结果转发: %1").arg(frame));
}

QSet<int> Backend::requiredBoardIds() const
{
    QSet<int> ret;
    for (const FaultMeta& meta : m_faultMetas) {
        const int boardId = boardIdForIndex(meta.channel, meta.index);
        if (boardId >= 0) ret.insert(boardId);
    }
    if (ret.isEmpty()) {
        ret.insert(0);
        ret.insert(1);
    }
    return ret;
}

void Backend::tryForwardDiagResult()
{
    if (m_diagForwarded) return;

    const QSet<int> required = requiredBoardIds();
    for (const int boardId : required) {
        if (!m_receivedBoardIds.contains(boardId)) {
            appLog(QStringLiteral("[诊断] 已收到部分返回，等待 ID:%1 报文后再转发诊断结果").arg(boardId));
            return;
        }
    }

    const QString actual = buildCollectedActualFrame();
    if (actual.isEmpty()) return;

    forwardDiagResult(actual);
    m_diagForwarded = true;
}


QVariantList Backend::buildChannelResultRows(const QString& channel) const
{
    QVariantList rows;
    const QString ch = channel.trimmed().toUpper();
    QSet<QString> seen;

    for (const FaultMeta& meta : m_faultMetas) {
        if (meta.channel != ch) {
            continue;
        }

        const QString pk = pointKey(meta.channel, meta.index);
        if (seen.contains(pk)) {
            continue;
        }
        seen.insert(pk);

        const int boardId = boardIdForIndex(meta.channel, meta.index);
        const bool received = m_receivedBoardIds.contains(boardId);
        const int expCode = m_lastExpectedStatus.value(pk, 0);
        const int actCode = m_lastActualStatus.value(pk, 0);

        QString expType = QStringLiteral("未勾选");
        QString actType = received ? QStringLiteral("无故障") : QStringLiteral("等待返回");

        for (const FaultMeta& m : m_faultMetas) {
            if (pointKey(m.channel, m.index) != pk) {
                continue;
            }
            if (expCode > 0 && statusCodeMatches(m.channel, m.faultType, expCode)) {
                expType = faultTypeName(m.faultType);
            }
            if (received && actCode > 0 && statusCodeMatches(m.channel, m.faultType, actCode)) {
                actType = faultTypeName(m.faultType);
            }
        }

        QVariantMap row;
        row.insert(QStringLiteral("index"), meta.index);
        row.insert(QStringLiteral("localIndex"), localIndexForIndex(meta.channel, meta.index));
        row.insert(QStringLiteral("boardId"), boardId);
        row.insert(QStringLiteral("pointName"), meta.point.isEmpty() ? QStringLiteral("%1%2").arg(ch).arg(meta.index) : meta.point);
        row.insert(QStringLiteral("expected"), expType);
        row.insert(QStringLiteral("actual"), actType);
        row.insert(QStringLiteral("expectedCode"), expCode);
        row.insert(QStringLiteral("actualCode"), received ? actCode : -1);
        row.insert(QStringLiteral("received"), received);
        row.insert(QStringLiteral("ok"), received && expCode == actCode);
        row.insert(QStringLiteral("result"), received ? (expCode == actCode ? QStringLiteral("正确") : QStringLiteral("错误")) : QStringLiteral("等待"));
        rows.append(row);
    }

    std::sort(rows.begin(), rows.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("index")).toInt() < b.toMap().value(QStringLiteral("index")).toInt();
    });

    return rows;
}

void Backend::rebuildResultRows()
{
    m_powerDiagRows = buildChannelResultRows(QStringLiteral("R"));
    m_commDiagRows = buildChannelResultRows(QStringLiteral("C"));
    emit resultRowsChanged();
    buildResultModel();
    buildMergedDetailResultModel();
    if (m_detailMode && !m_currentDetailSystem.isEmpty()) {
        buildDetailResultModel(m_currentDetailSystem);
    }
}

QString Backend::commitSelection()
{
    m_lastExpectedStatus = expectedStatusMapFromSelection();
    m_lastActualStatus.clear();
    m_receivedBoardIds.clear();
    m_lastActualFrameByBoard.clear();
    m_diagForwarded = false;
    m_detailResultModel.clear();
    m_resultDetailTitle.clear();
    m_currentDetailSystem.clear();
    m_detailMode = false;
    emit detailResultModelChanged();
    emit resultDetailTitleChanged();
    emit detailModeChanged();
    m_lastExpectedFrame = buildExpectedFrame(m_lastExpectedStatus);
    m_lastConfigTime = QDateTime::currentDateTime();
    rebuildResultRows();
    buildMergedDetailResultModel();

    appLog(QStringLiteral("[提交] 已锁定当前勾选：%1").arg(m_lastExpectedFrame));
    appLog(QStringLiteral("[提交] 等待设备返回诊断结果"));
    return m_lastExpectedFrame;
}


void Backend::setPageIndex(int idx)
{
    idx = (idx <= 0) ? 0 : 1;
    if (m_pageIndex == idx) return;
    m_pageIndex = idx;
    emit pageIndexChanged();
}

QString Backend::typeTextForCode(const QString& channel, int code) const
{
    if (code <= 0) return QStringLiteral("未设置");
    const QString ch = channel.trimmed().toUpper();
    if (code == 1) return QStringLiteral("断路");
    if (ch == QStringLiteral("R") && code == 2) return QStringLiteral("短路");
    if (ch == QStringLiteral("R") && code == 3) return QStringLiteral("虚接");
    if (ch == QStringLiteral("C") && (code == 2 || code == 3)) return QStringLiteral("短路");
    if (ch == QStringLiteral("C") && code == 4) return QStringLiteral("虚接");
    return QStringLiteral("未知(%1)").arg(code);
}

void Backend::buildResultModel()
{
    QVariantList rows;
    auto appendSystem = [&](const QString& systemName, const QString& channel) {
        bool hasAny = false;
        bool ok = true;
        for (const FaultMeta& meta : m_faultMetas) {
            if (meta.channel != channel) continue;
            const QString pk = pointKey(meta.channel, meta.index);
            const int boardId = boardIdForIndex(meta.channel, meta.index);
            if (!m_receivedBoardIds.contains(boardId)) continue;
            hasAny = true;
            if (m_lastExpectedStatus.value(pk, 0) != m_lastActualStatus.value(pk, 0)) ok = false;
        }
        QVariantMap row;
        row.insert(QStringLiteral("systemName"), systemName);
        row.insert(QStringLiteral("sysText"), systemName);
        row.insert(QStringLiteral("pageId"), channel);
        row.insert(QStringLiteral("ok"), hasAny && ok);
        row.insert(QStringLiteral("received"), hasAny);
        row.insert(QStringLiteral("resultText"), hasAny ? (ok ? QStringLiteral("正确") : QStringLiteral("错误")) : QStringLiteral("等待"));
        rows.append(row);
    };
    appendSystem(QStringLiteral("电源故障"), QStringLiteral("R"));
    appendSystem(QStringLiteral("通讯故障"), QStringLiteral("C"));
    m_resultModel = rows;
    emit resultModelChanged();
}

void Backend::buildMergedDetailResultModel()
{
    QVariantList rows;
    QSet<QString> seen;

    auto appendChannel = [&](const QString& ch, const QString& category) {
        for (const FaultMeta& meta : m_faultMetas) {
            if (meta.channel != ch) continue;
            const QString pk = pointKey(meta.channel, meta.index);
            if (seen.contains(pk)) continue;
            seen.insert(pk);

            const int boardId = boardIdForIndex(meta.channel, meta.index);
            const bool received = m_receivedBoardIds.contains(boardId);
            const int expCode = m_lastExpectedStatus.value(pk, 0);
            const int actCode = m_lastActualStatus.value(pk, 0);

            QVariantMap row;
            row.insert(QStringLiteral("index"), rows.size() + 1);
            row.insert(QStringLiteral("categoryText"), category);
            row.insert(QStringLiteral("pointText"), meta.point.isEmpty() ? QStringLiteral("%1%2").arg(ch).arg(meta.index) : meta.point);
            row.insert(QStringLiteral("setTypeText"), received ? typeTextForCode(ch, actCode) : QStringLiteral("等待返回"));
            row.insert(QStringLiteral("judgeTypeText"), expCode > 0 ? typeTextForCode(ch, expCode) : QStringLiteral("未选择"));
            row.insert(QStringLiteral("ok"), received && expCode == actCode);
            row.insert(QStringLiteral("received"), received);
            rows.append(row);
        }
    };

    appendChannel(QStringLiteral("R"), QStringLiteral("电源故障"));
    appendChannel(QStringLiteral("C"), QStringLiteral("通讯故障"));

    m_detailResultModel = rows;
    emit detailResultModelChanged();
}

void Backend::buildDetailResultModel(const QString& systemName)
{
    const QString ch = (systemName == QStringLiteral("C") || systemName.contains(QStringLiteral("通讯")))
            ? QStringLiteral("C") : QStringLiteral("R");
    QVariantList rows;
    QSet<QString> seen;
    for (const FaultMeta& meta : m_faultMetas) {
        if (meta.channel != ch) continue;
        const QString pk = pointKey(meta.channel, meta.index);
        if (seen.contains(pk)) continue;
        seen.insert(pk);

        const int boardId = boardIdForIndex(meta.channel, meta.index);
        const bool received = m_receivedBoardIds.contains(boardId);
        const int expCode = m_lastExpectedStatus.value(pk, 0);
        const int actCode = m_lastActualStatus.value(pk, 0);

        QVariantMap row;
        row.insert(QStringLiteral("index"), rows.size() + 1);
        row.insert(QStringLiteral("pointText"), meta.point.isEmpty() ? QStringLiteral("%1%2").arg(ch).arg(meta.index) : meta.point);
        row.insert(QStringLiteral("setTypeText"), received ? typeTextForCode(ch, actCode) : QStringLiteral("等待返回"));
        row.insert(QStringLiteral("judgeTypeText"), expCode > 0 ? typeTextForCode(ch, expCode) : QStringLiteral("未选择"));
        row.insert(QStringLiteral("ok"), received && expCode == actCode);
        row.insert(QStringLiteral("received"), received);
        rows.append(row);
    }
    m_detailResultModel = rows;
    emit detailResultModelChanged();
}

void Backend::openResultDetail(const QString& systemName)
{
    const QString ch = (systemName == QStringLiteral("C") || systemName.contains(QStringLiteral("通讯")))
            ? QStringLiteral("C") : QStringLiteral("R");
    m_currentDetailSystem = ch;
    m_resultDetailTitle = (ch == QStringLiteral("C")) ? QStringLiteral("通讯故障") : QStringLiteral("电源故障");
    buildDetailResultModel(ch);
    if (!m_detailMode) {
        m_detailMode = true;
        emit detailModeChanged();
    }
    emit resultDetailTitleChanged();
}

void Backend::closeResultDetail()
{
    if (!m_detailMode) return;
    m_detailMode = false;
    emit detailModeChanged();
}

void Backend::continueDiagnosis(bool clearSelections)
{
    m_lastActualStatus.clear();
    m_lastExpectedStatus.clear();
    m_receivedBoardIds.clear();
    m_lastActualFrameByBoard.clear();
    m_diagForwarded = false;
    m_lastExpectedFrame.clear();
    m_powerDiagRows.clear();
    m_commDiagRows.clear();
    m_resultModel.clear();
    m_detailResultModel.clear();
    m_resultDetailTitle.clear();
    m_currentDetailSystem.clear();
    m_detailMode = false;
    emit resultRowsChanged();
    emit resultModelChanged();
    emit detailResultModelChanged();
    emit resultDetailTitleChanged();
    emit detailModeChanged();
    if (clearSelections) resetAllStates();
    setPageIndex(0);
}

void Backend::updateLastBoardNameFromFrame(const QString& frame)
{
    const QRegularExpression re(QStringLiteral(R"(^\s*#([^:;]+)[:;])"));
    const QRegularExpressionMatch match = re.match(frame);
    if (!match.hasMatch()) {
        return;
    }

    const QString name = match.captured(1).trimmed();
    if (name.isEmpty() || name == m_lastBoardName) {
        return;
    }

    m_lastBoardName = name;
    emit lastBoardNameChanged();
    qDebug() << "[Backend] board name from frame =" << m_lastBoardName;
}

void Backend::updateStatusText()
{
    int opened = 0;
    for (auto it = m_states.constBegin(); it != m_states.constEnd(); ++it) {
        if (it.value().toBool()) {
            ++opened;
        }
    }

    const QString text = opened == 0
            ? QStringLiteral("当前未选中故障项")
            : QStringLiteral("当前已勾选 %1 个故障项").arg(opened);

    if (text == m_statusText) {
        return;
    }

    m_statusText = text;
    emit statusTextChanged();
}

QString Backend::logText() const
{
    return m_logBuffer.join(QStringLiteral("\n"));
}

void Backend::clearLog()
{
    if (m_logBuffer.isEmpty()) {
        return;
    }

    m_logBuffer.clear();
    emit logTextChanged();
}

void Backend::setLogMax(int n)
{
    m_logMax = qMax(10, n);
    if (m_logBuffer.size() > m_logMax) {
        m_logBuffer.erase(m_logBuffer.begin(),
                          m_logBuffer.begin() + (m_logBuffer.size() - m_logMax));
        emit logTextChanged();
    }
}

void Backend::appLog(const QString& line)
{
    const QString text = QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), line);

    m_logBuffer.push_back(text);
    if (m_logBuffer.size() > m_logMax) {
        m_logBuffer.erase(m_logBuffer.begin(),
                          m_logBuffer.begin() + (m_logBuffer.size() - m_logMax));
    }

    emit logTextChanged();
}

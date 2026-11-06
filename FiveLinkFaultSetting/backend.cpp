#include "backend.h"
#include "transporthub.h"

#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QSet>

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

void Backend::requestIRead()
{
    sendRaw(QStringLiteral("#ID:0;READ;%"));
    sendRaw(QStringLiteral("#ID:1;READ;%"));
}

bool Backend::applyIreadFrame(const QString& frame)
{
    const QString text = frame.trimmed();
    if (text.startsWith(QStringLiteral("#DIAG"), Qt::CaseInsensitive) ||
        text.startsWith(QStringLiteral("DIAG"), Qt::CaseInsensitive)) {
        return applyDiagFrame(QString(), text);
    }
    return applyStatusFrame(text);
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

    if (text.startsWith(QStringLiteral("#DIAG"), Qt::CaseInsensitive) ||
        text.startsWith(QStringLiteral("DIAG"), Qt::CaseInsensitive)) {
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
    updateLastBoardNameFromFrame(text);
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

    const QRegularExpression idRe(QStringLiteral(R"((^|[:;])\s*ID\s*:\s*(\d+))"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch idMatch = idRe.match(s);
    if (!idMatch.hasMatch()) return false;
    *id = idMatch.captured(2).toInt();

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

QString Backend::diagKey(const QString& channel, int index) const
{
    return QStringLiteral("%1:%2").arg(channel.trimmed().toUpper()).arg(index);
}

QHash<QString, int> Backend::parseDiagValueMap(const QString& text, bool selectedValues) const
{
    QHash<QString, int> ret;
    QString content = text.trimmed();
    content.remove(QLatin1Char('#'));
    content.remove(QLatin1Char('%'));

    // DIAG 的 E 段使用 RS/CS 表示“选中的结果”，A 段使用 R/C 表示故障板实际状态。
    const QString rName = selectedValues ? QStringLiteral("RS") : QStringLiteral("R");
    const QString cName = selectedValues ? QStringLiteral("CS") : QStringLiteral("C");

    const QStringList parts = content.split(QLatin1Char('$'), Qt::SkipEmptyParts);
    for (QString part : parts) {
        part = part.trimmed();
        while (part.startsWith(QLatin1Char(';'))) {
            part.remove(0, 1);
        }

        // 兼容 A:NULL:ID:0;R:... 这种 ID 前面带 NULL: 的写法。
        const QRegularExpression idRe(QStringLiteral(R"((^|[:;])\s*ID\s*:\s*(\d+))"),
                                      QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch idMatch = idRe.match(part);
        if (!idMatch.hasMatch()) {
            continue;
        }
        const int boardId = idMatch.captured(2).toInt();

        auto parseList = [&part](const QString& name) -> QVector<int> {
            QVector<int> values;
            const QRegularExpression re(QStringLiteral(R"((^|;)\s*%1\s*:\s*([^;]+))").arg(name),
                                        QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = re.match(part);
            if (!m.hasMatch()) {
                return values;
            }
            const QStringList nums = m.captured(2).split(QLatin1Char(','), Qt::SkipEmptyParts);
            values.reserve(nums.size());
            for (const QString& num : nums) {
                values.push_back(num.trimmed().toInt());
            }
            return values;
        };

        const QVector<int> rValues = parseList(rName);
        for (int i = 0; i < rValues.size(); ++i) {
            const int globalIndex = boardId * 20 + i + 1;
            ret.insert(diagKey(QStringLiteral("R"), globalIndex), rValues.at(i));
        }

        const QVector<int> cValues = parseList(cName);
        for (int i = 0; i < cValues.size(); ++i) {
            const int globalIndex = boardId * 8 + i + 1;
            ret.insert(diagKey(QStringLiteral("C"), globalIndex), cValues.at(i));
        }
    }

    return ret;
}

QVariantList Backend::buildDetailResults(const QHash<QString, int>& expected,
                                         const QHash<QString, int>& actual) const
{
    auto configText = [](const QString& channel, int code) -> QString {
        const QString ch = channel.trimmed().toUpper();
        if (code == 1) return QStringLiteral("断路");
        if (code == 2) return QStringLiteral("短路");
        if (code == 3) return ch == QStringLiteral("C") ? QStringLiteral("短路") : QStringLiteral("虚接");
        if (code == 4 && ch == QStringLiteral("C")) return QStringLiteral("虚接");
        return QStringLiteral("未设置");
    };

    auto resultText = [](const QString& channel, int code) -> QString {
        const QString ch = channel.trimmed().toUpper();
        if (code == 1) return QStringLiteral("断路");
        if (code == 2) return QStringLiteral("短路");
        if (code == 3) return ch == QStringLiteral("C") ? QStringLiteral("短路") : QStringLiteral("虚接");
        if (code == 4 && ch == QStringLiteral("C")) return QStringLiteral("虚接");
        return QStringLiteral("未选择");
    };

    auto normalizedType = [](const QString& channel, int code) -> QString {
        const QString ch = channel.trimmed().toUpper();
        if (code == 1) return QStringLiteral("open");
        if (code == 2) return QStringLiteral("short");
        if (code == 3) return ch == QStringLiteral("C") ? QStringLiteral("short") : QStringLiteral("loose");
        if (code == 4 && ch == QStringLiteral("C")) return QStringLiteral("loose");
        return QStringLiteral("none");
    };

    QVariantList rows;
    QSet<QString> visitedPoints;
    for (const FaultMeta& meta : m_faultMetas) {
        const QString key = diagKey(meta.channel, meta.index);
        if (visitedPoints.contains(key)) {
            continue;
        }
        visitedPoints.insert(key);

        const int expectedCode = expected.value(key, 0);
        const int actualCode = actual.value(key, 0);
        const bool ok = normalizedType(meta.channel, expectedCode) == normalizedType(meta.channel, actualCode);

        QVariantMap row;
        row.insert(QStringLiteral("index"), rows.size() + 1);
        row.insert(QStringLiteral("systemName"), meta.systemName);
        row.insert(QStringLiteral("pointName"), meta.point);
        row.insert(QStringLiteral("faultType"), configText(meta.channel, expectedCode));
        row.insert(QStringLiteral("faultDiagnosis"), resultText(meta.channel, actualCode));
        row.insert(QStringLiteral("diagnosisResult"), ok ? QStringLiteral("正确") : QStringLiteral("错误"));
        row.insert(QStringLiteral("ok"), ok);
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
    const QHash<QString, int> expected = parseDiagValueMap(expectedText, true);
    const QHash<QString, int> actual = parseDiagValueMap(actualText, false);
    const QVariantList details = buildDetailResults(actual, expected);
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
    item.insert(QStringLiteral("diagnosisResult"), allOk ? QStringLiteral("正确") : QStringLiteral("错误"));
    item.insert(QStringLiteral("ok"), allOk);
    item.insert(QStringLiteral("receiveTime"), recvTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    item.insert(QStringLiteral("rawDiagPacket"), frame.trimmed());
    item.insert(QStringLiteral("systemResults"), systems);
    item.insert(QStringLiteral("detailResults"), details);
    m_diagResults.append(item);
    emit diagResultsChanged();

    updateLastBoardNameFromFrame(frame);
    appLog(QStringLiteral("[诊断] 收到: %1").arg(frame.trimmed()));
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
    if (systemName == QStringLiteral("五联动系统")) {
        return all;
    }
    for (const QVariant& v : all) {
        const QVariantMap row = v.toMap();
        if (row.value(QStringLiteral("systemName")).toString() == systemName) {
            ret.append(row);
        }
    }
    return ret;
}

void Backend::updateLastBoardNameFromFrame(const QString& frame)
{
    QString text = frame.trimmed();
    if (text.startsWith(QLatin1Char('#'))) {
        text.remove(0, 1);
    }
    if (text.endsWith(QLatin1Char('%'))) {
        text.chop(1);
    }

    // 状态报文格式示例：abc:ID:0;R:...;C:...
    // DIAG 报文实际段示例：DIAG;...;;A:abc:ID:0;R:...;C:...
    // 其中 abc 是板卡自报名称，ID:0 是板卡序号。
    // 旧格式 #ID:0;... 没有板卡名称，不能把 "ID" 当成名称写入 ComboBox。
    int aPos = text.indexOf(QStringLiteral("A:"), 0, Qt::CaseInsensitive);
    if (aPos >= 0) {
        text = text.mid(aPos + 2);
        while (text.startsWith(QLatin1Char(';')) || text.startsWith(QLatin1Char(':'))) {
            text.remove(0, 1);
        }
    }

    const QRegularExpression re(QStringLiteral(R"(^\s*([^:;]+)\s*:\s*ID\s*:\s*\d+)"),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch()) {
        return;
    }

    const QString name = match.captured(1).trimmed();
    if (name.isEmpty() || name.compare(QStringLiteral("ID"), Qt::CaseInsensitive) == 0 || name == m_lastBoardName) {
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
            ? QStringLiteral("当前无故障开启")
            : QStringLiteral("当前已开启 %1 个故障开关").arg(opened);

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

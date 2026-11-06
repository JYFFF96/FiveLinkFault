#include "appconfig.h"

#include <QFile>
#include <QDomDocument>
#include <QDebug>

AppConfig::AppConfig(QObject *parent)
    : QObject(parent)
{
}

bool AppConfig::load(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "打开配置文件失败:" << filePath;
        return false;
    }

    QDomDocument doc;
    QString errorMsg;
    int errorLine = 0;
    int errorColumn = 0;

    if (!doc.setContent(&file, &errorMsg, &errorLine, &errorColumn)) {
        qWarning() << "解析XML失败:" << errorMsg
                   << "line:" << errorLine
                   << "column:" << errorColumn;
        return false;
    }

    QDomElement root = doc.documentElement();
    if (root.tagName() != "AppConfig") {
        qWarning() << "XML根节点不是 AppConfig";
        return false;
    }

    m_appTitle.clear();
    m_resetFrame.clear();
    m_resetFrames.clear();
    m_readFrame.clear();
    m_readFrames.clear();
    m_sections.clear();
    m_allFaults.clear();

    m_appTitle = root.firstChildElement("AppTitle").text().trimmed();

    QDomElement commandsElement = root.firstChildElement("Commands");
    QDomNodeList resetFrameNodes = commandsElement.elementsByTagName("ResetFrame");
    for (int i = 0; i < resetFrameNodes.count(); ++i) {
        const QString frame = resetFrameNodes.at(i).toElement().text().trimmed();
        if (!frame.isEmpty()) {
            m_resetFrames.append(frame);
        }
    }

    if (!m_resetFrames.isEmpty()) {
        m_resetFrame = m_resetFrames.first().toString();
    } else {
        m_resetFrame = QStringLiteral("#ID:0;R:1,2,3,4;RS:0,0,0,0;C:1,2;CS:0,0;%");
        m_resetFrames.append(m_resetFrame);
    }

    QDomNodeList readFrameNodes = commandsElement.elementsByTagName("ReadFrame");
    for (int i = 0; i < readFrameNodes.count(); ++i) {
        const QString frame = readFrameNodes.at(i).toElement().text().trimmed();
        if (!frame.isEmpty()) {
            m_readFrames.append(frame);
        }
    }

    if (!m_readFrames.isEmpty()) {
        m_readFrame = m_readFrames.first().toString();
    } else {
        m_readFrame = QStringLiteral("#ID:0;READ;%");
        m_readFrames.append(m_readFrame);
    }

    QDomElement pagesElement = root.firstChildElement("FaultPages");
    QDomElement pageElement = pagesElement.firstChildElement("Page");
    if (pageElement.isNull()) {
        pageElement = root.firstChildElement("FaultPage");
    }

    QDomNodeList sectionNodes = pageElement.elementsByTagName("Section");
    for (int i = 0; i < sectionNodes.count(); ++i) {
        QDomElement sectionElement = sectionNodes.at(i).toElement();

        QVariantMap section;
        section["title"] = sectionElement.attribute("title");

        QVariantList faults;
        QDomNodeList faultNodes = sectionElement.elementsByTagName("Fault");
        for (int j = 0; j < faultNodes.count(); ++j) {
            QDomElement faultElement = faultNodes.at(j).toElement();

            QVariantMap fault;
            fault["text"] = faultElement.attribute("text");
            fault["index"] = faultElement.attribute("index").toInt();
            fault["faultType"] = faultElement.attribute("faultType");
            const QString sectionTitle = sectionElement.attribute("title");
            fault["channel"] = sectionTitle.contains(QStringLiteral("通讯")) ? QStringLiteral("C") : QStringLiteral("R");

            QString clickable = faultElement.attribute("clickable", "true").trimmed().toLower();
            fault["clickable"] = !(clickable == "false" || clickable == "0" || clickable == "no");

            faults.append(fault);
            m_allFaults.append(fault);
        }

        section["items"] = faults;
        m_sections.append(section);
    }

    emit loaded();
    return true;
}

QString AppConfig::appTitle() const
{
    return m_appTitle;
}

QString AppConfig::resetFrame() const
{
    return m_resetFrame;
}

QVariantList AppConfig::resetFrames() const
{
    return m_resetFrames;
}

QString AppConfig::readFrame() const
{
    return m_readFrame;
}

QVariantList AppConfig::readFrames() const
{
    return m_readFrames;
}

QVariantList AppConfig::sections() const
{
    return m_sections;
}

QVariantList AppConfig::allFaults() const
{
    return m_allFaults;
}

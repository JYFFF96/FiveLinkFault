#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>

#include "appconfig.h"
#include "backend.h"
#include "serialmanager.h"
#include "wifimanager.h"
#include "transporthub.h"
#include <QIcon>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/images/logo.ico"));
    QCoreApplication::setOrganizationName("zxkedu");
    QCoreApplication::setApplicationName("FiveLinkFaultSetting");
    qDebug() << "config exists:"
             << QFile::exists(":/config/AppConfig.xml");

    qDebug() << "logo exists:"
             << QFile::exists(":/images/logo.ico");
    AppConfig appConfig;
    QString configPath;
#ifdef Q_OS_ANDROID
    configPath = ":/config/AppConfig.xml";
#else
    configPath = QCoreApplication::applicationDirPath()
                 + "/config/AppConfig.xml";

    if (!QFile::exists(configPath)) {
        configPath = ":/config/AppConfig.xml";
    }
#endif

    if (!appConfig.load(configPath)) {
        qWarning() << "配置文件加载失败:" << configPath;
    }

    Backend backend;
    SerialManager serial;
    WifiManager wifi;
    TransportHub hub(&backend, &serial, &wifi);

    backend.setHub(&hub);
    backend.setFaultDefinitions(appConfig.allFaults());

    QObject::connect(&serial, &SerialManager::frameDecoded,
                     &hub, &TransportHub::applyIncoming);

    // Wi-Fi 使用带 IP 的通道，避免同一帧通过 frameDecoded / frameDecodedWithIp 重复解析。
    QObject::connect(&wifi, &WifiManager::frameDecodedWithIp,
                     &hub, &TransportHub::applyIncomingWithIp);

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("AppConfig", &appConfig);
    engine.rootContext()->setContextProperty("Backend", &backend);
    engine.rootContext()->setContextProperty("Serial", &serial);
    engine.rootContext()->setContextProperty("Wifi", &wifi);
    engine.rootContext()->setContextProperty("Hub", &hub);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("FiveLinkFaultSetting", "Main");

    return app.exec();
}

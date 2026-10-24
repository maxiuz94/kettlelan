// KettleLAN – graphical application (Qt 6 / QML).
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "backend.h"
#include "util.h"

#ifndef KETTLELAN_VERSION
#define KETTLELAN_VERSION "dev"
#endif

namespace {

// Writes the log to the terminal and to a file at the same time (Settings → Open log).
class TeeBuf : public std::streambuf {
public:
    TeeBuf(std::streambuf* a, std::streambuf* b) : a_(a), b_(b) {}

protected:
    int overflow(int c) override {
        if (c == EOF) return !EOF;
        const int r1 = a_ ? a_->sputc(char(c)) : c;
        const int r2 = b_ ? b_->sputc(char(c)) : c;
        return (r1 == EOF || r2 == EOF) ? EOF : c;
    }
    int sync() override {
        const int r1 = a_ ? a_->pubsync() : 0;
        const int r2 = b_ ? b_->pubsync() : 0;
        return (r1 == 0 && r2 == 0) ? 0 : -1;
    }

private:
    std::streambuf* a_;
    std::streambuf* b_;
};

QString instanceName() {
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) user = qEnvironmentVariable("USERNAME");
    return QStringLiteral("kettlelan-") + user;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KettleLAN"));
    QApplication::setApplicationName(QStringLiteral("KettleLAN"));
    QApplication::setApplicationVersion(QStringLiteral(KETTLELAN_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("kettlelan"));
    QApplication::setWindowIcon(kettle::gui::makeAppIcon(false));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Virtual LAN over the Steam network"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimized(QStringLiteral("minimized"), QStringLiteral("Start hidden in the system tray."));
    parser.addOption(minimized);
    QCommandLineOption writeIcon(QStringLiteral("write-icon"), QStringLiteral("Save the application icon to a PNG file."),
                                 QStringLiteral("file"));
    parser.addOption(writeIcon);
    QCommandLineOption iconSize(QStringLiteral("icon-size"), QStringLiteral("Icon size in pixels (default 256)."),
                                QStringLiteral("px"), QStringLiteral("256"));
    parser.addOption(iconSize);
    parser.process(app);

    if (parser.isSet(writeIcon)) {
        const int size = std::clamp(parser.value(iconSize).toInt(), 16, 1024);
        const bool ok = kettle::gui::renderAppIcon(size, false).save(parser.value(writeIcon));
        return ok ? 0 : 1;
    }

    const bool demo = qEnvironmentVariableIsSet("KETTLELAN_DEMO");

    // Only one instance – a second one shows the first one's window and exits.
    QLocalServer server;
    if (!demo) {
        QLocalSocket probe;
        probe.connectToServer(instanceName());
        if (probe.waitForConnected(300)) {
            probe.write("show\n");
            probe.waitForBytesWritten(300);
            std::puts("KettleLAN is already running – showing its window.");
            return 0;
        }
        QLocalServer::removeServer(instanceName());
        server.setSocketOptions(QLocalServer::UserAccessOption); // our user only
        server.listen(instanceName());
    }

    // Log: terminal + file.
    std::ofstream logFile(kettle::gui::logFilePath().toStdString(), std::ios::trunc);
    // the log contains members' names and addresses – owner only
    QFile::setPermissions(kettle::gui::logFilePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    TeeBuf tee(std::clog.rdbuf(), logFile.is_open() ? logFile.rdbuf() : nullptr);
    std::clog.rdbuf(&tee);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    kettle::gui::Backend backend(demo);
    qmlRegisterSingletonInstance("KettleLAN.Core", 1, 0, "Backend", &backend);

    QObject::connect(&server, &QLocalServer::newConnection, &backend, [&server, &backend] {
        while (QLocalSocket* s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            backend.showWindow();
        }
    });

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { std::_Exit(1); },
                     Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/KettleLAN/Main.qml")));
    if (engine.rootObjects().isEmpty()) std::_Exit(1);

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    backend.attachWindow(window);
    if (!parser.isSet(minimized) || !backend.trayAvailable()) backend.showWindow();
    backend.startEngine();

    // Screenshot for the docs: KETTLELAN_DEMO=1 KETTLELAN_SCREENSHOT=file.png
    const QString shot = qEnvironmentVariable("KETTLELAN_SCREENSHOT");
    if (!shot.isEmpty() && window) {
        QTimer::singleShot(1500, &app, [window, shot] {
            window->grabWindow().save(shot);
            std::_Exit(0);
        });
    }

    const int ret = app.exec();
    std::clog.flush();
    logFile.flush();
    // Steam is already disconnected (Backend::quitApp) – exit without further cleanup.
    std::_Exit(ret);
}

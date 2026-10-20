// KettleLAN – program z interfejsem graficznym (Qt 6 / QML).
#include <QApplication>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml>

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

// Zapisuje dziennik jednocześnie na terminal i do pliku (Ustawienia → Otwórz dziennik).
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
    parser.setApplicationDescription(QStringLiteral("Wirtualny LAN przez sieć Steam"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimized(QStringLiteral("minimized"), QStringLiteral("Uruchom schowany w zasobniku."));
    parser.addOption(minimized);
    QCommandLineOption writeIcon(QStringLiteral("write-icon"), QStringLiteral("Zapisz ikonę programu do pliku PNG."),
                                 QStringLiteral("plik"));
    parser.addOption(writeIcon);
    parser.process(app);

    if (parser.isSet(writeIcon)) {
        const bool ok = kettle::gui::makeAppIcon(false).pixmap(256, 256).save(parser.value(writeIcon));
        return ok ? 0 : 1;
    }

    const bool demo = qEnvironmentVariableIsSet("KETTLELAN_DEMO");

    // Tylko jedna kopia programu – druga pokazuje okno pierwszej i kończy się.
    QLocalServer server;
    if (!demo) {
        QLocalSocket probe;
        probe.connectToServer(instanceName());
        if (probe.waitForConnected(300)) {
            probe.write("show\n");
            probe.waitForBytesWritten(300);
            std::puts("KettleLAN już działa – pokazuję jego okno.");
            return 0;
        }
        QLocalServer::removeServer(instanceName());
        server.listen(instanceName());
    }

    // Dziennik: terminal + plik.
    std::ofstream logFile(kettle::gui::logFilePath().toStdString(), std::ios::trunc);
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

    // Zrzut ekranu do dokumentacji: KETTLELAN_DEMO=1 KETTLELAN_SCREENSHOT=plik.png
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
    // Steam jest już rozłączony (Backend::quitApp) – kończymy bez dalszego sprzątania.
    std::_Exit(ret);
}

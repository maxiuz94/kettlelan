#include "backend.h"

#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QUrl>

#include "util.h"

namespace kettle::gui {

namespace {

QString statusName(EngineStatus s) {
    switch (s) {
    case EngineStatus::Starting: return QStringLiteral("starting");
    case EngineStatus::SteamMissing: return QStringLiteral("steamMissing");
    case EngineStatus::TunError: return QStringLiteral("tunError");
    case EngineStatus::Ready: return QStringLiteral("ready");
    }
    return QStringLiteral("starting");
}

QString netStateName(steam::State s) {
    switch (s) {
    case steam::State::Idle: return QStringLiteral("idle");
    case steam::State::Creating: return QStringLiteral("creating");
    case steam::State::Joining: return QStringLiteral("joining");
    case steam::State::Connected: return QStringLiteral("connected");
    }
    return QStringLiteral("idle");
}

QVariantMap peerToMap(const QString& name, const QString& address, bool self, bool owner, bool verified,
                      bool conflict, int pingMs, bool relayed) {
    QVariantMap m;
    m[QStringLiteral("name")] = name;
    m[QStringLiteral("address")] = address;
    m[QStringLiteral("self")] = self;
    m[QStringLiteral("owner")] = owner;
    m[QStringLiteral("verified")] = verified;
    m[QStringLiteral("conflict")] = conflict;
    m[QStringLiteral("pingMs")] = pingMs;
    m[QStringLiteral("relayed")] = relayed;
    QString initial;
    for (const QChar c : name)
        if (c.isLetterOrNumber()) {
            initial = c.toUpper();
            break;
        }
    m[QStringLiteral("initial")] = initial.isEmpty() ? QStringLiteral("?") : initial;
    return m;
}

#ifndef _WIN32
QString autostartFile() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           QStringLiteral("/autostart/kettlelan.desktop");
}
QString executablePath() {
    const QByteArray appImage = qgetenv("APPIMAGE"); // uruchomione z AppImage
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath() : QString::fromLocal8Bit(appImage);
}
#else
const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif

} // namespace

Backend::Backend(bool demo, QObject* parent) : QObject(parent), demo_(demo) {
    QSettings s;
    closeToTray_ = s.value(QStringLiteral("closeToTray"), true).toBool();
    notifications_ = s.value(QStringLiteral("notifications"), true).toBool();
    theme_ = s.value(QStringLiteral("theme"), QStringLiteral("system")).toString();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &Backend::themeChanged);
#endif
    connect(this, &Backend::settingsChanged, this, &Backend::themeChanged);

    setupTray();
    if (demo_) loadDemo();
}

Backend::~Backend() = default;

void Backend::attachWindow(QWindow* window) { window_ = window; }

void Backend::startEngine() {
    if (demo_) return;
    engine_ = std::make_unique<Engine>(
        [this](const Snapshot& snap) {
            QMetaObject::invokeMethod(this, [this, snap] { applySnapshot(snap); }, Qt::QueuedConnection);
        },
        [this](const std::string& text) {
            const QString t = QString::fromStdString(text);
            QMetaObject::invokeMethod(this, [this, t] { handleEvent(t); }, Qt::QueuedConnection);
        });
    engine_->start();
}

// ---------------------------------------------------------------- stan z silnika

void Backend::applySnapshot(const Snapshot& snap) {
    status_ = statusName(snap.status);
    errorText_ = QString::fromStdString(snap.error);
    personaName_ = QString::fromStdString(snap.personaName);
    selfAddress_ = QString::fromStdString(snap.selfAddress);
    netState_ = netStateName(snap.netState);
    networkName_ = QString::fromStdString(snap.networkName);
    networkCode_ = QString::fromStdString(snap.networkCode);

    peers_.clear();
    memberCount_ = 0;
    for (const auto& p : snap.peers) {
        peers_.append(peerToMap(QString::fromStdString(p.name), QString::fromStdString(p.address.toString()),
                                p.self, p.owner, p.verified, p.conflict, p.pingMs, p.relayed));
        if (p.self || p.verified) ++memberCount_;
    }
    emit changed();
    rebuildTrayMenu();
}

void Backend::handleEvent(const QString& text) {
    emit toast(text);
    // Powiadomienia systemowe tylko o osobach, i tylko gdy okno jest schowane.
    const bool aboutPeople = text.contains(QStringLiteral("dołączył")) || text.contains(QStringLiteral("opuścił"));
    const bool hidden = !window_ || !window_->isVisible();
    if (notifications_ && aboutPeople && hidden && tray_)
        tray_->showMessage(QStringLiteral("KettleLAN"), text, makeAppIcon(true), 4000);
}

// ---------------------------------------------------------------- akcje

void Backend::retry() {
    if (engine_) engine_->retry();
}

QString Backend::defaultNetworkName() const {
    return personaName_.isEmpty() ? QStringLiteral("Moja sieć") : QStringLiteral("Sieć %1").arg(personaName_);
}

void Backend::createNetwork(const QString& name) {
    const QString n = name.trimmed().isEmpty() ? defaultNetworkName() : name.trimmed().left(60);
    if (demo_) {
        networkName_ = n;
        netState_ = QStringLiteral("connected");
        emit changed();
        return;
    }
    if (engine_) engine_->create(n.toStdString());
}

bool Backend::isCodeValid(const QString& code) const { return netcode::parse(code.toStdString()).has_value(); }

void Backend::joinNetwork(const QString& code) {
    if (!isCodeValid(code)) {
        emit toast(QStringLiteral("To nie wygląda na kod sieci. Kod ma postać KTL-XXXX-XXXX-XXXX."));
        return;
    }
    if (engine_) engine_->join(code.toStdString());
}

void Backend::leaveNetwork() {
    if (demo_) {
        netState_ = QStringLiteral("idle");
        emit changed();
        return;
    }
    if (engine_) engine_->leave();
}

void Backend::copyText(const QString& text, const QString& message) {
    QGuiApplication::clipboard()->setText(text);
    emit toast(message);
}

void Backend::openUrl(const QString& url) { QDesktopServices::openUrl(QUrl(url)); }

void Backend::openLogs() { QDesktopServices::openUrl(QUrl::fromLocalFile(logFilePath())); }

void Backend::showWindow() {
    if (!window_) return;
    window_->show();
    window_->raise();
    window_->requestActivate();
}

void Backend::quitApp() {
    if (tray_) tray_->hide();
    if (window_) window_->hide();
    if (engine_) engine_->shutdown(); // opuszcza sieć i rozłącza Steam (maks. ~3 s)
    QCoreApplication::quit();
}

// ---------------------------------------------------------------- ustawienia

void Backend::setCloseToTray(bool v) {
    if (v == closeToTray_) return;
    closeToTray_ = v;
    QSettings().setValue(QStringLiteral("closeToTray"), v);
    emit settingsChanged();
}

void Backend::setNotifications(bool v) {
    if (v == notifications_) return;
    notifications_ = v;
    QSettings().setValue(QStringLiteral("notifications"), v);
    emit settingsChanged();
}

bool Backend::autostart() const {
#ifdef _WIN32
    return QSettings(QString::fromLatin1(kRunKey), QSettings::NativeFormat).contains(QStringLiteral("KettleLAN"));
#else
    return QFile::exists(autostartFile());
#endif
}

void Backend::setAutostart(bool v) {
#ifdef _WIN32
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (v)
        run.setValue(QStringLiteral("KettleLAN"),
                     QStringLiteral("\"%1\" --minimized").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else
        run.remove(QStringLiteral("KettleLAN"));
#else
    const QString path = autostartFile();
    if (v) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream out(&f);
            out << "[Desktop Entry]\n"
                   "Type=Application\n"
                   "Name=KettleLAN\n"
                   "Comment=Wirtualny LAN przez sieć Steam\n"
                   "Exec=\"" << executablePath() << "\" --minimized\n"
                   "Terminal=false\n"
                   "X-GNOME-Autostart-enabled=true\n";
        }
    } else {
        QFile::remove(path);
    }
#endif
    emit settingsChanged();
}

void Backend::setTheme(const QString& v) {
    if (v == theme_) return;
    theme_ = v;
    QSettings().setValue(QStringLiteral("theme"), v);
    emit settingsChanged();
}

bool Backend::darkMode() const {
    if (theme_ == QLatin1String("dark")) return true;
    if (theme_ == QLatin1String("light")) return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) return true;
    if (scheme == Qt::ColorScheme::Light) return false;
#endif
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

bool Backend::trayAvailable() const { return QSystemTrayIcon::isSystemTrayAvailable(); }

QString Backend::version() const { return QCoreApplication::applicationVersion(); }

QString Backend::interfaceName() const {
#ifdef _WIN32
    return QStringLiteral("KettleLAN (Wintun)");
#else
    return QStringLiteral("kettle0");
#endif
}

// ---------------------------------------------------------------- zasobnik

void Backend::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(makeAppIcon(false), this);
    tray_->setToolTip(QStringLiteral("KettleLAN"));
    trayMenu_ = new QMenu();
    tray_->setContextMenu(trayMenu_);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r != QSystemTrayIcon::Trigger || !window_) return;
        if (window_->isVisible()) window_->hide();
        else showWindow();
    });
    connect(this, &QObject::destroyed, trayMenu_, &QObject::deleteLater);
    rebuildTrayMenu();
    tray_->show();
}

void Backend::rebuildTrayMenu() {
    if (!tray_) return;
    const bool connected = netState_ == QLatin1String("connected");

    // Menu przebudowujemy tylko przy zmianie składu sieci (ping zmienia się co sekundę).
    QString sig = netState_ + networkName_ + networkCode_;
    for (const auto& v : peers_) {
        const auto m = v.toMap();
        sig += m.value(QStringLiteral("name")).toString() + m.value(QStringLiteral("address")).toString() +
               (m.value(QStringLiteral("verified")).toBool() ? QStringLiteral("1") : QStringLiteral("0"));
    }
    if (sig == trayMenuSignature_) return;
    trayMenuSignature_ = sig;

    tray_->setIcon(makeAppIcon(connected));
    tray_->setToolTip(connected ? QStringLiteral("KettleLAN – %1 (%2)").arg(networkName_, selfAddress_)
                                : QStringLiteral("KettleLAN"));

    trayMenu_->clear();
    if (connected) {
        auto* title = trayMenu_->addAction(QStringLiteral("%1 · %2").arg(networkName_, selfAddress_));
        title->setEnabled(false);
        trayMenu_->addSeparator();
        bool anyone = false;
        for (const auto& v : peers_) {
            const auto m = v.toMap();
            if (m.value(QStringLiteral("self")).toBool() || !m.value(QStringLiteral("verified")).toBool()) continue;
            anyone = true;
            const QString name = m.value(QStringLiteral("name")).toString();
            const QString addr = m.value(QStringLiteral("address")).toString();
            trayMenu_->addAction(QStringLiteral("Kopiuj IP: %1  (%2)").arg(name, addr), this, [this, name, addr] {
                copyText(addr, QStringLiteral("Skopiowano adres %1 (%2)").arg(addr, name));
            });
        }
        if (!anyone) trayMenu_->addAction(QStringLiteral("Nikt jeszcze nie dołączył"))->setEnabled(false);
        trayMenu_->addSeparator();
        trayMenu_->addAction(QStringLiteral("Kopiuj kod sieci"), this, [this] {
            copyText(networkCode_, QStringLiteral("Skopiowano kod sieci – wyślij go znajomym"));
        });
        trayMenu_->addAction(QStringLiteral("Opuść sieć"), this, [this] { leaveNetwork(); });
    }
    trayMenu_->addAction(QStringLiteral("Pokaż okno"), this, [this] { showWindow(); });
    trayMenu_->addSeparator();
    trayMenu_->addAction(QStringLiteral("Zakończ KettleLAN"), this, [this] { quitApp(); });
}

// ---------------------------------------------------------------- tryb pokazowy (do zrzutów ekranu)

void Backend::loadDemo() {
    demoPage_ = qEnvironmentVariable("KETTLELAN_DEMO_PAGE", QStringLiteral("start"));
    personaName_ = QStringLiteral("maxiuZ");
    selfAddress_ = QStringLiteral("100.80.46.79");
    if (demoPage_ == QLatin1String("steam")) {
        status_ = QStringLiteral("steamMissing");
        errorText_ = QStringLiteral("Steam nie jest uruchomiony. Włącz Steama, zaloguj się i spróbuj ponownie.");
    } else {
        status_ = QStringLiteral("ready");
    }
    if (demoPage_ == QLatin1String("network")) {
        netState_ = QStringLiteral("connected");
        networkName_ = QStringLiteral("Wieczór z Quake");
        networkCode_ = QStringLiteral("KTL-50AA-1SZ3-XRK3");
        peers_ = {
            peerToMap(QStringLiteral("maxiuZ"), QStringLiteral("100.80.46.79"), true, true, true, false, -1, false),
            peerToMap(QStringLiteral("Znajomy 1"), QStringLiteral("100.71.4.92"), false, false, true, false, 18, false),
            peerToMap(QStringLiteral("Znajomy 2"), QStringLiteral("100.98.230.7"), false, false, true, false, 34, false),
            peerToMap(QStringLiteral("Znajomy 3"), QStringLiteral("100.112.61.140"), false, false, true, false, 52, true),
            peerToMap(QStringLiteral("Znajomy 4"), QStringLiteral("100.77.9.31"), false, false, false, false, -1, false),
        };
        memberCount_ = 4;
    }
}

} // namespace kettle::gui

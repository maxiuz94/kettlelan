#include "backend.h"

#include <algorithm>

#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QPalette>
#include <QProcess>
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
    case steam::State::Connecting: return QStringLiteral("connecting");
    case steam::State::Connected: return QStringLiteral("connected");
    }
    return QStringLiteral("idle");
}

QString normalizeCode(const QString& code) {
    const auto c = netcode::parse(code.toStdString());
    return c ? QString::fromStdString(netcode::format(*c)) : QString();
}

// Text from other people in a Qt menu: "&" marks a keyboard shortcut there, so double it.
QString menuText(const QString& t) {
    QString out = t;
    return out.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

// The settings file holds network codes (they work like passwords) – owner only.
void restrictSettingsFile() {
    QSettings s;
    s.sync();
    QFile::setPermissions(s.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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
    const QByteArray appImage = qgetenv("APPIMAGE"); // started from an AppImage
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath() : QString::fromLocal8Bit(appImage);
}
#else
// KettleLAN runs as administrator on Windows, and Windows skips elevated programs in the "Run"
// registry key at logon. A Task Scheduler task with the highest privileges starts it instead.
constexpr const char* kTaskName = "KettleLAN";

bool runSchtasks(const QString& nativeArguments) {
    QProcess p;
    p.setProgram(QStringLiteral("schtasks.exe"));
    p.setNativeArguments(nativeArguments);
    p.start();
    return p.waitForFinished(10000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}
#endif

} // namespace

Backend::Backend(bool demo, QObject* parent) : QObject(parent), demo_(demo) {
    QSettings s;
    closeToTray_ = s.value(QStringLiteral("closeToTray"), true).toBool();
    notifications_ = s.value(QStringLiteral("notifications"), true).toBool();
    theme_ = s.value(QStringLiteral("theme"), QStringLiteral("system")).toString();
    autoConnect_ = s.value(QStringLiteral("autoConnect"), true).toBool();
    hideAddress_ = s.value(QStringLiteral("hideAddress"), true).toBool();
    protectServices_ = s.value(QStringLiteral("protectServices"), true).toBool();
    loadSaved();
    restrictSettingsFile();

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
        [this](const Message& message) {
            QMetaObject::invokeMethod(this, [this, message] { handleEvent(message); }, Qt::QueuedConnection);
        });
    engine_->setHideAddress(hideAddress_);
    engine_->setProtectServices(protectServices_);
    engine_->start();
}

// ---------------------------------------------------------------- state from the engine

void Backend::applySnapshot(const Snapshot& snap) {
    status_ = statusName(snap.status);
    const bool hasError = snap.status == EngineStatus::SteamMissing || snap.status == EngineStatus::TunError;
    errorText_ = hasError ? messageText(snap.error) : QString();
    personaName_ = QString::fromStdString(snap.personaName);
    selfAddress_ = QString::fromStdString(snap.selfAddress);
    netState_ = netStateName(snap.netState);
    networkName_ = QString::fromStdString(snap.networkName);
    networkCode_ = QString::fromStdString(snap.networkCode);
    blockedPackets_ = snap.blockedPackets;

    peers_.clear();
    memberCount_ = 0;
    for (const auto& p : snap.peers) {
        peers_.append(peerToMap(QString::fromStdString(p.name), QString::fromStdString(p.address.toString()),
                                p.self, p.owner, p.verified, p.conflict, p.pingMs, p.relayed));
        if (p.self || p.verified) ++memberCount_;
    }
    emit changed();

    if (netState_ == QLatin1String("connected") && !networkCode_.isEmpty()) {
        const QString sig = networkCode_ + networkName_;
        if (sig != lastSavedSignature_) {
            lastSavedSignature_ = sig;
            rememberNetwork(networkCode_, networkName_);
            QSettings().setValue(QStringLiteral("lastNetwork"), networkCode_);
        }
    } else {
        lastSavedSignature_.clear();
    }

    // Rejoin the last network automatically – once, right after connecting to Steam.
    if (status_ == QLatin1String("ready") && !autoConnectDone_) {
        autoConnectDone_ = true;
        const QString last = QSettings().value(QStringLiteral("lastNetwork")).toString();
        if (autoConnect_ && !last.isEmpty() && netState_ == QLatin1String("idle")) connectSaved(last);
    }
    rebuildTrayMenu();
}

QString Backend::messageText(const Message& message) const {
    QString text = QString::fromUtf8(messageTemplate(message.id));
    for (const auto& arg : message.args) text = text.arg(QString::fromStdString(arg));
    return text;
}

void Backend::handleEvent(const Message& message) {
    const QString text = messageText(message);
    if (message.id == Msg::UnsafeFolder) {
        // Lasting warning at the top of the window, and a notification when the window is hidden.
        warningText_ = text;
        emit changed();
        if (tray_ && (!window_ || !window_->isVisible())) tray_->showMessage(QStringLiteral("KettleLAN"), text, QSystemTrayIcon::Warning, 10000);
        return;
    }
    emit toast(text);
    // System notifications only about people, and only when the window is hidden.
    const bool hidden = !window_ || !window_->isVisible();
    if (notifications_ && isPeerEvent(message.id) && hidden && tray_)
#ifdef _WIN32
        tray_->showMessage(QStringLiteral("KettleLAN"), text, makeAppIcon(true), 4000); // shown as plain text
#else
        tray_->showMessage(QStringLiteral("KettleLAN"), text.toHtmlEscaped(), makeAppIcon(true), 4000); // may be HTML
#endif
}

// ---------------------------------------------------------------- actions

void Backend::retry() {
    if (engine_) engine_->retry();
}

QString Backend::defaultNetworkName() const {
    return personaName_.isEmpty() ? tr("My network") : tr("%1's network").arg(personaName_);
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
        emit toast(messageText({Msg::InvalidCode, {}}));
        return;
    }
    const QString normalized = normalizeCode(code);
    if (engine_) engine_->join(normalized.toStdString(), savedName(normalized).toStdString());
}

void Backend::connectSaved(const QString& code) { joinNetwork(code); }

void Backend::forgetNetwork(const QString& code) {
    const QString c = normalizeCode(code);
    for (int i = 0; i < saved_.size(); ++i) {
        if (saved_[i].toMap().value(QStringLiteral("code")).toString() == c) {
            const QString name = saved_[i].toMap().value(QStringLiteral("name")).toString();
            saved_.removeAt(i);
            storeSaved();
            if (QSettings().value(QStringLiteral("lastNetwork")).toString() == c)
                QSettings().remove(QStringLiteral("lastNetwork"));
            emit toast(tr("Removed “%1” from your networks").arg(name));
            return;
        }
    }
}

void Backend::leaveNetwork() {
    // Leaving on purpose: do not rejoin this network automatically on the next start.
    QSettings().remove(QStringLiteral("lastNetwork"));
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
    if (engine_) engine_->shutdown(); // leaves the network and disconnects Steam (~3 s at most)
    QCoreApplication::quit();
}

// ---------------------------------------------------------------- settings

void Backend::setCloseToTray(bool v) {
    if (v == closeToTray_) return;
    closeToTray_ = v;
    QSettings().setValue(QStringLiteral("closeToTray"), v);
    emit settingsChanged();
}

void Backend::setAutoConnect(bool v) {
    if (v == autoConnect_) return;
    autoConnect_ = v;
    QSettings().setValue(QStringLiteral("autoConnect"), v);
    emit settingsChanged();
}

void Backend::setHideAddress(bool v) {
    if (v == hideAddress_) return;
    hideAddress_ = v;
    QSettings().setValue(QStringLiteral("hideAddress"), v);
    if (engine_) engine_->setHideAddress(v);
    emit settingsChanged();
}

void Backend::setProtectServices(bool v) {
    if (v == protectServices_) return;
    protectServices_ = v;
    QSettings().setValue(QStringLiteral("protectServices"), v);
    if (engine_) engine_->setProtectServices(v);
    emit settingsChanged();
}

// ---------------------------------------------------------------- saved networks

void Backend::loadSaved() {
    saved_ = QSettings().value(QStringLiteral("networks")).toList();
}

void Backend::storeSaved() {
    std::sort(saved_.begin(), saved_.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("lastUsed")).toString() > b.toMap().value(QStringLiteral("lastUsed")).toString();
    });
    if (!demo_) {
        QSettings().setValue(QStringLiteral("networks"), saved_);
        restrictSettingsFile();
    }
    emit savedChanged();
}

QString Backend::savedName(const QString& code) const {
    for (const auto& v : saved_)
        if (v.toMap().value(QStringLiteral("code")).toString() == code) return v.toMap().value(QStringLiteral("name")).toString();
    return {};
}

void Backend::rememberNetwork(const QString& code, const QString& name) {
    QVariantMap entry;
    entry[QStringLiteral("code")] = code;
    entry[QStringLiteral("name")] = name.isEmpty() ? QStringLiteral("KettleLAN network") : name;
    entry[QStringLiteral("lastUsed")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (int i = 0; i < saved_.size(); ++i)
        if (saved_[i].toMap().value(QStringLiteral("code")).toString() == code) {
            saved_.removeAt(i);
            break;
        }
    saved_.prepend(entry);
    storeSaved();
}

void Backend::setNotifications(bool v) {
    if (v == notifications_) return;
    notifications_ = v;
    QSettings().setValue(QStringLiteral("notifications"), v);
    emit settingsChanged();
}

bool Backend::autostart() const {
#ifdef _WIN32
    return runSchtasks(QStringLiteral("/Query /TN %1").arg(QLatin1String(kTaskName)));
#else
    return QFile::exists(autostartFile());
#endif
}

void Backend::setAutostart(bool v) {
#ifdef _WIN32
    if (v) {
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        runSchtasks(QStringLiteral("/Create /F /TN %1 /SC ONLOGON /RL HIGHEST /TR \"\\\"%2\\\" --minimized\"")
                        .arg(QLatin1String(kTaskName), exe));
    } else {
        runSchtasks(QStringLiteral("/Delete /F /TN %1").arg(QLatin1String(kTaskName)));
    }
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
                   "Comment=Virtual LAN over the Steam network\n"
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

// ---------------------------------------------------------------- system tray

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

    // Rebuild the menu only when the members change (ping changes every second).
    QString sig = netState_ + networkName_ + networkCode_ + QString::number(saved_.size());
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
        auto* title = trayMenu_->addAction(menuText(QStringLiteral("%1 · %2").arg(networkName_, selfAddress_)));
        title->setEnabled(false);
        trayMenu_->addSeparator();
        bool anyone = false;
        for (const auto& v : peers_) {
            const auto m = v.toMap();
            if (m.value(QStringLiteral("self")).toBool() || !m.value(QStringLiteral("verified")).toBool()) continue;
            anyone = true;
            const QString name = m.value(QStringLiteral("name")).toString();
            const QString addr = m.value(QStringLiteral("address")).toString();
            trayMenu_->addAction(menuText(tr("Copy IP: %1  (%2)").arg(name, addr)), this, [this, name, addr] {
                copyText(addr, tr("Copied address %1 (%2)").arg(addr, name));
            });
        }
        if (!anyone) trayMenu_->addAction(tr("Nobody has joined yet"))->setEnabled(false);
        trayMenu_->addSeparator();
        trayMenu_->addAction(tr("Copy network code"), this, [this] {
            copyText(networkCode_, tr("Network code copied – send it to your friends"));
        });
        trayMenu_->addAction(tr("Leave network"), this, [this] { leaveNetwork(); });
    } else if (netState_ == QLatin1String("idle") && status_ == QLatin1String("ready") && !saved_.isEmpty()) {
        trayMenu_->addAction(tr("Your networks"))->setEnabled(false);
        for (const auto& v : saved_) {
            const auto m = v.toMap();
            const QString code = m.value(QStringLiteral("code")).toString();
            trayMenu_->addAction(menuText(tr("Connect: %1").arg(m.value(QStringLiteral("name")).toString())), this,
                                 [this, code] { connectSaved(code); });
        }
        trayMenu_->addSeparator();
    }
    trayMenu_->addAction(tr("Show window"), this, [this] { showWindow(); });
    trayMenu_->addSeparator();
    trayMenu_->addAction(tr("Quit KettleLAN"), this, [this] { quitApp(); });
}

// ---------------------------------------------------------------- demo mode (for screenshots)

void Backend::loadDemo() {
    if (qEnvironmentVariableIsSet("KETTLELAN_DEMO_WARNING"))
        warningText_ = messageText({Msg::UnsafeFolder, {"C:\\Users\\Player\\Desktop\\KettleLAN-0.2.1-windows-x64"}});
    demoPage_ = qEnvironmentVariable("KETTLELAN_DEMO_PAGE", QStringLiteral("start"));
    personaName_ = QStringLiteral("maxiuZ");
    selfAddress_ = QStringLiteral("100.80.46.79");
    if (demoPage_ == QLatin1String("steam")) {
        status_ = QStringLiteral("steamMissing");
        errorText_ = messageText({Msg::SteamNotRunning, {}});
    } else {
        status_ = QStringLiteral("ready");
    }
    auto demoEntry = [](const QString& name, const QString& code) {
        QVariantMap m;
        m[QStringLiteral("name")] = name;
        m[QStringLiteral("code")] = code;
        return QVariant(m);
    };
    saved_ = {demoEntry(tr("Quake night"), QStringLiteral("KTL-50AA-1SZ3-XRK3")),
              demoEntry(tr("Warcraft III with friends"), QStringLiteral("KTL-7F3Q-9XM2-A4KD"))};
    if (demoPage_ == QLatin1String("network")) {
        netState_ = QStringLiteral("connected");
        networkName_ = qEnvironmentVariable("KETTLELAN_DEMO_NAME", tr("Quake night"));
        networkCode_ = QStringLiteral("KTL-50AA-1SZ3-XRK3");
        peers_ = {
            peerToMap(QStringLiteral("maxiuZ"), QStringLiteral("100.80.46.79"), true, true, true, false, -1, false),
            peerToMap(tr("Friend %1").arg(1), QStringLiteral("100.71.4.92"), false, false, true, false, 18, false),
            peerToMap(tr("Friend %1").arg(2), QStringLiteral("100.98.230.7"), false, false, true, false, 34, false),
            peerToMap(tr("Friend %1").arg(3), QStringLiteral("100.112.61.140"), false, false, true, false, 52, true),
            peerToMap(tr("Friend %1").arg(4), QStringLiteral("100.77.9.31"), false, false, false, false, -1, false),
        };
        memberCount_ = 4;
    }
}

} // namespace kettle::gui

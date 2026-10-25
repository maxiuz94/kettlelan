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
    case EngineStatus::Offline: return QStringLiteral("offline");
    case EngineStatus::Starting: return QStringLiteral("starting");
    case EngineStatus::SteamMissing: return QStringLiteral("steamMissing");
    case EngineStatus::TunError: return QStringLiteral("tunError");
    case EngineStatus::Ready: return QStringLiteral("ready");
    }
    return QStringLiteral("starting");
}

QString normalizeCode(const QString& code) {
    const auto c = netcode::parse(code.toStdString());
    return c ? QString::fromStdString(netcode::format(*c)) : QString();
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
// Autostart: the user's "Run" registry key (KettleLAN runs as the user; the service has the rights).
const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
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
    engine_ = std::make_unique<EngineClient>([this](const Snapshot& snap) { applySnapshot(snap); },
                                             [this](const Message& message) { handleEvent(message); }, this);

    // The Steam process runs only while a network is on (otherwise Steam would show "Spacewar" for nothing).
    QSettings settings;
    const QString last = settings.value(QStringLiteral("lastNetwork")).toString(); // KettleLAN 0.2
    if (activeNetworks().isEmpty() && !last.isEmpty()) setActive(last, true);
    settings.remove(QStringLiteral("lastNetwork"));
    if (!autoConnect_) settings.remove(QStringLiteral("activeNetworks")); // start with every network off
    // ▶/⏹ is remembered; a first start is off (no "Spacewar" before anything is clicked), an update from a
    // version without the button keeps Steam on when a network was on
    steamEnabled_ = settings.value(QStringLiteral("steamEnabled"), !activeNetworks().isEmpty()).toBool();
    settings.setValue(QStringLiteral("steamEnabled"), steamEnabled_);
    if (steamEnabled_) {
        goOnline();
    } else {
        Snapshot offline;
        offline.status = EngineStatus::Offline;
        applySnapshot(offline);
    }
}

void Backend::goOnline() {
    if (engine_ && !engine_->running()) engine_->start(hideAddress_, protectServices_);
}

void Backend::goOffline() {
    if (engine_) engine_->stop(false);
}

void Backend::setSteamEnabled(bool on) {
    if (demo_ || on == steamEnabled_) return;
    steamEnabled_ = on;
    QSettings().setValue(QStringLiteral("steamEnabled"), on);
    // the network switches stay as they are: with Steam on again, the same networks connect
    if (on) goOnline();
    else {
        networkOpen_ = false;
        pendingOpen_.clear();
        goOffline();
    }
    rebuildNetworks();
    emit changed();
}

// ---------------------------------------------------------------- state from the engine

void Backend::applySnapshot(const Snapshot& snap) {
    const QString previous = status_;
    status_ = statusName(snap.status);
    const bool hasError = snap.status == EngineStatus::SteamMissing || snap.status == EngineStatus::TunError;
    errorText_ = hasError ? messageText(snap.error) : QString();
    personaName_ = QString::fromStdString(snap.personaName);
    // the address comes from the Steam account – remember it for the time KettleLAN is offline
    if (!snap.selfAddress.empty()) {
        selfAddress_ = QString::fromStdString(snap.selfAddress);
        QSettings().setValue(QStringLiteral("lastAddress"), selfAddress_);
    } else if (selfAddress_.isEmpty()) {
        selfAddress_ = QSettings().value(QStringLiteral("lastAddress")).toString();
    }
    blockedPackets_ = snap.blockedPackets;

    live_.clear();
    for (const auto& info : snap.networks) {
        LiveNetwork n;
        n.code = QString::fromStdString(info.code);
        n.lobbyName = QString::fromStdString(info.name);
        n.connected = info.state == steam::State::Connected;
        n.owner = info.owner;
        for (const auto& p : info.peers) {
            n.peers.append(peerToMap(QString::fromStdString(p.name), QString::fromStdString(p.address.toString()),
                                     p.self, p.owner, p.verified, p.conflict, p.pingMs, p.relayed));
            if (p.self || p.verified) ++n.members;
        }
        // The shared name from the lobby wins over what we saved; the alias stays ours.
        if (n.connected && !rememberedNow_.contains(n.code)) {
            rememberedNow_.insert(n.code);
            rememberNetwork(n.code, n.lobbyName.isEmpty() ? savedName(n.code) : n.lobbyName);
        } else if (n.connected && !n.lobbyName.isEmpty() && n.lobbyName != savedName(n.code)) {
            updateSavedName(n.code, n.lobbyName);
        }
        if (n.connected && n.code == pendingOpen_) {
            pendingOpen_.clear();
            openCode_ = n.code;
            networkOpen_ = true;
        }
        live_.push_back(std::move(n));
    }
    for (auto it = rememberedNow_.begin(); it != rememberedNow_.end();) {
        if (!liveFor(*it)) it = rememberedNow_.erase(it); // next connect updates "last used" again
        else ++it;
    }
    showOpenNetwork();
    rebuildNetworks();
    emit changed();

    // Connected to Steam: join the networks that are on (rejoined after a start, or just switched on).
    if (status_ == QLatin1String("ready") && previous != QLatin1String("ready")) {
        const QStringList active = activeNetworks();
        for (int i = 0; i < active.size() && i < maxNetworks(); ++i)
            if (engine_ && !liveFor(active[i])) engine_->join(active[i].toStdString(), savedName(active[i]).toStdString());
    }
    rebuildTrayMenu();
}

int Backend::connectedCount() const {
    int n = 0;
    for (const auto& v : networks_)
        if (v.toMap().value(QStringLiteral("state")).toString() != QLatin1String("off")) ++n;
    return std::max(n, int(live_.size()));
}

const Backend::LiveNetwork* Backend::liveFor(const QString& code) const {
    for (const auto& n : live_)
        if (n.code == code) return &n;
    return nullptr;
}

// Fills the properties of the network screen from the open network.
void Backend::showOpenNetwork() {
    const LiveNetwork* n = networkOpen_ ? liveFor(openCode_) : nullptr;
    if (networkOpen_ && (!n || !n->connected)) networkOpen_ = false; // left or lost – back to the home screen
    if (n && n->connected) {
        netState_ = QStringLiteral("connected");
        networkCode_ = n->code;
        networkName_ = n->lobbyName.isEmpty() ? savedName(n->code) : n->lobbyName;
        networkAlias_ = savedAlias(n->code);
        networkOwner_ = n->owner;
        peers_ = n->peers;
        memberCount_ = n->members;
    } else {
        netState_ = QStringLiteral("idle");
        networkCode_.clear();
        networkName_.clear();
        networkAlias_.clear();
        networkOwner_ = false;
        peers_.clear();
        memberCount_ = 0;
    }
}

// Home screen: connected and connecting networks first (in joining order), then the other saved ones.
void Backend::rebuildNetworks() {
    networks_.clear();
    // name: the shared name ("" = not known yet), alias: the user's own note on this computer
    auto entry = [this](const QString& code, const QString& name, const QString& state, int members, bool owner) {
        QVariantMap m;
        m[QStringLiteral("code")] = code;
        m[QStringLiteral("name")] = name;
        m[QStringLiteral("alias")] = savedAlias(code);
        m[QStringLiteral("state")] = state;
        m[QStringLiteral("members")] = members;
        m[QStringLiteral("owner")] = owner;
        return QVariant(m);
    };
    for (const auto& n : live_)
        networks_.append(entry(n.code, n.lobbyName.isEmpty() ? savedName(n.code) : n.lobbyName,
                               n.connected ? QStringLiteral("connected") : QStringLiteral("connecting"), n.members, n.owner));
    // switched on, but the Steam process is still connecting: show them as connecting right away
    const QStringList active = activeNetworks();
    for (const auto& v : saved_) {
        const auto m = v.toMap();
        const QString code = m.value(QStringLiteral("code")).toString();
        if (liveFor(code)) continue;
        const QString state = !active.contains(code) ? QStringLiteral("off")
                            : !steamEnabled_ ? QStringLiteral("waiting") // on, but Steam is off (▶)
                                             : QStringLiteral("connecting");
        networks_.append(entry(code, m.value(QStringLiteral("name")).toString(), state, 0, false));
    }
}

QString Backend::messageText(const Message& message) const {
    QString text = QString::fromUtf8(messageTemplate(message.id));
    for (const auto& arg : message.args) text = text.arg(QString::fromStdString(arg));
    return text;
}

void Backend::handleEvent(const Message& message) {
    if (isNetworkStateEvent(message.id)) return; // connecting, connected, left: the list shows it
    inform(messageText(message), !isPeerEvent(message.id));
}

void Backend::inform(const QString& text, bool important) {
    if (notifications_ && tray_ && QSystemTrayIcon::supportsMessages()) {
#ifdef _WIN32
        tray_->showMessage(QStringLiteral("KettleLAN"), text, makeAppIcon(true), 5000); // shown as plain text
#else
        tray_->showMessage(QStringLiteral("KettleLAN"), text.toHtmlEscaped(), makeAppIcon(true), 5000); // may be HTML
#endif
    } else if (important) {
        emit toast(text);
    }
}

// ---------------------------------------------------------------- actions

void Backend::retry() {
    if (!engine_) return;
    if (engine_->running()) engine_->retry();
    else goOnline();
}

QString Backend::defaultNetworkName() const {
    return personaName_.isEmpty() ? tr("My network") : tr("%1's network").arg(personaName_);
}

void Backend::createNetwork(const QString& name) {
    const QString n = name.trimmed().isEmpty() ? defaultNetworkName() : name.trimmed().left(60);
    const QString code = QString::fromStdString(netcode::format(netcode::generate()));
    rememberNetwork(code, n); // on the list right away, with the name the user chose
    joinNetwork(code);
}

bool Backend::isCodeValid(const QString& code) const { return netcode::parse(code.toStdString()).has_value(); }

void Backend::joinNetwork(const QString& code) {
    if (!isCodeValid(code)) {
        inform(messageText({Msg::InvalidCode, {}}), true);
        return;
    }
    const QString normalized = normalizeCode(code);
    if (liveFor(normalized)) { // already in it – just show it
        openNetwork(normalized);
        return;
    }
    if (connectedCount() >= maxNetworks()) {
        inform(messageText({Msg::NetworkLimit, {std::to_string(maxNetworks())}}), true);
        return;
    }
    setActive(normalized, true);
    pendingOpen_ = normalized;
    if (demo_) return;
    if (status_ != QLatin1String("ready")) {
        // "Create" and "Join" ask for a connection: switch Steam on; the network is joined once it is ready
        if (!steamEnabled_) setSteamEnabled(true);
        else goOnline();
        emit changed();
        return;
    }
    if (engine_) engine_->join(normalized.toStdString(), savedName(normalized).toStdString());
}

void Backend::connectSaved(const QString& code) { joinNetwork(code); }

void Backend::openNetwork(const QString& code) {
    const LiveNetwork* n = liveFor(normalizeCode(code));
    if (!n || !n->connected) return;
    openCode_ = n->code;
    networkOpen_ = true;
    showOpenNetwork();
    emit changed();
}

void Backend::closeNetwork() {
    networkOpen_ = false;
    showOpenNetwork();
    emit changed();
}

void Backend::setNetworkEnabled(const QString& code, bool on) {
    if (on && !steamEnabled_ && !demo_) {
        // Steam is off: only remember the choice – the network connects with ▶
        if (connectedCount() >= maxNetworks()) {
            inform(messageText({Msg::NetworkLimit, {std::to_string(maxNetworks())}}), true);
            return;
        }
        setActive(normalizeCode(code), true);
        rebuildNetworks();
        emit changed();
        return;
    }
    if (on) {
        joinNetwork(code);
        pendingOpen_.clear(); // switching on from the list keeps the list on screen
    } else {
        disconnectNetwork(normalizeCode(code));
    }
}

// Leaving on purpose: do not rejoin this network automatically on the next start.
void Backend::disconnectNetwork(const QString& code) {
    setActive(code, false);
    if (pendingOpen_ == code) pendingOpen_.clear();
    if (demo_) {
        live_.erase(std::remove_if(live_.begin(), live_.end(), [&](const LiveNetwork& n) { return n.code == code; }), live_.end());
        showOpenNetwork();
        rebuildNetworks();
        emit changed();
        return;
    }
    if (engine_) engine_->leave(code.toStdString());
}

QStringList Backend::activeNetworks() const {
    return QSettings().value(QStringLiteral("activeNetworks")).toStringList();
}

void Backend::setActive(const QString& code, bool active) {
    if (demo_) return;
    QStringList list = activeNetworks();
    list.removeAll(code);
    if (active) list.append(code);
    QSettings().setValue(QStringLiteral("activeNetworks"), list);
}

void Backend::forgetNetwork(const QString& code) {
    const QString c = normalizeCode(code);
    if (liveFor(c)) disconnectNetwork(c);
    setActive(c, false);
    for (int i = 0; i < saved_.size(); ++i) {
        if (saved_[i].toMap().value(QStringLiteral("code")).toString() == c) {
            const QString name = saved_[i].toMap().value(QStringLiteral("name")).toString();
            saved_.removeAt(i);
            storeSaved();
            rebuildNetworks();
            emit changed();
            inform(tr("Removed “%1” from your networks").arg(name), false);
            return;
        }
    }
}

void Backend::leaveNetwork() {
    if (!openCode_.isEmpty()) disconnectNetwork(openCode_);
    networkOpen_ = false;
    showOpenNetwork();
    emit changed();
}

// The confirmation is shown where the user clicked (a ✓ or "Copied"), not as a message.
void Backend::copyText(const QString& text, const QString&) { QGuiApplication::clipboard()->setText(text); }

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
    if (engine_) engine_->stop(true); // leaves the networks and ends the Steam process (~3 s at most)
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

QString Backend::savedAlias(const QString& code) const {
    for (const auto& v : saved_)
        if (v.toMap().value(QStringLiteral("code")).toString() == code) return v.toMap().value(QStringLiteral("alias")).toString();
    return {};
}

void Backend::updateSavedName(const QString& code, const QString& name) {
    for (auto& v : saved_) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("code")).toString() != code) continue;
        m[QStringLiteral("name")] = name;
        v = m;
        storeSaved();
        return;
    }
}

void Backend::setAlias(const QString& code, const QString& alias) {
    const QString c = normalizeCode(code);
    const QString clean = alias.trimmed().left(60);
    for (auto& v : saved_) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("code")).toString() != c) continue;
        if (clean.isEmpty()) m.remove(QStringLiteral("alias"));
        else m[QStringLiteral("alias")] = clean;
        v = m;
        storeSaved();
        showOpenNetwork();
        rebuildNetworks();
        trayMenuSignature_.clear();
        rebuildTrayMenu();
        emit changed();
        return;
    }
}

void Backend::renameNetwork(const QString& code, const QString& name) {
    const QString c = normalizeCode(code);
    const QString clean = name.trimmed().left(60);
    const LiveNetwork* n = liveFor(c);
    if (clean.isEmpty() || !n || !n->owner) return;
    if (engine_) engine_->rename(c.toStdString(), clean.toStdString());
    updateSavedName(c, clean);
    inform(tr("Renamed the network to “%1” for everyone").arg(clean), false);
}

void Backend::rememberNetwork(const QString& code, const QString& name) {
    QVariantMap entry;
    entry[QStringLiteral("code")] = code;
    entry[QStringLiteral("name")] = name == QLatin1String("KettleLAN network") ? QString() : name; // old default = not known
    const QString alias = savedAlias(code);
    if (!alias.isEmpty()) entry[QStringLiteral("alias")] = alias;
    entry[QStringLiteral("lastUsed")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (int i = 0; i < saved_.size(); ++i)
        if (saved_[i].toMap().value(QStringLiteral("code")).toString() == code) {
            saved_.removeAt(i);
            break;
        }
    saved_.prepend(entry);
    // at most kMaxSaved saved networks: forget the least recently used one that is not in use
    constexpr int kMaxSaved = 20;
    for (int i = int(saved_.size()) - 1; saved_.size() > kMaxSaved && i > 0; --i) {
        const QString c = saved_[i].toMap().value(QStringLiteral("code")).toString();
        if (!liveFor(c) && !activeNetworks().contains(c)) saved_.removeAt(i);
    }
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

// The tray menu stays short: everything else is in the window.
void Backend::rebuildTrayMenu() {
    if (!tray_) return;
    int connected = 0;
    for (const auto& n : live_)
        if (n.connected) ++connected;
    const QString sig = QString::number(connected) + selfAddress_ + status_;
    if (sig == trayMenuSignature_) return;
    trayMenuSignature_ = sig;

    tray_->setIcon(makeAppIcon(connected > 0));
    tray_->setToolTip(status_ == QLatin1String("offline") ? tr("KettleLAN – not connected to Steam")
                      : connected == 0 ? QStringLiteral("KettleLAN")
                      : connected == 1 ? tr("KettleLAN – %1 · 1 network").arg(selfAddress_)
                                       : tr("KettleLAN – %1 · %2 networks").arg(selfAddress_).arg(connected));
    if (!trayMenu_->isEmpty()) return; // the two actions never change
    trayMenu_->addAction(tr("Show window"), this, [this] { showWindow(); });
    trayMenu_->addAction(tr("Quit KettleLAN"), this, [this] { quitApp(); });
}

// ---------------------------------------------------------------- demo mode (for screenshots)

void Backend::loadDemo() {
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
    QVariantMap warcraftSaved = demoEntry(tr("Warcraft III with friends"), QStringLiteral("KTL-7F3Q-9XM2-A4KD")).toMap();
    warcraftSaved[QStringLiteral("alias")] = tr("the Thursday group");
    saved_ = {demoEntry(tr("Quake night"), QStringLiteral("KTL-50AA-1SZ3-XRK3")),
              QVariant(warcraftSaved),
              demoEntry(tr("CS 1.6 Thursday"), QStringLiteral("KTL-9WQ2-H4T7-ZK3M")),
              demoEntry(tr("Old LAN party"), QStringLiteral("KTL-3M8K-QA2W-7VDP"))};
    if (demoPage_ == QLatin1String("network") || demoPage_ == QLatin1String("start") || demoPage_ == QLatin1String("details")) {
        LiveNetwork quake;
        quake.code = QStringLiteral("KTL-50AA-1SZ3-XRK3");
        quake.lobbyName = tr("Quake night");
        quake.connected = true;
        quake.owner = true;
        quake.peers = {
            peerToMap(QStringLiteral("maxiuZ"), QStringLiteral("100.80.46.79"), true, true, true, false, -1, false),
            peerToMap(tr("Friend %1").arg(1), QStringLiteral("100.71.4.92"), false, false, true, false, 18, false),
            peerToMap(tr("Friend %1").arg(2), QStringLiteral("100.98.230.7"), false, false, true, false, 34, false),
            peerToMap(tr("Friend %1").arg(3), QStringLiteral("100.112.61.140"), false, false, true, false, 52, true),
            peerToMap(tr("Friend %1").arg(4), QStringLiteral("100.77.9.31"), false, false, false, false, -1, false),
        };
        quake.members = 4;
        LiveNetwork warcraft;
        warcraft.code = QStringLiteral("KTL-7F3Q-9XM2-A4KD");
        warcraft.lobbyName = tr("Warcraft III with friends");
        warcraft.connected = true;
        warcraft.peers = {
            peerToMap(QStringLiteral("maxiuZ"), QStringLiteral("100.80.46.79"), true, false, true, false, -1, false),
            peerToMap(tr("Friend %1").arg(5), QStringLiteral("100.90.18.204"), false, true, true, false, 27, true),
        };
        warcraft.members = 2;
        live_ = {quake, warcraft};
        if (demoPage_ == QLatin1String("network") || demoPage_ == QLatin1String("details")) {
            openCode_ = quake.code;
            networkOpen_ = true;
        }
    }
    steamEnabled_ = true; // the screenshots show a connected KettleLAN
    showOpenNetwork();
    rebuildNetworks();
}

} // namespace kettle::gui

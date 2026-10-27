// KettleLAN – bridge between the QML interface and the network engine.
#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QString>
#include <QVariantList>
#include <QWindow>
#include <memory>

#include "engine.h"
#include "engine_client.h"
#include "network.h"

class QSystemTrayIcon;
class QMenu;

namespace kettle::gui {

class Backend : public QObject {
    Q_OBJECT
    // program state
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString personaName READ personaName NOTIFY changed)
    Q_PROPERTY(QString selfAddress READ selfAddress NOTIFY changed)
    Q_PROPERTY(QString netState READ netState NOTIFY changed)
    Q_PROPERTY(QString networkName READ networkName NOTIFY changed)
    Q_PROPERTY(QString networkCode READ networkCode NOTIFY changed)
    Q_PROPERTY(QVariantList peers READ peers NOTIFY changed)
    Q_PROPERTY(int memberCount READ memberCount NOTIFY changed)
    Q_PROPERTY(QVariantList savedNetworks READ savedNetworks NOTIFY savedChanged)
    // home screen: saved networks with their current state (connected first)
    Q_PROPERTY(QVariantList networks READ networks NOTIFY changed)
    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY changed)
    Q_PROPERTY(int maxNetworks READ maxNetworks CONSTANT)
    // the network shown on the network screen (networkName, networkCode, peers… describe it)
    Q_PROPERTY(bool networkOpen READ networkOpen NOTIFY changed)
    // ▶/⏹: whether KettleLAN connects to Steam (Steam shows "Spacewar" while it does). Remembered.
    Q_PROPERTY(bool steamEnabled READ steamEnabled WRITE setSteamEnabled NOTIFY changed)
    Q_PROPERTY(QString networkAlias READ networkAlias NOTIFY changed) // your own note for the open network
    Q_PROPERTY(bool networkOwner READ networkOwner NOTIFY changed)    // you opened it: you can rename it
    // settings
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY settingsChanged)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY settingsChanged)
    // your name in every network; "" = your Steam name
    Q_PROPERTY(QString nickname READ nickname WRITE setNickname NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList blockedPeople READ blockedPeople NOTIFY settingsChanged) // [{steamId, name}]
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY settingsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(bool hideAddress READ hideAddress WRITE setHideAddress NOTIFY settingsChanged)
    Q_PROPERTY(bool protectServices READ protectServices WRITE setProtectServices NOTIFY settingsChanged)
    Q_PROPERTY(QString blockedCount READ blockedCount NOTIFY changed)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY settingsChanged)
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY themeChanged)
    // information
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString interfaceName READ interfaceName CONSTANT)
    Q_PROPERTY(QString demoPage READ demoPage CONSTANT)

public:
    explicit Backend(bool demo, QObject* parent = nullptr);
    ~Backend() override;

    void attachWindow(QWindow* window);
    void startEngine();

    QString status() const { return status_; }
    QString errorText() const { return errorText_; }
    QString personaName() const { return personaName_; }
    QString selfAddress() const { return selfAddress_; }
    QString netState() const { return netState_; }
    QString networkName() const { return networkName_; }
    QString networkCode() const { return networkCode_; }
    QVariantList peers() const { return peers_; }
    int memberCount() const { return memberCount_; }
    QVariantList networks() const { return networks_; }
    int connectedCount() const; // connected and connecting networks
    int maxNetworks() const { return int(steam::Network::kMaxNetworks); }
    bool networkOpen() const { return networkOpen_; }
    bool steamEnabled() const { return steamEnabled_; }
    void setSteamEnabled(bool on);
    QString networkAlias() const { return networkAlias_; }
    bool networkOwner() const { return networkOwner_; }
    QVariantList savedNetworks() const { return saved_; }

    bool closeToTray() const { return closeToTray_; }
    void setCloseToTray(bool v);
    bool notifications() const { return notifications_; }
    QString nickname() const { return nickname_; }
    void setNickname(const QString& v);
    QVariantList blockedPeople() const;
    // A message as a system notification (when on and supported); an important one falls back to
    // a short message in the window, so an error is never lost.
    void inform(const QString& text, bool important);
    void setNotifications(bool v);
    bool autostart() const;
    void setAutostart(bool v);
    bool autoConnect() const { return autoConnect_; }
    void setAutoConnect(bool v);
    bool hideAddress() const { return hideAddress_; }
    void setHideAddress(bool v);
    bool protectServices() const { return protectServices_; }
    void setProtectServices(bool v);
    QString blockedCount() const { return QString::number(blockedPackets_); }
    QString theme() const { return theme_; }
    void setTheme(const QString& v);
    bool darkMode() const;

    bool trayAvailable() const;
    QString version() const;
    QString interfaceName() const;
    QString demoPage() const { return demoPage_; }

    Q_INVOKABLE void retry();
    Q_INVOKABLE void createNetwork(const QString& name);
    Q_INVOKABLE bool isCodeValid(const QString& code) const;
    Q_INVOKABLE void joinNetwork(const QString& code);
    Q_INVOKABLE void leaveNetwork(); // the open network
    Q_INVOKABLE void openNetwork(const QString& code);
    Q_INVOKABLE void closeNetwork();
    Q_INVOKABLE void setNetworkEnabled(const QString& code, bool on);

    Q_INVOKABLE void setAlias(const QString& code, const QString& alias); // only on this computer
    Q_INVOKABLE void renameNetwork(const QString& code, const QString& name); // for everyone (owner only)
    Q_INVOKABLE void connectSaved(const QString& code);
    Q_INVOKABLE void forgetNetwork(const QString& code);
    Q_INVOKABLE void copyText(const QString& text, const QString& message);
    // Blocking (on this computer only, in every network): nothing is exchanged with that person.
    Q_INVOKABLE void blockPerson(const QString& steamId, const QString& name);
    Q_INVOKABLE void unblockPerson(const QString& steamId);
    Q_INVOKABLE void openUrl(const QString& url);
    Q_INVOKABLE void openLogs();
    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void quitApp();
    Q_INVOKABLE QString defaultNetworkName() const;

signals:
    void changed();
    void settingsChanged();
    void savedChanged();
    void themeChanged();
    void toast(const QString& text);

private:
    void applySnapshot(const Snapshot& snap);
    void handleEvent(const Message& message);
    QString messageText(const Message& message) const;
    void setupTray();
    void rebuildTrayMenu();
    void loadDemo();
    void loadSaved();
    void storeSaved();
    void rememberNetwork(const QString& code, const QString& name);
    QString savedName(const QString& code) const;

    bool demo_ = false;
    QString demoPage_;
    std::unique_ptr<EngineClient> engine_; // the Steam process (kettlelan-steam)
    QPointer<QWindow> window_;
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    QString trayMenuSignature_;

    QString status_ = QStringLiteral("starting");
    QString errorText_;
    QString personaName_;
    QString selfAddress_;
    QString netState_ = QStringLiteral("idle");
    QString networkName_;
    QString networkCode_;
    QVariantList peers_;
    int memberCount_ = 0;

    QVariantList saved_;
    struct LiveNetwork {
        QString code;
        QString lobbyName;
        bool connected = false;
        bool owner = false;
        QVariantList peers;
        int members = 0;
    };
    std::vector<LiveNetwork> live_;  // from the engine: connecting and connected networks
    QVariantList networks_;          // home screen model
    QString openCode_;
    QString networkAlias_;
    bool networkOwner_ = false;
    QString savedAlias(const QString& code) const;
    void updateSavedName(const QString& code, const QString& name);
    bool networkOpen_ = false;
    QString pendingOpen_;            // open this network once it is connected
    QSet<QString> rememberedNow_;    // saved/updated in this session (once per connect)
    bool autoConnectDone_ = false;
    void disconnectNetwork(const QString& code);
    void goOnline();  // starts the Steam process
    void goOffline(); // ends it – Steam stops showing "Spacewar"
    bool steamEnabled_ = false;
    void rebuildNetworks();
    void showOpenNetwork();
    QStringList activeNetworks() const;
    void setActive(const QString& code, bool active);
    const LiveNetwork* liveFor(const QString& code) const;

    bool closeToTray_ = true;
    bool autoConnect_ = true;
    bool hideAddress_ = true;
    bool protectServices_ = true;
    qulonglong blockedPackets_ = 0;
    bool notifications_ = true;
    QString nickname_;
    QVariantMap blocked_; // SteamID (text) → the name we saw when blocking
    void sendBlocked();

    QString theme_ = QStringLiteral("system");

};

} // namespace kettle::gui

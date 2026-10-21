// KettleLAN – bridge between the QML interface and the network engine.
#pragma once

#include <QObject>
#include <QPointer>
#include <QTranslator>
#include <QString>
#include <QVariantList>
#include <QWindow>
#include <memory>

#include "engine.h"

class QSystemTrayIcon;
class QMenu;
class QQmlEngine;

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
    // settings
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY settingsChanged)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY settingsChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY settingsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(bool hideAddress READ hideAddress WRITE setHideAddress NOTIFY settingsChanged)
    Q_PROPERTY(bool protectServices READ protectServices WRITE setProtectServices NOTIFY settingsChanged)
    Q_PROPERTY(QString blockedCount READ blockedCount NOTIFY changed)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY settingsChanged)
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY themeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool askLanguage READ askLanguage NOTIFY languageChanged)
    // information
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString interfaceName READ interfaceName CONSTANT)
    Q_PROPERTY(QString demoPage READ demoPage CONSTANT)

public:
    explicit Backend(bool demo, QObject* parent = nullptr);
    ~Backend() override;

    void attachWindow(QWindow* window);
    void attachEngine(QQmlEngine* engine) { qmlEngine_ = engine; }
    // Loads the translation chosen in the settings (before the QML is loaded).
    void applyInitialLanguage();
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
    QVariantList savedNetworks() const { return saved_; }

    bool closeToTray() const { return closeToTray_; }
    void setCloseToTray(bool v);
    bool notifications() const { return notifications_; }
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
    QString language() const { return language_; }
    void setLanguage(const QString& language);
    bool askLanguage() const { return askLanguage_; }

    bool trayAvailable() const;
    QString version() const;
    QString interfaceName() const;
    QString demoPage() const { return demoPage_; }

    Q_INVOKABLE void retry();
    Q_INVOKABLE void createNetwork(const QString& name);
    Q_INVOKABLE bool isCodeValid(const QString& code) const;
    Q_INVOKABLE void joinNetwork(const QString& code);
    Q_INVOKABLE void leaveNetwork();
    Q_INVOKABLE void connectSaved(const QString& code);
    Q_INVOKABLE void forgetNetwork(const QString& code);
    Q_INVOKABLE void copyText(const QString& text, const QString& message);
    Q_INVOKABLE void openUrl(const QString& url);
    Q_INVOKABLE void openLogs();
    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void quitApp();
    Q_INVOKABLE QString defaultNetworkName() const;
    Q_INVOKABLE void keepEnglish();

signals:
    void changed();
    void settingsChanged();
    void savedChanged();
    void themeChanged();
    void languageChanged();
    void toast(const QString& text);

private:
    void applySnapshot(const Snapshot& snap);
    void handleEvent(const Message& message);
    QString translate(const Message& message) const;
    void loadTranslator(const QString& language);
    void setupTray();
    void rebuildTrayMenu();
    void loadDemo();
    void loadSaved();
    void storeSaved();
    void rememberNetwork(const QString& code, const QString& name);
    QString savedName(const QString& code) const;

    bool demo_ = false;
    QString demoPage_;
    std::unique_ptr<Engine> engine_;
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
    QString lastSavedSignature_;
    bool autoConnectDone_ = false;

    bool closeToTray_ = true;
    bool autoConnect_ = true;
    bool hideAddress_ = true;
    bool protectServices_ = true;
    qulonglong blockedPackets_ = 0;
    bool notifications_ = true;
    QString theme_ = QStringLiteral("system");
    QString language_ = QStringLiteral("en");
    bool askLanguage_ = false;
    QTranslator translator_;
    QQmlEngine* qmlEngine_ = nullptr;
    Message errorMessage_;
    bool hasError_ = false;
};

} // namespace kettle::gui

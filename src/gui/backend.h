// KettleLAN – most między interfejsem QML a silnikiem sieci.
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QWindow>
#include <memory>

#include "engine.h"

class QSystemTrayIcon;
class QMenu;

namespace kettle::gui {

class Backend : public QObject {
    Q_OBJECT
    // stan programu
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString personaName READ personaName NOTIFY changed)
    Q_PROPERTY(QString selfAddress READ selfAddress NOTIFY changed)
    Q_PROPERTY(QString netState READ netState NOTIFY changed)
    Q_PROPERTY(QString networkName READ networkName NOTIFY changed)
    Q_PROPERTY(QString networkCode READ networkCode NOTIFY changed)
    Q_PROPERTY(QVariantList peers READ peers NOTIFY changed)
    Q_PROPERTY(int memberCount READ memberCount NOTIFY changed)
    // ustawienia
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY settingsChanged)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY settingsChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY settingsChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY settingsChanged)
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY themeChanged)
    // informacje
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

    bool closeToTray() const { return closeToTray_; }
    void setCloseToTray(bool v);
    bool notifications() const { return notifications_; }
    void setNotifications(bool v);
    bool autostart() const;
    void setAutostart(bool v);
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
    Q_INVOKABLE void leaveNetwork();
    Q_INVOKABLE void copyText(const QString& text, const QString& message);
    Q_INVOKABLE void openUrl(const QString& url);
    Q_INVOKABLE void openLogs();
    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void quitApp();
    Q_INVOKABLE QString defaultNetworkName() const;

signals:
    void changed();
    void settingsChanged();
    void themeChanged();
    void toast(const QString& text);

private:
    void applySnapshot(const Snapshot& snap);
    void handleEvent(const QString& text);
    void setupTray();
    void rebuildTrayMenu();
    void loadDemo();

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

    bool closeToTray_ = true;
    bool notifications_ = true;
    QString theme_ = QStringLiteral("system");
};

} // namespace kettle::gui

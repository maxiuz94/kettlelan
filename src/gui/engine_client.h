// KettleLAN – the window's side of the Steam process (kettlelan-steam): starts and stops it, sends it
// commands and receives its state. Same commands as Engine, which runs inside that process.
#pragma once

#include <QObject>
#include <QProcess>
#include <functional>

#include "engine.h"

namespace kettle::gui {

class EngineClient : public QObject {
    Q_OBJECT
public:
    using SnapshotHandler = std::function<void(const Snapshot&)>;
    using EventHandler = std::function<void(const Message&)>;

    EngineClient(SnapshotHandler onSnapshot, EventHandler onEvent, QObject* parent = nullptr);
    ~EngineClient() override;

    bool running() const { return process_.state() != QProcess::NotRunning; }
    void start(bool hideAddress, bool protectServices); // starts the Steam process (connects to Steam)
    // Leaves the networks and ends it (Steam: no "Spacewar"). wait: block until it has ended (when quitting).
    void stop(bool wait = true);

    void join(const std::string& code, const std::string& nameHint);
    void leave(const std::string& code);
    void leaveAll();
    void rename(const std::string& code, const std::string& name);
    void setNick(const std::string& nick); // also sent again whenever the Steam process starts
    void setHideAddress(bool on);
    void setProtectServices(bool on);
    void retry();

private:
    void send(const QJsonObject& command);
    void readOutput();
    void readLog();

    SnapshotHandler onSnapshot_;
    EventHandler onEvent_;
    QProcess process_;
    QByteArray outBuf_;
    QByteArray logBuf_;
    bool stopping_ = false;
    bool wanted_ = false;
    bool hideAddress_ = true;
    bool protectServices_ = true;
    std::string nick_;
};

} // namespace kettle::gui

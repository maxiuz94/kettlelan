#include "engine_client.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QTimer>
#include <iostream>

#include "engine_ipc.h"
#include "log.h"

namespace kettle::gui {

namespace {
QString workerPath() {
#ifdef _WIN32
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("kettlelan-steam.exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("kettlelan-steam"));
#endif
}
} // namespace

EngineClient::EngineClient(SnapshotHandler onSnapshot, EventHandler onEvent, QObject* parent)
    : QObject(parent), onSnapshot_(std::move(onSnapshot)), onEvent_(std::move(onEvent)) {
    process_.setProgram(workerPath());
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &EngineClient::readOutput);
    connect(&process_, &QProcess::readyReadStandardError, this, &EngineClient::readLog);
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readOutput();
        readLog();
        Snapshot offline;
        offline.status = EngineStatus::Offline;
        if (stopping_ || !wanted_) {
            onSnapshot_(offline);
            return;
        }
        // ended unexpectedly while a network is on: report it and start again shortly
        KLOG_WARN("The Steam process ended unexpectedly (" << (status == QProcess::CrashExit ? "crash" : "exit code " + std::to_string(code))
                  << ") – starting it again");
        Snapshot restarting;
        restarting.status = EngineStatus::Starting;
        onSnapshot_(restarting);
        QTimer::singleShot(2000, this, [this] {
            if (wanted_ && !running()) start(hideAddress_, protectServices_);
        });
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        KLOG_ERROR("Could not start " << workerPath().toStdString());
        wanted_ = false;
        Snapshot failed;
        failed.status = EngineStatus::SteamMissing;
        failed.error = {Msg::SteamInitFailed, {"kettlelan-steam could not be started – reinstall KettleLAN"}};
        onSnapshot_(failed);
    });
}

EngineClient::~EngineClient() { stop(true); }

void EngineClient::start(bool hideAddress, bool protectServices) {
    hideAddress_ = hideAddress;
    protectServices_ = protectServices;
    wanted_ = true;
    if (running()) return;
    stopping_ = false;
    outBuf_.clear();
    logBuf_.clear();
    process_.setArguments({hideAddress ? QStringLiteral("--hide-address=1") : QStringLiteral("--hide-address=0"),
                           protectServices ? QStringLiteral("--protect-services=1") : QStringLiteral("--protect-services=0")});
    Snapshot starting;
    starting.status = EngineStatus::Starting;
    onSnapshot_(starting);
    process_.start();
    send({{"cmd", "nick"}, {"name", QString::fromStdString(nick_)}}); // before it connects to Steam
}

void EngineClient::stop(bool wait) {
    wanted_ = false;
    if (!running()) return;
    stopping_ = true;
    send({{"cmd", "quit"}});
    process_.closeWriteChannel();
    if (!wait) {
        // end it for sure if it hangs; "finished" reports the offline state
        QTimer::singleShot(6000, this, [this] {
            if (running() && !wanted_) process_.kill();
        });
        return;
    }
    if (!process_.waitForFinished(5000)) { // Steam is hanging – do not wait forever
        process_.kill();
        process_.waitForFinished(1000);
    }
}

void EngineClient::send(const QJsonObject& command) {
    if (running()) process_.write(ipc::encodeCommand(command));
}

void EngineClient::join(const std::string& code, const std::string& nameHint) {
    send({{"cmd", "join"}, {"code", QString::fromStdString(code)}, {"name", QString::fromStdString(nameHint)}});
}
void EngineClient::leave(const std::string& code) { send({{"cmd", "leave"}, {"code", QString::fromStdString(code)}}); }
void EngineClient::leaveAll() { send({{"cmd", "leaveAll"}}); }
void EngineClient::rename(const std::string& code, const std::string& name) {
    send({{"cmd", "rename"}, {"code", QString::fromStdString(code)}, {"name", QString::fromStdString(name)}});
}
void EngineClient::setHideAddress(bool on) {
    hideAddress_ = on;
    send({{"cmd", "hideAddress"}, {"on", on}});
}
void EngineClient::setProtectServices(bool on) {
    protectServices_ = on;
    send({{"cmd", "protectServices"}, {"on", on}});
}
void EngineClient::retry() { send({{"cmd", "retry"}}); }
void EngineClient::setNick(const std::string& nick) {
    nick_ = nick;
    send({{"cmd", "nick"}, {"name", QString::fromStdString(nick)}});
}

void EngineClient::readOutput() {
    outBuf_ += process_.readAllStandardOutput();
    for (int nl; (nl = outBuf_.indexOf('\n')) >= 0;) {
        const QByteArray line = outBuf_.left(nl);
        outBuf_.remove(0, nl + 1);
        const auto in = ipc::decodeIncoming(line);
        if (!in) continue;
        if (in->kind == ipc::Incoming::Kind::Snapshot) onSnapshot_(in->snapshot);
        else onEvent_(in->event);
    }
}

// The Steam process's log goes into KettleLAN's log (terminal and file), line by line.
void EngineClient::readLog() {
    logBuf_ += process_.readAllStandardError();
    for (int nl; (nl = logBuf_.indexOf('\n')) >= 0;) {
        std::clog << logBuf_.left(nl).toStdString() << std::endl;
        logBuf_.remove(0, nl + 1);
    }
}

} // namespace kettle::gui

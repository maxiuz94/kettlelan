// KettleLAN – the Steam process (kettlelan-steam). Started by the KettleLAN window while at least one
// network is on: connects to Steam, opens the network adapter and runs the networks. Steam shows
// "Spacewar" exactly as long as this process runs, so the window stops it when every network is off.
//
// Protocol (see engine_ipc.h): commands on standard input, state and events on standard output, one
// JSON object per line; the log goes to standard error. When standard input closes (the window quit
// or crashed) the process leaves its networks and exits.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

#include "engine.h"
#include "engine_ipc.h"

using namespace kettle;
using namespace kettle::gui;

int main(int argc, char** argv) {
    std::mutex outMutex;
    auto send = [&outMutex](const QByteArray& line) {
        std::lock_guard lk(outMutex);
        std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
        std::fflush(stdout);
    };

    Engine engine([&send](const Snapshot& snap) { send(ipc::encodeSnapshot(snap)); },
                  [&send](const Message& message) { send(ipc::encodeEvent(message)); });
    // privacy settings before connecting (from the window's settings)
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--hide-address=0") engine.setHideAddress(false);
        if (arg == "--hide-address=1") engine.setHideAddress(true);
        if (arg == "--protect-services=0") engine.setProtectServices(false);
        if (arg == "--protect-services=1") engine.setProtectServices(true);
    }
    engine.start();

    std::string line;
    while (std::getline(std::cin, line)) {
        const auto cmd = ipc::decodeCommand(QByteArray::fromStdString(line));
        if (!cmd) continue;
        const QString name = cmd->value("cmd").toString();
        const std::string code = cmd->value("code").toString().toStdString();
        const std::string text = cmd->value("name").toString().toStdString();
        if (name == "join") engine.join(code, text);
        else if (name == "leave") engine.leave(code);
        else if (name == "leaveAll") engine.leaveAll();
        else if (name == "rename") engine.rename(code, text);
        else if (name == "hideAddress") engine.setHideAddress(cmd->value("on").toBool());
        else if (name == "protectServices") engine.setProtectServices(cmd->value("on").toBool());
        else if (name == "retry") engine.retry();
        else if (name == "quit") break;
    }
    engine.shutdown(); // leaves the networks and disconnects from Steam (~3 s at most)
    std::fflush(stdout);
    std::clog.flush();
    // Exit right away: objects tied to Steam must not be destroyed after SteamAPI_Shutdown().
    std::_Exit(0);
}

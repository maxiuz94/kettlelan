// KettleLAN – command-line version (useful for diagnostics and SSH sessions).
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "log.h"
#include "messages.h"
#include "netcode.h"
#include "network.h"
#include "session.h"
#include "tun.h"

#ifndef KETTLELAN_VERSION
#define KETTLELAN_VERSION "dev"
#endif

using namespace kettle;

namespace {

std::atomic<bool> g_quit{false};

void onSignal(int) {
    // A second Ctrl+C quits immediately.
    if (g_quit) std::_Exit(130);
    g_quit = true;
}

class CommandQueue {
public:
    void push(std::string cmd) {
        std::lock_guard lk(m_);
        q_.push_back(std::move(cmd));
    }
    bool pop(std::string& out) {
        std::lock_guard lk(m_);
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }

private:
    std::mutex m_;
    std::deque<std::string> q_;
};

void printHelp() {
    std::cout << "\nCommands (up to " << steam::Network::kMaxNetworks << " networks at the same time):\n"
                 "  create [name]   create a new network and show its code\n"
                 "  join <code>     connect to a network, e.g. join KTL-7F3Q-9XM2-A4KD\n"
                 "  list            your networks, their members, addresses and ping\n"
                 "  code            show the codes of your networks\n"
                 "  leave [n|all]   leave network n from \"list\" (or the only one), or all of them\n"
                 "  help            show this help\n"
                 "  quit            exit KettleLAN\n";
#ifdef KETTLELAN_SECURITY_TESTS
    std::cout << "\nSecurity test mode (simulated intruder):\n"
                 "  test-wrong-key <code>   join the network's lobby with a forged key proof\n"
                 "  test-spoof <address>    send members a ping with a forged source address\n"
                 "  test-max-networks <n>   allow n networks at the same time (to see Steam's own limit)\n";
#endif
    std::cout << "\n";
}

void printNetworks(const steam::Network& net) {
    const auto networks = net.networks();
    if (networks.empty()) {
        std::cout << "You are not in a network.\n";
        return;
    }
    int n = 1;
    for (const auto& info : networks) {
        if (info.state != steam::State::Connected) {
            std::cout << "\n" << n++ << ". " << info.code << " – connecting, please wait…\n";
            continue;
        }
        std::cout << "\n" << n++ << ". Network \"" << info.name << "\"  (" << info.code << ")\n";
        for (const auto& p : info.peers) {
            std::string status;
            if (p.self) status = "you";
            else if (p.conflict) status = "address conflict";
            else if (!p.verified) status = "connecting…";
            else if (p.pingMs >= 0) status = std::to_string(p.pingMs) + " ms, " + (p.relayed ? "Steam relay" : "direct");
            else status = "connected";
            std::cout << "  " << std::left << std::setw(16) << p.address.toString() << std::setw(24)
                      << (p.name + (p.owner ? " *" : "")) << status << "\n";
        }
    }
    std::cout << "\n";
}

// Removes escape sequences of special keys (arrows etc.) that the terminal inserts as "^[[A".
std::string stripEscapes(const std::string& line) {
    std::string out;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\x1b') {
            ++i;
            if (i < line.size() && (line[i] == '[' || line[i] == 'O')) {
                ++i;
                while (i < line.size() && !(line[i] >= '@' && line[i] <= '~')) ++i;
            }
            continue;
        }
        out += line[i];
    }
    return out;
}

void handleCommand(const std::string& rawLine, steam::Network& net) {
    const std::string line = stripEscapes(rawLine);
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    std::string rest;
    std::getline(in, rest);
    if (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);

    if (cmd.empty()) return;
    if (cmd == "create") net.create(rest);
    else if (cmd == "join") {
        const auto code = netcode::parse(rest);
        if (!code) std::cout << english({Msg::InvalidCode, {}}) << "\n";
        else net.connect(*code, "");
    } else if (cmd == "list") printNetworks(net);
    else if (cmd == "code") {
        const auto networks = net.networks();
        if (networks.empty()) std::cout << "You are not in a network.\n";
        int n = 1;
        for (const auto& info : networks)
            std::cout << n++ << ". " << info.code << (info.name.empty() ? "" : "  \"" + info.name + "\"") << "\n";
    } else if (cmd == "leave") {
        const auto networks = net.networks();
        if (networks.empty()) std::cout << "You are not in a network.\n";
        else if (rest == "all") net.leaveAll();
        else if (rest.empty() && networks.size() == 1) net.leave(networks.front().id);
        else if (rest.empty()) std::cout << "You are in " << networks.size() << " networks – type \"leave 1\", \"leave 2\" or \"leave all\".\n";
        else {
            const int n = std::atoi(rest.c_str());
            if (n >= 1 && n <= int(networks.size())) net.leave(networks[size_t(n - 1)].id);
            else std::cout << "There is no network " << rest << " – see \"list\".\n";
        }
    }
    else if (cmd == "quit" || cmd == "exit") g_quit = true;
    else if (cmd == "help" || cmd == "?") printHelp();
#ifdef KETTLELAN_SECURITY_TESTS
    else if (cmd == "test-wrong-key") {
        const auto code = netcode::parse(rest);
        if (!code) std::cout << "Give a network code.\n";
        else {
            std::cout << "TEST: joining the network while pretending not to know the key. "
                         "The other side should ignore us.\n";
            net.setDebugWrongKey(true);
            net.connect(*code, "");
        }
    } else if (cmd == "test-spoof") {
        const auto fake = ip::Address4::parse(rest);
        if (!fake) std::cout << "Give an address to forge, e.g. test-spoof 100.65.115.91\n";
        else std::cout << "TEST: sent a forged ping (source " << fake->toString() << ") to "
                       << net.debugSendSpoofed(*fake) << " member(s). It should be dropped.\n";
    }
#endif
#ifdef KETTLELAN_SECURITY_TESTS
    else if (cmd == "test-max-networks") {
        const int n = std::atoi(rest.c_str());
        if (n < 1 || n > 10) std::cout << "Give a number from 1 to 10.\n";
        else {
            net.setDebugMaxNetworks(size_t(n));
            std::cout << "TEST: up to " << n << " networks allowed. Steam may refuse to join more lobbies.\n";
        }
    }
#endif
    else std::cout << "Unknown command \"" << cmd << "\". Type \"help\".\n";
}

} // namespace

int main() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::cout << "KettleLAN " << KETTLELAN_VERSION << " – virtual LAN over the Steam network (open source, BSD-3)\n";

    steam::Session session;
    Message error;
    if (!session.init(error)) {
        std::cerr << english(error) << "\n";
        return 1;
    }

    steam::Network net(session.steamId(), nullptr);
    net.setHideAddress(true);
    net.setProtectServices(true);
    std::cout << "Protection: IP address hidden (Steam relay), computer services protected.\n";

    auto tun = platform::openTun(net.selfAddress(), error);
    if (!tun) {
        std::cerr << english(error) << "\n";
        session.shutdown();
        return 1;
    }
    net.setPacketHandler([&tun](std::span<const uint8_t> p) { tun->write(p); });
    tun->start([&net](std::span<const uint8_t> p) { net.sendPacket(p); });

    if (const auto note = platform::startupWarning()) KLOG_WARN(english(*note)); // log only, for diagnostics
    std::cout << "Your KettleLAN address: " << net.selfAddress().toString() << "\n";
    printHelp();

    CommandQueue commands;
    std::thread input([&commands] {
        std::string line;
        while (!g_quit && std::getline(std::cin, line)) commands.push(line);
        if (std::cin.eof()) g_quit = true;
    });
    input.detach();

    auto nextTick = std::chrono::steady_clock::now();
    while (!g_quit) {
        session.runCallbacks();
        std::string cmd;
        while (commands.pop(cmd)) handleCommand(cmd, net);
        net.poll();
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextTick) {
            net.tick();
            nextTick = now + std::chrono::milliseconds(500);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "Closing KettleLAN…" << std::endl;
    // Safety net: if cleanup hangs, force the process to exit after 3 seconds.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::fputs("KettleLAN did not close within 3 s – forcing exit.\n", stderr);
        std::_Exit(0);
    }).detach();

    net.leave();
    session.runCallbacks();
    tun->stop();
    tun.reset();
    session.shutdown();

    // Exit right away: objects tied to Steam must not be destroyed after SteamAPI_Shutdown(),
    // and the keyboard thread is still waiting for the next line anyway.
    std::cout.flush();
    std::clog.flush();
    std::_Exit(0);
}

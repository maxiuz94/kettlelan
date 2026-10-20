// KettleLAN – wersja konsolowa (etap 1). Interfejs graficzny dojdzie w etapie 3.
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
    // Drugie Ctrl+C zamyka program natychmiast.
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
    std::cout << "\nPolecenia:\n"
                 "  utworz [nazwa]   – tworzy nową sieć i pokazuje jej kod\n"
                 "  dolacz <kod>     – łączy z siecią o tym kodzie, np. dolacz KTL-7F3Q-9XM2-A4KD\n"
                 "  lista            – członkowie sieci, ich adresy i ping\n"
                 "  kod              – pokazuje kod obecnej sieci\n"
                 "  opusc            – opuszcza sieć\n"
                 "  koniec           – zamyka KettleLAN\n"
                 "(działają też angielskie: create, join, list, code, leave, quit)\n\n";
}

void printPeers(const steam::Network& net) {
    if (net.state() == steam::State::Connecting) {
        std::cout << "Łączę się z siecią, chwilę…\n";
        return;
    }
    if (net.state() != steam::State::Connected) {
        std::cout << "Nie jesteś w żadnej sieci.\n";
        return;
    }
    std::cout << "\nSieć „" << net.networkName() << "”  (" << net.codeText() << ")\n";
    for (const auto& p : net.peers()) {
        std::string status;
        if (p.self) status = "to Ty";
        else if (p.conflict) status = "konflikt adresu";
        else if (!p.verified) status = "łączenie…";
        else if (p.pingMs >= 0) status = std::to_string(p.pingMs) + " ms, " + (p.relayed ? "relay Steam" : "bezpośrednio");
        else status = "połączono";
        std::cout << "  " << std::left << std::setw(16) << p.address.toString() << std::setw(24)
                  << (p.name + (p.owner ? " ★" : "")) << status << "\n";
    }
    std::cout << "\n";
}

// Usuwa sekwencje klawiszy specjalnych (strzałki itp.), które terminal wkleja jako „^[[A”.
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
    if (cmd == "utworz" || cmd == "utwórz" || cmd == "create") net.create(rest);
    else if (cmd == "dolacz" || cmd == "dołącz" || cmd == "join") {
        const auto code = netcode::parse(rest);
        if (!code) std::cout << "To nie wygląda na kod sieci (format: KTL-XXXX-XXXX-XXXX).\n";
        else net.connect(*code, "");
    } else if (cmd == "lista" || cmd == "list") printPeers(net);
    else if (cmd == "kod" || cmd == "code") {
        if (net.state() == steam::State::Connected) std::cout << "Kod sieci: " << net.codeText() << "\n";
        else std::cout << "Nie jesteś w żadnej sieci.\n";
    } else if (cmd == "opusc" || cmd == "opuść" || cmd == "leave") net.leave();
    else if (cmd == "koniec" || cmd == "quit" || cmd == "exit") g_quit = true;
    else if (cmd == "pomoc" || cmd == "help" || cmd == "?") printHelp();
    else std::cout << "Nieznane polecenie „" << cmd << "”. Wpisz „pomoc”.\n";
}

} // namespace

int main() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::cout << "KettleLAN " << KETTLELAN_VERSION << " – wirtualny LAN przez sieć Steam (open source, BSD-3)\n";

    steam::Session session;
    std::string error;
    if (!session.init(error)) {
        std::cerr << error << "\n";
        return 1;
    }

    steam::Network net(session.steamId(), nullptr);

    auto tun = platform::openTun(net.selfAddress(), error);
    if (!tun) {
        std::cerr << error << "\n";
        session.shutdown();
        return 1;
    }
    net.setPacketHandler([&tun](std::span<const uint8_t> p) { tun->write(p); });
    tun->start([&net](std::span<const uint8_t> p) { net.sendPacket(p); });

    std::cout << "Twój adres w sieci KettleLAN: " << net.selfAddress().toString() << "\n";
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

    std::cout << "Zamykam KettleLAN…" << std::endl;
    // Zabezpieczenie: gdyby sprzątanie się zacięło, po 3 s kończymy proces na siłę.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::fputs("KettleLAN nie zamknął się w 3 s – wymuszam zamknięcie.\n", stderr);
        std::_Exit(0);
    }).detach();

    net.leave();
    session.runCallbacks();
    tun->stop();
    tun.reset();
    session.shutdown();

    // Kończymy od razu: obiekty powiązane ze Steamem nie mogą być niszczone po SteamAPI_Shutdown(),
    // a wątek czytający klawiaturę i tak czeka na wpisanie kolejnej linii.
    std::cout.flush();
    std::clog.flush();
    std::_Exit(0);
}

// KettleLAN – silnik sieci działający we własnym wątku (Steam + karta TUN).
//
// Wszystkie wywołania Steamworks odbywają się w tym jednym wątku. Interfejs graficzny
// wysyła tu polecenia (post) i dostaje z powrotem migawki stanu (onSnapshot).
#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "network.h"

namespace kettle::gui {

enum class EngineStatus { Starting, SteamMissing, TunError, Ready };

struct Snapshot {
    EngineStatus status = EngineStatus::Starting;
    std::string error;
    std::string personaName;
    std::string selfAddress;
    steam::State netState = steam::State::Idle;
    std::string networkName;
    std::string networkCode;
    std::vector<steam::PeerInfo> peers;
    uint64_t blockedPackets = 0;
};

class Engine {
public:
    using SnapshotHandler = std::function<void(const Snapshot&)>;
    using EventHandler = std::function<void(const std::string&)>;

    Engine(SnapshotHandler onSnapshot, EventHandler onEvent);
    ~Engine();

    void start();     // uruchamia wątek i próbuje połączyć się ze Steamem
    void retry();     // ponowna próba po błędzie
    void create(const std::string& name);
    void join(const std::string& code, const std::string& nameHint);
    void leave();
    void setHideAddress(bool hide);
    void setProtectServices(bool protect);
    // Opuszcza sieć, zamyka kartę i rozłącza Steam. Blokuje do zakończenia (maks. kilka sekund).
    void shutdown();

private:
    void post(std::function<void()> fn);
    void loop();
    void initialize();
    void publish();

    SnapshotHandler onSnapshot_;
    EventHandler onEvent_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex queueMutex_;
    std::deque<std::function<void()>> queue_;

    // stan wątku silnika
    struct State;
    std::unique_ptr<State> st_;
};

} // namespace kettle::gui

// KettleLAN – network engine running on its own thread (Steam + TUN adapter).
//
// All Steamworks calls happen on this single thread. The GUI posts commands here and receives
// state snapshots back (onSnapshot).
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

// Offline: the window has not started the Steam process (no network is on) – Steam shows no "Spacewar".
enum class EngineStatus { Offline, Starting, SteamMissing, TunError, Ready };

struct Snapshot {
    EngineStatus status = EngineStatus::Starting;
    Message error;
    std::string personaName;
    std::string selfAddress;
    std::vector<steam::NetworkInfo> networks; // connecting and connected, in the order they were joined
    size_t maxNetworks = steam::Network::kMaxNetworks;
    uint64_t blockedPackets = 0;
};

class Engine {
public:
    using SnapshotHandler = std::function<void(const Snapshot&)>;
    using EventHandler = std::function<void(const Message&)>;

    Engine(SnapshotHandler onSnapshot, EventHandler onEvent);
    ~Engine();
    void start(); // connects to Steam and opens the adapter (runs in the Steam process)
    void retry();     // retry after an error
    // Connects to (or, when nobody is there, opens) the network with this code.
    void join(const std::string& code, const std::string& nameHint);
    void leave(const std::string& code); // one network
    void leaveAll();
    void rename(const std::string& code, const std::string& name); // shared name (lobby owner only)
    void setNick(const std::string& nick);                          // our name in every network ("" = Steam)
    void setHideAddress(bool hide);
    void setProtectServices(bool protect);
    // Leaves the network, closes the adapter and disconnects Steam. Blocks until done (a few seconds at most).
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

    // engine thread state
    struct State;
    std::unique_ptr<State> st_;
};

} // namespace kettle::gui

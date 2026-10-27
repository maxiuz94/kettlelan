// KettleLAN – network: Steam lobby + packet exchange between members.
//
// Threads:
//  * Steam thread – calls create/connect/leave/poll/tick/peers and handles Steam callbacks,
//  * TUN adapter thread – only calls sendPacket() (thread-safe).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <shared_mutex>
#include <span>
#include <string>
#include <vector>

#include <steam_api.h>

#include "ip.h"
#include "messages.h"
#include "netkeys.h"
#include "netcode.h"
#include "router.h"

namespace kettle::steam {

enum class State { Idle, Connecting, Connected };

struct PeerInfo {
    uint64_t steamId = 0;
    std::string name;
    ip::Address4 address;
    bool self = false;
    bool owner = false;
    bool verified = false;   // proved knowledge of the network key
    bool conflict = false;   // address clashes with someone else (very rare)
    bool blocked = false;    // we blocked this person (nothing is exchanged with them)
    int pingMs = -1;
    bool relayed = false;
    bool steamFriend = false; // your friend on Steam (checked on this computer only)
    std::string steamName;    // their Steam name – only for your Steam friends (you know them anyway)
};

using NetworkHandle = router::NetworkId;

struct NetworkInfo {
    NetworkHandle id = 0;
    State state = State::Connecting;
    std::string code;
    std::string name;            // the shared network name ("" while connecting or while not known yet)
    bool owner = false;          // we opened the lobby (only the owner can rename the network)
    std::vector<PeerInfo> peers; // we are first
};

enum class ConnectResult { Started, AlreadyConnected, LimitReached };

// All KettleLAN networks of this computer: one virtual adapter, one address, up to
// router::Router::kMaxNetworks networks (Steam lobbies) at the same time. Steam messages (packets and
// HELLO) are shared by all networks, so they are handled here, in one place.
class Network {
public:
    using EventHandler = std::function<void(const Message&)>;
    using PacketHandler = std::function<void(std::span<const uint8_t>)>;
    static constexpr size_t kMaxNetworks = router::Router::kMaxNetworks;

    Network(uint64_t selfSteamId, EventHandler onEvent);
    ~Network();

    // --- Steam thread ---
    ConnectResult create(const std::string& name, NetworkHandle* handle = nullptr); // new network with a new code
    // Connects to the network with this code: joins the people already in it, or opens it if
    // nobody is there. `nameHint` – name used when we are the ones opening the network.
    ConnectResult connect(const netcode::NetworkCode& code, const std::string& nameHint, NetworkHandle* handle = nullptr);
    void leave(NetworkHandle network);
    void leaveAll();
    // Renames a network for everyone; only the lobby owner can. False when we are not the owner.
    bool rename(NetworkHandle network, const std::string& name);
    // Our nickname in every network ("" = the Steam name). Encrypted with each network's key.
    void setNick(const std::string& nick);
    // People we blocked (SteamIDs, on this computer only): in every network, no traffic either way.
    void setBlocked(const std::set<uint64_t>& people);
    void poll(); // receive messages – call as often as possible
    void tick(); // periodic work – every ~0.5 s

    std::vector<NetworkInfo> networks() const;
    size_t networkCount() const { return lobbies_.size(); }
    ip::Address4 selfAddress() const { return selfAddress_; }

    // The first network – for the single-network user interface.
    State state() const;
    std::string codeText() const;
    std::string networkName() const;
    std::vector<PeerInfo> peers() const;
    void leave() { leaveAll(); }

    // Privacy and security (Steam thread).
    // hideAddress: Steam relay only – network members never learn our IP address.
    void setHideAddress(bool hide);
    // protectServices: drop packets to system services (SSH, SMB, RDP, databases…).
    void setProtectServices(bool protect) { router_.setProtectServices(protect); }
#ifdef KETTLELAN_SECURITY_TESTS
    // Security tests only: send proofs made with a wrong key (simulated intruder).
    void setDebugWrongKey(bool on) { debugWrongKey_ = on; }
    // Security tests only: sends every member a ping with a forged source address.
    // Returns the number of people it was sent to.
    int debugSendSpoofed(ip::Address4 fakeSource);
    // Limit tests only: raise the network limit to see whether Steam allows more lobbies.
    void setDebugMaxNetworks(size_t n) { router_.setMaxNetworksForTests(n); }
#endif
    uint64_t blockedPackets() const { return blockedPackets_; }

    // --- any thread ---
    void setPacketHandler(PacketHandler handler); // packets to hand over to the TUN adapter
    void sendPacket(std::span<const uint8_t> packet);

private:
    class Lobby;
    friend class Lobby;

    STEAM_CALLBACK(Network, onLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(Network, onSessionRequest, SteamNetworkingMessagesSessionRequest_t);
    STEAM_CALLBACK(Network, onSessionFailed, SteamNetworkingMessagesSessionFailed_t);

    // Steam has one set of lobby search filters, so searches run one after another.
    using SearchDone = std::function<void(const std::vector<uint64_t>& lobbies)>;
    void searchLobbies(const Lobby* owner, const std::string& tag, SearchDone done);
    void runNextSearch();
    void onLobbyList(LobbyMatchList_t* result, bool ioFailure);
    CCallResult<Network, LobbyMatchList_t> listCall_;
    struct Search {
        const Lobby* owner;
        std::string tag;
        SearchDone done;
    };
    std::deque<Search> searches_;
    bool searching_ = false;

    Lobby* lobbyFor(NetworkHandle network) const;
    bool inAnyLobby(uint64_t steamId) const;
    void removeLobby(Lobby* lobby);
    void closeSessions(const std::vector<uint64_t>& people);
    void sendHello(NetworkHandle network, uint64_t to, bool ack);
    void handleControl(uint64_t from, std::span<const uint8_t> data);
    void handleData(uint64_t from, std::span<const uint8_t> data);
    void sendTo(uint64_t steamId, std::span<const uint8_t> data, int channel);
    std::vector<PeerInfo> peersOf(const Lobby& lobby) const;
    // How we show a person: their nickname in our networks, else their Steam name.
    std::string personName(uint64_t steamId) const;
    // Name for messages; adds the code when two of our networks have the same name.
    std::string displayName(const Lobby& lobby) const;
    void notify(Msg id, std::vector<std::string> args = {});

    const uint64_t selfId_;
    const ip::Address4 selfAddress_;
    EventHandler onEvent_;

    std::vector<std::unique_ptr<Lobby>> lobbies_; // in the order they were joined
    std::vector<std::unique_ptr<Lobby>> retired_; // left lobbies, kept until their Steam calls finish

    router::Router router_;          // who is verified and which packets we accept
    std::set<uint64_t> reportedBad_; // whom we already reported (no log flooding)
    std::string nick_;                               // ours ("" = the Steam name)
    mutable std::map<uint64_t, std::string> nicks_;  // last nickname seen per person (for "X left")
    std::chrono::steady_clock::time_point nextKeepalive_{};
#ifdef KETTLELAN_SECURITY_TESTS
    bool debugWrongKey_ = false;
#endif
    std::atomic<uint64_t> blockedPackets_{0};
    std::mutex blockedLogMutex_;
    std::set<std::pair<uint64_t, uint16_t>> blockedLogged_; // (who, port) – logged only once
    std::map<uint64_t, std::chrono::steady_clock::time_point> floodLogged_; // Steam thread: last flood warning per person
    std::mutex handlerMutex_;
    PacketHandler packetHandler_;
};

} // namespace kettle::steam

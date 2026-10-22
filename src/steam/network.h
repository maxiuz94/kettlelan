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
    int pingMs = -1;
    bool relayed = false;
};

class Network {
public:
    using EventHandler = std::function<void(const Message&)>;
    using PacketHandler = std::function<void(std::span<const uint8_t>)>;

    Network(uint64_t selfSteamId, EventHandler onEvent);
    ~Network();

    // --- Steam thread ---
    void create(const std::string& name);  // new network with a new code
    // Connects to the network with this code: joins the people already in it, or opens it if
    // nobody is there. `nameHint` – name used when we are the ones opening the network.
    void connect(const netcode::NetworkCode& code, const std::string& nameHint);
    void leave();
    void poll();   // receive messages – call as often as possible
    void tick();   // periodic work – every ~0.5 s

    State state() const { return state_; }
    std::string codeText() const;
    std::string networkName() const;
    std::vector<PeerInfo> peers() const;
    ip::Address4 selfAddress() const { return selfAddress_; }

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
#endif
    uint64_t blockedPackets() const { return blockedPackets_; }

    // --- any thread ---
    void setPacketHandler(PacketHandler handler); // packets to hand over to the TUN adapter
    void sendPacket(std::span<const uint8_t> packet);

private:
    STEAM_CALLBACK(Network, onLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(Network, onSessionRequest, SteamNetworkingMessagesSessionRequest_t);
    STEAM_CALLBACK(Network, onSessionFailed, SteamNetworkingMessagesSessionFailed_t);

    void startSearch(bool merge);
    void onLobbyList(LobbyMatchList_t* result, bool ioFailure);
    void onLobbyCreated(LobbyCreated_t* result, bool ioFailure);
    void onLobbyEntered(LobbyEnter_t* result, bool ioFailure);
    void enterLobby(CSteamID id);
    void leaveLobbyOnly();
    CCallResult<Network, LobbyMatchList_t> listCall_;
    CCallResult<Network, LobbyCreated_t> createCall_;
    CCallResult<Network, LobbyEnter_t> enterCall_;

    void enterConnected();
    void syncMembers();
    bool isLobbyMember(uint64_t steamId) const;
    void sendHello(uint64_t to, bool ack);
    void handleControl(uint64_t from, std::span<const uint8_t> data);
    void handleData(uint64_t from, std::span<const uint8_t> data);
    void sendTo(uint64_t steamId, std::span<const uint8_t> data, int channel);
    void closeAllSessions();
    void resetState();
    void notify(Msg id, std::vector<std::string> args = {});

    const uint64_t selfId_;
    const ip::Address4 selfAddress_;
    EventHandler onEvent_;

    std::atomic<State> state_{State::Idle};
    CSteamID lobby_;
    std::optional<netcode::NetworkCode> code_;
    std::optional<netkeys::Keys> keys_; // derived once per connect (Argon2id)
    std::string tag_;
    std::string nameHint_;
    bool searchPending_ = false;
    bool mergeSearch_ = false;
    int joinRetries_ = 0;
    std::chrono::steady_clock::time_point nextMergeCheck_{};

    router::Router router_;                    // who is verified and which packets we accept
    std::set<uint64_t> reportedBad_;           // whom we already reported (no log flooding)
#ifdef KETTLELAN_SECURITY_TESTS
    bool debugWrongKey_ = false;
#endif
    std::atomic<uint64_t> blockedPackets_{0};
    std::mutex blockedLogMutex_;
    std::set<std::pair<uint64_t, uint16_t>> blockedLogged_; // (who, port) – logged only once
    std::mutex handlerMutex_;
    PacketHandler packetHandler_;
};

} // namespace kettle::steam

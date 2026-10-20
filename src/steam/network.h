// KettleLAN – sieć: lobby Steama + przesyłanie pakietów między członkami.
//
// Wątki:
//  * wątek Steama – wywołuje create/join/leave/poll/tick/peers i obsługuje zdarzenia Steama,
//  * wątek karty TUN – wywołuje wyłącznie sendPacket() (bezpieczne wielowątkowo).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <vector>

#include <steam_api.h>

#include "ip.h"
#include "netcode.h"

namespace kettle::steam {

enum class State { Idle, Creating, Joining, Connected };

struct PeerInfo {
    uint64_t steamId = 0;
    std::string name;
    ip::Address4 address;
    bool self = false;
    bool owner = false;
    bool verified = false;   // potwierdził znajomość klucza sieci
    bool conflict = false;   // adres koliduje z kimś innym (bardzo rzadkie)
    int pingMs = -1;
    bool relayed = false;
};

class Network {
public:
    using EventHandler = std::function<void(const std::string&)>;
    using PacketHandler = std::function<void(std::span<const uint8_t>)>;

    Network(uint64_t selfSteamId, EventHandler onEvent);
    ~Network();

    // --- wątek Steama ---
    void create(const std::string& name);
    void join(const netcode::NetworkCode& code);
    void leave();
    void poll();   // odbiór wiadomości – wywoływać jak najczęściej
    void tick();   // zadania okresowe – co ~0,5 s

    State state() const { return state_; }
    std::string codeText() const;
    std::string networkName() const;
    std::vector<PeerInfo> peers() const;
    ip::Address4 selfAddress() const { return selfAddress_; }

    // --- dowolny wątek ---
    void setPacketHandler(PacketHandler handler); // pakiety do oddania karcie TUN
    void sendPacket(std::span<const uint8_t> packet);

private:
    struct Peer {
        bool verified = false;
        bool conflict = false;
        bool badKeyReported = false;
        int hellosSent = 0;
        std::chrono::steady_clock::time_point nextHello{};
        ip::Address4 address;
    };

    STEAM_CALLBACK(Network, onLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(Network, onSessionRequest, SteamNetworkingMessagesSessionRequest_t);
    STEAM_CALLBACK(Network, onSessionFailed, SteamNetworkingMessagesSessionFailed_t);

    void onLobbyCreated(LobbyCreated_t* result, bool ioFailure);
    void onLobbyEntered(LobbyEnter_t* result, bool ioFailure);
    CCallResult<Network, LobbyCreated_t> createCall_;
    CCallResult<Network, LobbyEnter_t> enterCall_;

    void enterConnected();
    void syncMembers();
    bool isLobbyMember(uint64_t steamId) const;
    void sendHello(uint64_t to, bool ack);
    void handleControl(uint64_t from, std::span<const uint8_t> data);
    void handleData(uint64_t from, std::span<const uint8_t> data);
    void sendTo(uint64_t steamId, std::span<const uint8_t> data, int channel);
    void removePeer(uint64_t steamId);
    void resetState();
    void notify(const std::string& text);

    const uint64_t selfId_;
    const ip::Address4 selfAddress_;
    EventHandler onEvent_;

    std::atomic<State> state_{State::Idle};
    CSteamID lobby_;
    std::optional<netcode::NetworkCode> code_;
    std::string pendingName_;

    mutable std::shared_mutex mutex_;          // chroni peers_ i routes_
    std::map<uint64_t, Peer> peers_;           // członkowie lobby (bez nas)
    std::map<uint32_t, uint64_t> routes_;      // adres IP → SteamID (tylko zweryfikowani)
    std::mutex handlerMutex_;
    PacketHandler packetHandler_;
};

} // namespace kettle::steam

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
#include <set>
#include <utility>
#include <shared_mutex>
#include <span>
#include <string>
#include <vector>

#include <steam_api.h>

#include "ip.h"
#include "netcode.h"

namespace kettle::steam {

enum class State { Idle, Connecting, Connected };

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
    void create(const std::string& name);  // nowa sieć z nowym kodem
    // Łączy z siecią o danym kodzie: dołącza do osób, które już w niej są, albo otwiera ją,
    // jeśli nikogo nie ma. `nameHint` – nazwa używana, gdy to my otwieramy sieć.
    void connect(const netcode::NetworkCode& code, const std::string& nameHint);
    void leave();
    void poll();   // odbiór wiadomości – wywoływać jak najczęściej
    void tick();   // zadania okresowe – co ~0,5 s

    State state() const { return state_; }
    std::string codeText() const;
    std::string networkName() const;
    std::vector<PeerInfo> peers() const;
    ip::Address4 selfAddress() const { return selfAddress_; }

    // Prywatność i bezpieczeństwo (wątek Steama).
    // hideAddress: połączenia tylko przez relay Steama – członkowie sieci nie poznają naszego IP.
    void setHideAddress(bool hide);
    // protectServices: odrzucanie pakietów do usług systemowych (SSH, SMB, RDP, bazy danych…).
    void setProtectServices(bool protect) { protectServices_ = protect; }
    uint64_t blockedPackets() const { return blockedPackets_; }

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
    void removePeer(uint64_t steamId);
    void resetState();
    void notify(const std::string& text);

    const uint64_t selfId_;
    const ip::Address4 selfAddress_;
    EventHandler onEvent_;

    std::atomic<State> state_{State::Idle};
    CSteamID lobby_;
    std::optional<netcode::NetworkCode> code_;
    std::string tag_;
    std::string nameHint_;
    bool searchPending_ = false;
    bool mergeSearch_ = false;
    int joinRetries_ = 0;
    std::chrono::steady_clock::time_point nextMergeCheck_{};

    mutable std::shared_mutex mutex_;          // chroni peers_ i routes_
    std::map<uint64_t, Peer> peers_;           // członkowie lobby (bez nas)
    std::map<uint32_t, uint64_t> routes_;      // adres IP → SteamID (tylko zweryfikowani)
    std::atomic<bool> protectServices_{true};
    std::atomic<uint64_t> blockedPackets_{0};
    std::mutex blockedLogMutex_;
    std::set<std::pair<uint64_t, uint16_t>> blockedLogged_; // (kto, port) – logujemy tylko raz
    std::mutex handlerMutex_;
    PacketHandler packetHandler_;
};

} // namespace kettle::steam

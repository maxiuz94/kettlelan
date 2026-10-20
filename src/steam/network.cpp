#include "network.h"

#include <algorithm>
#include <random>

#include "log.h"
#include "protocol.h"

namespace kettle::steam {

using Clock = std::chrono::steady_clock;

namespace {

constexpr int kMaxMembers = 250;
constexpr auto kHelloInterval = std::chrono::seconds(2);
constexpr int kHelloAttempts = 15; // ok. 30 s prób
constexpr int kDataFlags = k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_AutoRestartBrokenSession;
constexpr int kControlFlags = k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_AutoRestartBrokenSession;

constexpr const char* kKeyMarker = "kettlelan";
constexpr const char* kKeyName = "name";
constexpr const char* kKeyProto = "proto";

SteamNetworkingIdentity identityFor(uint64_t steamId) {
    SteamNetworkingIdentity id;
    id.SetSteamID64(steamId);
    return id;
}

std::string nameOf(uint64_t steamId) {
    const char* n = SteamFriends()->GetFriendPersonaName(CSteamID(uint64(steamId)));
    return (n && *n) ? std::string(n) : std::to_string(steamId);
}

uint32_t randomSecret() {
    std::random_device rd;
    uint32_t s = 0;
    while (s == 0) s = (uint32_t(rd()) ^ (uint32_t(rd()) << 7)) & netcode::kSecretMask;
    return s;
}

} // namespace

Network::Network(uint64_t selfSteamId, EventHandler onEvent)
    : selfId_(selfSteamId), selfAddress_(ip::addressForSteamId(selfSteamId)), onEvent_(std::move(onEvent)) {}

Network::~Network() {
    if (state_ != State::Idle) leave();
}

void Network::notify(const std::string& text) {
    KLOG_INFO(text);
    if (onEvent_) onEvent_(text);
}

// ---------------------------------------------------------------- tworzenie / dołączanie

void Network::create(const std::string& name) {
    if (state_ != State::Idle) {
        notify("Najpierw opuść obecną sieć.");
        return;
    }
    pendingName_ = name.empty() ? (std::string(SteamFriends()->GetPersonaName()) + " – sieć") : name;
    state_ = State::Creating;
    createCall_.Set(SteamMatchmaking()->CreateLobby(k_ELobbyTypeInvisible, kMaxMembers), this,
                    &Network::onLobbyCreated);
    notify("Tworzę sieć „" + pendingName_ + "”…");
}

void Network::onLobbyCreated(LobbyCreated_t* r, bool ioFailure) {
    if (ioFailure || r->m_eResult != k_EResultOK) {
        state_ = State::Idle;
        notify("Nie udało się utworzyć sieci (Steam: błąd " + std::to_string(ioFailure ? -1 : int(r->m_eResult)) + ").");
        return;
    }
    lobby_ = CSteamID(uint64(r->m_ulSteamIDLobby));
    code_ = netcode::NetworkCode{lobby_.GetAccountID(), randomSecret()};
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyMarker, "1");
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyName, pendingName_.c_str());
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyProto, std::to_string(protocol::kVersion).c_str());
    // Twórca lobby jest w nim od razu – Steam nie wyśle nam osobnego wyniku dołączenia.
    enterConnected();
}

void Network::join(const netcode::NetworkCode& code) {
    if (state_ != State::Idle) {
        notify("Najpierw opuść obecną sieć.");
        return;
    }
    code_ = code;
    lobby_ = CSteamID(code.lobbyAccountId, k_EChatInstanceFlagLobby, SteamUtils()->GetConnectedUniverse(),
                      k_EAccountTypeChat);
    state_ = State::Joining;
    enterCall_.Set(SteamMatchmaking()->JoinLobby(lobby_), this, &Network::onLobbyEntered);
    notify("Dołączam do sieci " + netcode::format(code) + "…");
}

void Network::onLobbyEntered(LobbyEnter_t* r, bool ioFailure) {
    if (state_ != State::Joining) return;
    if (ioFailure || r->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
        const auto resp = ioFailure ? 0u : r->m_EChatRoomEnterResponse;
        resetState();
        if (resp == k_EChatRoomEnterResponseDoesntExist)
            notify("Sieć o tym kodzie nie istnieje – sprawdź kod albo poproś o nowy.");
        else if (resp == k_EChatRoomEnterResponseFull)
            notify("Sieć jest pełna.");
        else
            notify("Nie udało się dołączyć do sieci (Steam: kod " + std::to_string(resp) + ").");
        return;
    }
    if (std::string(SteamMatchmaking()->GetLobbyData(lobby_, kKeyMarker)) != "1") {
        SteamMatchmaking()->LeaveLobby(lobby_);
        resetState();
        notify("Ten kod nie należy do sieci KettleLAN.");
        return;
    }
    enterConnected();
}

void Network::enterConnected() {
    SteamMatchmaking()->SetLobbyMemberData(lobby_, "ip", selfAddress_.toString().c_str());
    state_ = State::Connected;
    syncMembers();
    notify("Połączono z siecią „" + networkName() + "”. Twój adres: " + selfAddress_.toString() +
         ", kod sieci: " + codeText());
}

void Network::leave() {
    if (state_ == State::Idle) return;
    if (lobby_.IsValid()) SteamMatchmaking()->LeaveLobby(lobby_);
    std::vector<uint64_t> ids;
    {
        std::shared_lock lk(mutex_);
        for (auto& [id, _] : peers_) ids.push_back(id);
    }
    for (uint64_t id : ids) SteamNetworkingMessages()->CloseSessionWithUser(identityFor(id));
    resetState();
    notify("Opuszczono sieć.");
}

void Network::resetState() {
    std::unique_lock lk(mutex_);
    peers_.clear();
    routes_.clear();
    lobby_ = CSteamID();
    code_.reset();
    state_ = State::Idle;
}

// ---------------------------------------------------------------- członkowie

bool Network::isLobbyMember(uint64_t steamId) const {
    if (!lobby_.IsValid()) return false;
    const int n = SteamMatchmaking()->GetNumLobbyMembers(lobby_);
    for (int i = 0; i < n; ++i)
        if (SteamMatchmaking()->GetLobbyMemberByIndex(lobby_, i).ConvertToUint64() == steamId) return true;
    return false;
}

void Network::syncMembers() {
    if (state_ != State::Connected) return;
    std::vector<uint64_t> current;
    const int n = SteamMatchmaking()->GetNumLobbyMembers(lobby_);
    for (int i = 0; i < n; ++i) {
        const uint64_t id = SteamMatchmaking()->GetLobbyMemberByIndex(lobby_, i).ConvertToUint64();
        if (id != selfId_) current.push_back(id);
    }
    std::vector<uint64_t> gone;
    {
        std::unique_lock lk(mutex_);
        for (uint64_t id : current) {
            auto [it, inserted] = peers_.try_emplace(id);
            if (inserted) {
                it->second.address = ip::addressForSteamId(id);
                it->second.nextHello = Clock::now();
            }
        }
        for (auto& [id, _] : peers_)
            if (std::find(current.begin(), current.end(), id) == current.end()) gone.push_back(id);
    }
    for (uint64_t id : gone) removePeer(id);
}

void Network::removePeer(uint64_t steamId) {
    bool wasVerified = false;
    {
        std::unique_lock lk(mutex_);
        auto it = peers_.find(steamId);
        if (it == peers_.end()) return;
        wasVerified = it->second.verified;
        auto r = routes_.find(it->second.address.value());
        if (r != routes_.end() && r->second == steamId) routes_.erase(r);
        peers_.erase(it);
    }
    SteamNetworkingMessages()->CloseSessionWithUser(identityFor(steamId));
    if (wasVerified) notify(nameOf(steamId) + " opuścił(a) sieć.");
}

void Network::onLobbyChatUpdate(LobbyChatUpdate_t* ev) {
    if (state_ != State::Connected || ev->m_ulSteamIDLobby != lobby_.ConvertToUint64()) return;
    syncMembers();
}

// ---------------------------------------------------------------- sesje Steam Networking

void Network::onSessionRequest(SteamNetworkingMessagesSessionRequest_t* ev) {
    const uint64_t from = ev->m_identityRemote.GetSteamID64();
    // Przyjmujemy połączenia wyłącznie od osób z naszego lobby.
    if (state_ == State::Connected && isLobbyMember(from)) {
        SteamNetworkingMessages()->AcceptSessionWithUser(ev->m_identityRemote);
    } else {
        KLOG_DEBUG("Odrzucono sesję od " << from);
    }
}

void Network::onSessionFailed(SteamNetworkingMessagesSessionFailed_t* ev) {
    KLOG_WARN("Połączenie z " << nameOf(ev->m_info.m_identityRemote.GetSteamID64())
              << " przerwane: " << ev->m_info.m_szEndDebug);
}

// ---------------------------------------------------------------- HELLO

void Network::sendHello(uint64_t to, bool ack) {
    if (!code_) return;
    protocol::HelloMessage msg;
    msg.type = ack ? protocol::MessageType::HelloAck : protocol::MessageType::Hello;
    msg.address = selfAddress_;
    msg.proof = protocol::computeProof(*code_, selfId_, to);
    const auto bytes = protocol::encode(msg);
    sendTo(to, bytes, protocol::kChannelControl);
}

void Network::handleControl(uint64_t from, std::span<const uint8_t> data) {
    if (state_ != State::Connected || !code_) return;
    const auto msg = protocol::decode(data);
    if (!msg) return;

    // HELLO może przyjść, zanim Steam powiadomi nas o nowym członku lobby.
    bool known;
    {
        std::shared_lock lk(mutex_);
        known = peers_.count(from) != 0;
    }
    if (!known && isLobbyMember(from)) syncMembers();

    bool reply = false;
    std::string announce;
    {
        std::unique_lock lk(mutex_);
        auto it = peers_.find(from);
        if (it == peers_.end()) return; // nie ma go (jeszcze) na liście członków
        Peer& peer = it->second;

        if (msg->proof != protocol::computeProof(*code_, from, selfId_)) {
            if (!peer.badKeyReported) {
                peer.badKeyReported = true;
                lk.unlock();
                notify(nameOf(from) + " jest w lobby, ale nie zna klucza sieci – ignoruję.");
            }
            return;
        }
        if (msg->version != protocol::kVersion) {
            if (!peer.badKeyReported) {
                peer.badKeyReported = true;
                lk.unlock();
                notify(nameOf(from) + " używa innej wersji KettleLAN – zaktualizujcie program.");
            }
            return;
        }
        if (msg->address != peer.address) return; // adres musi wynikać z jego SteamID

        reply = msg->type == protocol::MessageType::Hello;
        if (!peer.verified) {
            peer.verified = true;
            const auto existing = routes_.find(peer.address.value());
            if (peer.address == selfAddress_ || (existing != routes_.end() && existing->second != from)) {
                peer.conflict = true;
                announce = nameOf(from) + " ma ten sam adres co ktoś inny w sieci – ruch do tej osoby nie będzie działał.";
            } else {
                routes_[peer.address.value()] = from;
                announce = nameOf(from) + " dołączył(a) – adres " + peer.address.toString();
            }
        }
    }
    if (reply) sendHello(from, true);
    if (!announce.empty()) notify(announce);
}

// ---------------------------------------------------------------- dane

void Network::sendTo(uint64_t steamId, std::span<const uint8_t> data, int channel) {
    const EResult r = SteamNetworkingMessages()->SendMessageToUser(
        identityFor(steamId), data.data(), uint32(data.size()),
        channel == protocol::kChannelData ? kDataFlags : kControlFlags, channel);
    if (r != k_EResultOK && r != k_EResultNoConnection) KLOG_DEBUG("Wysyłka do " << steamId << ": " << int(r));
}

void Network::sendPacket(std::span<const uint8_t> packet) {
    if (state_ != State::Connected) return;
    const auto hdr = ip::parseIPv4(packet);
    if (!hdr || hdr->src != selfAddress_) return;

    std::shared_lock lk(mutex_);
    if (ip::isGroupDestination(hdr->dst)) {
        for (auto& [addr, id] : routes_) sendTo(id, packet, protocol::kChannelData);
        return;
    }
    const auto it = routes_.find(hdr->dst.value());
    if (it != routes_.end()) sendTo(it->second, packet, protocol::kChannelData);
}

void Network::handleData(uint64_t from, std::span<const uint8_t> data) {
    const auto hdr = ip::parseIPv4(data);
    if (!hdr) return;
    if (hdr->dst != selfAddress_ && !ip::isGroupDestination(hdr->dst)) return;
    {
        std::shared_lock lk(mutex_);
        const auto it = routes_.find(hdr->src.value());
        // nadawca musi być zweryfikowany i nie może podszywać się pod cudzy adres
        if (it == routes_.end() || it->second != from) return;
    }
    std::lock_guard lk(handlerMutex_);
    if (packetHandler_) packetHandler_(data);
}

void Network::setPacketHandler(PacketHandler handler) {
    std::lock_guard lk(handlerMutex_);
    packetHandler_ = std::move(handler);
}

void Network::poll() {
    SteamNetworkingMessage_t* msgs[64];
    for (int channel : {protocol::kChannelControl, protocol::kChannelData}) {
        int n;
        while ((n = SteamNetworkingMessages()->ReceiveMessagesOnChannel(channel, msgs, 64)) > 0) {
            for (int i = 0; i < n; ++i) {
                const uint64_t from = msgs[i]->m_identityPeer.GetSteamID64();
                const std::span<const uint8_t> data(static_cast<const uint8_t*>(msgs[i]->m_pData),
                                                    size_t(msgs[i]->m_cbSize));
                if (channel == protocol::kChannelControl) handleControl(from, data);
                else handleData(from, data);
                msgs[i]->Release();
            }
        }
    }
}

void Network::tick() {
    if (state_ != State::Connected) return;
    const auto now = Clock::now();
    std::vector<uint64_t> due;
    {
        std::unique_lock lk(mutex_);
        for (auto& [id, peer] : peers_) {
            if (peer.verified || peer.hellosSent >= kHelloAttempts || now < peer.nextHello) continue;
            peer.hellosSent++;
            peer.nextHello = now + kHelloInterval;
            due.push_back(id);
        }
    }
    for (uint64_t id : due) sendHello(id, false);
}

// ---------------------------------------------------------------- informacje

std::string Network::codeText() const { return code_ ? netcode::format(*code_) : std::string(); }

std::string Network::networkName() const {
    if (!lobby_.IsValid()) return {};
    return SteamMatchmaking()->GetLobbyData(lobby_, kKeyName);
}

std::vector<PeerInfo> Network::peers() const {
    std::vector<PeerInfo> out;
    if (state_ != State::Connected) return out;
    const uint64_t owner = SteamMatchmaking()->GetLobbyOwner(lobby_).ConvertToUint64();

    PeerInfo self;
    self.steamId = selfId_;
    self.name = SteamFriends()->GetPersonaName();
    self.address = selfAddress_;
    self.self = true;
    self.verified = true;
    self.owner = owner == selfId_;
    out.push_back(self);

    std::shared_lock lk(mutex_);
    for (const auto& [id, peer] : peers_) {
        PeerInfo p;
        p.steamId = id;
        p.name = nameOf(id);
        p.address = peer.address;
        p.owner = owner == id;
        p.verified = peer.verified;
        p.conflict = peer.conflict;
        SteamNetConnectionInfo_t info{};
        SteamNetConnectionRealTimeStatus_t status{};
        const auto st = SteamNetworkingMessages()->GetSessionConnectionInfo(identityFor(id), &info, &status);
        if (st == k_ESteamNetworkingConnectionState_Connected) {
            p.pingMs = status.m_nPing;
            p.relayed = (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) != 0;
        }
        out.push_back(p);
    }
    return out;
}

} // namespace kettle::steam

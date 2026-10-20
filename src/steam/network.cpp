#include "network.h"

#include <algorithm>

#include "filter.h"
#include "log.h"
#include "protocol.h"
#include "text.h"

namespace kettle::steam {

using Clock = std::chrono::steady_clock;

namespace {

constexpr int kMaxMembers = 250;
constexpr auto kHelloInterval = std::chrono::seconds(2);
constexpr int kHelloAttempts = 15; // ok. 30 s prób
constexpr int kDataFlags = k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_AutoRestartBrokenSession;
constexpr int kControlFlags = k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_AutoRestartBrokenSession;

constexpr auto kFirstMergeCheck = std::chrono::seconds(5);
constexpr auto kMergeInterval = std::chrono::seconds(30);
constexpr int kMaxJoinRetries = 3;

constexpr const char* kKeyMarker = "kettlelan";
constexpr const char* kMarkerValue = "2";
constexpr const char* kKeyTag = "tag";
constexpr const char* kKeyName = "name";
constexpr const char* kKeyProto = "proto";

SteamNetworkingIdentity identityFor(uint64_t steamId) {
    SteamNetworkingIdentity id;
    id.SetSteamID64(steamId);
    return id;
}

// Nick innej osoby – tekst od obcych, więc zawsze oczyszczony ze znaków sterujących.
std::string nameOf(uint64_t steamId) {
    const char* n = SteamFriends()->GetFriendPersonaName(CSteamID(uint64(steamId)));
    const std::string clean = n ? text::sanitize(n, 32) : std::string();
    return clean.empty() ? std::to_string(steamId) : clean;
}

// Porównanie w stałym czasie (nie zdradza, ile bajtów dowodu się zgadza).
bool equalConstantTime(uint64_t a, uint64_t b) {
    uint64_t diff = a ^ b;
    diff |= diff >> 32;
    diff |= diff >> 16;
    diff |= diff >> 8;
    return (diff & 0xFF) == 0;
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

// ---------------------------------------------------------------- tworzenie / łączenie
//
// Sieć nie ma stałego lobby. Kod wyznacza znacznik (tag); przy łączeniu szukamy w Steamie lobby
// z tym znacznikiem. Jeśli nikogo nie ma, otwieramy nowe lobby z tym samym znacznikiem.
// Gdyby powstały dwa lobby naraz, wszyscy przechodzą do tego o najmniejszym numerze.

void Network::create(const std::string& name) {
    connect(netcode::generate(), name.empty() ? (std::string(SteamFriends()->GetPersonaName()) + " – sieć") : name);
}

void Network::connect(const netcode::NetworkCode& code, const std::string& nameHint) {
    if (state_ != State::Idle) {
        notify("Najpierw opuść obecną sieć.");
        return;
    }
    code_ = code;
    tag_ = protocol::lobbyTag(code);
    nameHint_ = nameHint;
    joinRetries_ = 0;
    state_ = State::Connecting;
    notify("Łączę z siecią " + netcode::format(code) + "…");
    startSearch(false);
}

void Network::startSearch(bool merge) {
    mergeSearch_ = merge;
    searchPending_ = true;
    SteamMatchmaking()->AddRequestLobbyListStringFilter(kKeyTag, tag_.c_str(), k_ELobbyComparisonEqual);
    SteamMatchmaking()->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
    SteamMatchmaking()->AddRequestLobbyListResultCountFilter(10);
    listCall_.Set(SteamMatchmaking()->RequestLobbyList(), this, &Network::onLobbyList);
}

void Network::onLobbyList(LobbyMatchList_t* r, bool ioFailure) {
    searchPending_ = false;
    const bool merge = mergeSearch_;
    if (!code_) return; // w międzyczasie opuszczono sieć

    // najmniejszy numer lobby z naszym znacznikiem (sprawdzamy znacznik jeszcze raz)
    uint64_t best = 0;
    const uint32 count = ioFailure ? 0 : r->m_nLobbiesMatching;
    for (uint32 i = 0; i < count; ++i) {
        const CSteamID id = SteamMatchmaking()->GetLobbyByIndex(int(i));
        if (std::string(SteamMatchmaking()->GetLobbyData(id, kKeyTag)) != tag_) continue;
        if (best == 0 || id.ConvertToUint64() < best) best = id.ConvertToUint64();
    }

    if (merge) {
        if (state_ != State::Connected || best == 0 || best >= lobby_.ConvertToUint64()) return;
        notify("Łączę się z resztą osób z tej sieci…");
        leaveLobbyOnly();
        state_ = State::Connecting;
        enterLobby(CSteamID(uint64(best)));
        return;
    }

    if (state_ != State::Connecting) return;
    if (best != 0) {
        enterLobby(CSteamID(uint64(best)));
    } else {
        // nikogo nie ma – otwieramy sieć
        createCall_.Set(SteamMatchmaking()->CreateLobby(k_ELobbyTypeInvisible, kMaxMembers), this,
                        &Network::onLobbyCreated);
    }
}

void Network::onLobbyCreated(LobbyCreated_t* r, bool ioFailure) {
    if (state_ != State::Connecting || !code_) return;
    if (ioFailure || r->m_eResult != k_EResultOK) {
        resetState();
        notify("Nie udało się otworzyć sieci (Steam: błąd " + std::to_string(ioFailure ? -1 : int(r->m_eResult)) + ").");
        return;
    }
    lobby_ = CSteamID(uint64(r->m_ulSteamIDLobby));
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyMarker, kMarkerValue);
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyTag, tag_.c_str());
    const std::string name = text::sanitize(nameHint_, 60);
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyName, (name.empty() ? std::string("Sieć KettleLAN") : name).c_str());
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyProto, std::to_string(protocol::kVersion).c_str());
    // Twórca lobby jest w nim od razu – Steam nie wyśle nam osobnego wyniku dołączenia.
    enterConnected();
}

void Network::enterLobby(CSteamID id) {
    lobby_ = id;
    enterCall_.Set(SteamMatchmaking()->JoinLobby(lobby_), this, &Network::onLobbyEntered);
}

void Network::onLobbyEntered(LobbyEnter_t* r, bool ioFailure) {
    if (state_ != State::Connecting || !code_) return;
    const bool ok = !ioFailure && r->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess;
    if (!ok) {
        // Lobby mogło zniknąć między wyszukaniem a dołączeniem – szukamy jeszcze raz.
        lobby_ = CSteamID();
        if (++joinRetries_ <= kMaxJoinRetries) {
            startSearch(false);
            return;
        }
        resetState();
        notify("Nie udało się połączyć z siecią (Steam: kod " +
               std::to_string(ioFailure ? 0u : r->m_EChatRoomEnterResponse) + "). Spróbuj ponownie.");
        return;
    }
    if (std::string(SteamMatchmaking()->GetLobbyData(lobby_, kKeyTag)) != tag_) {
        SteamMatchmaking()->LeaveLobby(lobby_);
        resetState();
        notify("To lobby nie należy do tej sieci KettleLAN.");
        return;
    }
    enterConnected();
}

void Network::enterConnected() {
    SteamMatchmaking()->SetLobbyMemberData(lobby_, "ip", selfAddress_.toString().c_str());
    state_ = State::Connected;
    nextMergeCheck_ = Clock::now() + kFirstMergeCheck;
    syncMembers();
    notify("Połączono z siecią „" + networkName() + "”. Twój adres: " + selfAddress_.toString() +
           ", kod sieci: " + codeText());
}

void Network::leaveLobbyOnly() {
    if (lobby_.IsValid()) SteamMatchmaking()->LeaveLobby(lobby_);
    std::vector<uint64_t> ids;
    {
        std::unique_lock lk(mutex_);
        for (auto& [id, _] : peers_) ids.push_back(id);
        peers_.clear();
        routes_.clear();
    }
    for (uint64_t id : ids) SteamNetworkingMessages()->CloseSessionWithUser(identityFor(id));
    lobby_ = CSteamID();
}

void Network::leave() {
    if (state_ == State::Idle) return;
    leaveLobbyOnly();
    resetState();
    notify("Opuszczono sieć.");
}

void Network::resetState() {
    std::unique_lock lk(mutex_);
    peers_.clear();
    routes_.clear();
    lobby_ = CSteamID();
    code_.reset();
    tag_.clear();
    searchPending_ = false;
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

        if (!equalConstantTime(msg->proof, protocol::computeProof(*code_, from, selfId_))) {
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

    if (ip::isGroupDestination(hdr->dst) && protectServices_ && filter::isDiscoveryBroadcast(packet))
        return; // ogłoszenia usług systemu (nazwa komputera, udostępnione zasoby) zostają u nas

    std::shared_lock lk(mutex_);
    if (ip::isGroupDestination(hdr->dst)) {
        for (auto& [addr, id] : routes_) sendTo(id, packet, protocol::kChannelData);
        return;
    }
    const auto it = routes_.find(hdr->dst.value());
    if (it != routes_.end()) sendTo(it->second, packet, protocol::kChannelData);
}

void Network::handleData(uint64_t from, std::span<const uint8_t> data) {
    if (data.size() > 65535) return;
    const auto hdr = ip::parseIPv4(data);
    if (!hdr) return;
    if (hdr->dst != selfAddress_ && !ip::isGroupDestination(hdr->dst)) return;
    {
        std::shared_lock lk(mutex_);
        const auto it = routes_.find(hdr->src.value());
        // nadawca musi być zweryfikowany i nie może podszywać się pod cudzy adres
        if (it == routes_.end() || it->second != from) return;
    }
    if (protectServices_) {
        const auto res = filter::inspectInbound(data);
        if (res.verdict != filter::Verdict::Allow) {
            ++blockedPackets_;
            std::lock_guard lk(blockedLogMutex_);
            if (blockedLogged_.size() < 1000 && blockedLogged_.insert({from, res.dstPort}).second) {
                const char* why = res.verdict == filter::Verdict::BlockedService ? "chroniona usługa"
                                : res.verdict == filter::Verdict::BlockedProtocol ? "niedozwolony protokół"
                                : "podejrzany fragment pakietu";
                KLOG_WARN("Zablokowano ruch od " << nameOf(from) << " (" << hdr->src.toString() << ") do "
                          << filter::serviceName(res.dstPort) << " – " << why);
            }
            return;
        }
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
    if (now >= nextMergeCheck_ && !searchPending_) {
        nextMergeCheck_ = now + kMergeInterval;
        startSearch(true);
    }
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

// ---------------------------------------------------------------- prywatność

void Network::setHideAddress(bool hide) {
    // Bez ICE Steam nie wymienia z drugą stroną żadnych adresów IP – zostaje tylko relay Valve.
    // Wyłączenie przywraca domyślne zachowanie z ustawień klienta Steam.
    SteamNetworkingUtils()->SetGlobalConfigValueInt32(
        k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable,
        hide ? k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Disable
             : k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Default);
    KLOG_INFO((hide ? "Prywatność: połączenia tylko przez relay Steam (adres IP ukryty)"
                    : "Prywatność: dozwolone połączenia bezpośrednie"));
    // Istniejące połączenia zestawiamy od nowa, żeby zmiana objęła także je.
    std::vector<uint64_t> ids;
    {
        std::shared_lock lk(mutex_);
        for (auto& [id, _] : peers_) ids.push_back(id);
    }
    for (uint64_t id : ids) SteamNetworkingMessages()->CloseSessionWithUser(identityFor(id));
}

// ---------------------------------------------------------------- informacje

std::string Network::codeText() const { return code_ ? netcode::format(*code_) : std::string(); }

std::string Network::networkName() const {
    if (!lobby_.IsValid()) return {};
    // nazwę ustawia osoba, która otworzyła sieć – tekst od obcych
    const std::string clean = text::sanitize(SteamMatchmaking()->GetLobbyData(lobby_, kKeyName), 60);
    return clean.empty() ? std::string("Sieć KettleLAN") : clean;
}

std::vector<PeerInfo> Network::peers() const {
    std::vector<PeerInfo> out;
    if (state_ != State::Connected) return out;
    const uint64_t owner = SteamMatchmaking()->GetLobbyOwner(lobby_).ConvertToUint64();

    PeerInfo self;
    self.steamId = selfId_;
    self.name = text::sanitize(SteamFriends()->GetPersonaName(), 32);
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

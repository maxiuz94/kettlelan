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

} // namespace

Network::Network(uint64_t selfSteamId, EventHandler onEvent)
    : selfId_(selfSteamId), selfAddress_(ip::addressForSteamId(selfSteamId)), onEvent_(std::move(onEvent)),
      router_(selfSteamId) {}

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
    router_.setCode(code_);
    SteamMatchmaking()->SetLobbyMemberData(lobby_, "ip", selfAddress_.toString().c_str());
    state_ = State::Connected;
    nextMergeCheck_ = Clock::now() + kFirstMergeCheck;
    syncMembers();
    notify("Połączono z siecią „" + networkName() + "”. Twój adres: " + selfAddress_.toString() +
           ", kod sieci: " + codeText());
}

void Network::closeAllSessions() {
    for (const auto& peer : router_.peers()) SteamNetworkingMessages()->CloseSessionWithUser(identityFor(peer.steamId));
}

void Network::leaveLobbyOnly() {
    if (lobby_.IsValid()) SteamMatchmaking()->LeaveLobby(lobby_);
    closeAllSessions();
    const auto code = router_.code();
    router_.clear();
    router_.setCode(code); // przy łączeniu dwóch lobby kod zostaje ten sam
    lobby_ = CSteamID();
}

void Network::leave() {
    if (state_ == State::Idle) return;
    leaveLobbyOnly();
    resetState();
    notify("Opuszczono sieć.");
}

void Network::resetState() {
    router_.clear();
    reportedBad_.clear();
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
    for (int i = 0; i < n; ++i) current.push_back(SteamMatchmaking()->GetLobbyMemberByIndex(lobby_, i).ConvertToUint64());

    std::set<uint64_t> verifiedBefore;
    for (const auto& p : router_.peers())
        if (p.verified) verifiedBefore.insert(p.steamId);

    for (uint64_t id : router_.syncMembers(current)) {
        SteamNetworkingMessages()->CloseSessionWithUser(identityFor(id));
        reportedBad_.erase(id);
        if (verifiedBefore.count(id)) notify(nameOf(id) + " opuścił(a) sieć.");
    }
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

// ---------------------------------------------------------------- HELLO (logika w router::Router)

void Network::sendHello(uint64_t to, bool ack) {
    auto msg = router_.makeHello(to, ack);
    if (!msg) return;
#ifdef KETTLELAN_SECURITY_TESTS
    if (debugWrongKey_) msg->proof ^= 0x5A5A5A5A5A5A5A5Aull; // test: udajemy, że nie znamy klucza
#endif
    sendTo(to, protocol::encode(*msg), protocol::kChannelControl);
}

void Network::handleControl(uint64_t from, std::span<const uint8_t> data) {
    if (state_ != State::Connected) return;
    const auto msg = protocol::decode(data);
    if (!msg) return;

    // HELLO może przyjść, zanim Steam powiadomi nas o nowym członku lobby.
    if (!router_.isMember(from) && isLobbyMember(from)) syncMembers();

    using router::HelloResult;
    const HelloResult res = router_.onHello(from, *msg);
    switch (res) {
    case HelloResult::Verified:
        notify(nameOf(from) + " dołączył(a) – adres " + ip::addressForSteamId(from).toString());
        break;
    case HelloResult::Conflict:
        if (reportedBad_.insert(from).second)
            notify(nameOf(from) + " ma ten sam adres co ktoś inny w sieci – ruch do tej osoby nie będzie działał.");
        break;
    case HelloResult::BadProof:
        if (reportedBad_.insert(from).second)
            notify(nameOf(from) + " jest w lobby, ale nie zna klucza sieci – ignoruję.");
        return;
    case HelloResult::BadVersion:
        if (reportedBad_.insert(from).second)
            notify(nameOf(from) + " używa innej wersji KettleLAN – zaktualizujcie program.");
        return;
    case HelloResult::BadAddress:
        if (reportedBad_.insert(from).second)
            KLOG_WARN(nameOf(from) << " podał(a) cudzy adres przy weryfikacji – ignoruję.");
        return;
    case HelloResult::Repeated:
        break;
    case HelloResult::UnknownPeer:
    case HelloResult::NoNetwork:
        return;
    }
    if (msg->type == protocol::MessageType::Hello) sendHello(from, true);
}

// ---------------------------------------------------------------- dane (logika w router::Router)

void Network::sendTo(uint64_t steamId, std::span<const uint8_t> data, int channel) {
    const EResult r = SteamNetworkingMessages()->SendMessageToUser(
        identityFor(steamId), data.data(), uint32(data.size()),
        channel == protocol::kChannelData ? kDataFlags : kControlFlags, channel);
    if (r != k_EResultOK && r != k_EResultNoConnection) KLOG_DEBUG("Wysyłka do " << steamId << ": " << int(r));
}

void Network::sendPacket(std::span<const uint8_t> packet) {
    if (state_ != State::Connected) return;
    for (uint64_t id : router_.recipients(packet)) sendTo(id, packet, protocol::kChannelData);
}

void Network::handleData(uint64_t from, std::span<const uint8_t> data) {
    filter::Result fr;
    const auto res = router_.onData(from, data, &fr);
    if (res == router::InboundResult::Filtered) {
        ++blockedPackets_;
        std::lock_guard lk(blockedLogMutex_);
        if (blockedLogged_.size() < 1000 && blockedLogged_.insert({from, fr.dstPort}).second) {
            const char* why = fr.verdict == filter::Verdict::BlockedService ? "chroniona usługa"
                            : fr.verdict == filter::Verdict::BlockedProtocol ? "niedozwolony protokół"
                            : "podejrzany fragment pakietu";
            KLOG_WARN("Zablokowano ruch od " << nameOf(from) << " do " << filter::serviceName(fr.dstPort) << " – " << why);
        }
        return;
    }
    if (res == router::InboundResult::Spoofed) {
        ++blockedPackets_;
        std::lock_guard lk(blockedLogMutex_);
        if (blockedLogged_.insert({from, 0xFFFF}).second)
            KLOG_WARN("Odrzucono pakiety od " << nameOf(from) << " z cudzym adresem nadawcy (podszywanie się)");
        return;
    }
    if (res != router::InboundResult::Deliver) return;
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
    for (uint64_t id : router_.dueHellos(now, kHelloInterval, kHelloAttempts)) sendHello(id, false);
}

#ifdef KETTLELAN_SECURITY_TESTS
int Network::debugSendSpoofed(ip::Address4 fakeSource) {
    int sent = 0;
    for (const auto& peer : router_.peers()) {
        if (!peer.verified) continue;
        // ICMP echo request: nagłówek IPv4 (20 B) + ICMP (8 B)
        std::vector<uint8_t> p(28, 0);
        p[0] = 0x45; p[3] = 28; p[8] = 64; p[9] = 1;
        const auto s = fakeSource.bytes(), d = peer.address.bytes();
        for (int i = 0; i < 4; ++i) { p[12 + i] = s[i]; p[16 + i] = d[i]; }
        auto checksum = [](const uint8_t* b, size_t n) {
            uint32_t sum = 0;
            for (size_t i = 0; i + 1 < n; i += 2) sum += uint32_t(b[i] << 8 | b[i + 1]);
            while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
            return uint16_t(~sum);
        };
        const uint16_t ipSum = checksum(p.data(), 20);
        p[10] = uint8_t(ipSum >> 8); p[11] = uint8_t(ipSum);
        p[20] = 8; // echo request
        p[24] = 0x4B; p[25] = 0x4C; // identyfikator „KL”
        const uint16_t icmpSum = checksum(p.data() + 20, 8);
        p[22] = uint8_t(icmpSum >> 8); p[23] = uint8_t(icmpSum);
        sendTo(peer.steamId, p, protocol::kChannelData);
        ++sent;
    }
    return sent;
}
#endif

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
    closeAllSessions(); // istniejące połączenia zestawią się od nowa z nowym ustawieniem
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

    for (const auto& peer : router_.peers()) {
        PeerInfo p;
        p.steamId = peer.steamId;
        p.name = nameOf(peer.steamId);
        p.address = peer.address;
        p.owner = owner == peer.steamId;
        p.verified = peer.verified;
        p.conflict = peer.conflict;
        SteamNetConnectionInfo_t info{};
        SteamNetConnectionRealTimeStatus_t status{};
        const auto st = SteamNetworkingMessages()->GetSessionConnectionInfo(identityFor(peer.steamId), &info, &status);
        if (st == k_ESteamNetworkingConnectionState_Connected) {
            p.pingMs = status.m_nPing;
            p.relayed = (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) != 0;
        }
        out.push_back(p);
    }
    return out;
}

} // namespace kettle::steam

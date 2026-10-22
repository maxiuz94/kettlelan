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
constexpr int kHelloAttempts = 15; // about 30 s of attempts
constexpr int kDataFlags = k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_AutoRestartBrokenSession;
constexpr int kControlFlags = k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_AutoRestartBrokenSession;

constexpr auto kFirstMergeCheck = std::chrono::seconds(5);
constexpr auto kMergeInterval = std::chrono::seconds(30);
constexpr int kMaxJoinRetries = 3;

constexpr const char* kKeyMarker = "kettlelan";
constexpr const char* kMarkerValue = "3"; // lobby format of KettleLAN 0.3
constexpr const char* kKeyTag = "tag";
constexpr const char* kKeyName = "name";
constexpr const char* kKeyProto = "proto";

SteamNetworkingIdentity identityFor(uint64_t steamId) {
    SteamNetworkingIdentity id;
    id.SetSteamID64(steamId);
    return id;
}

// Another person's name – text from strangers, so always cleaned of control characters.
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

void Network::notify(Msg id, std::vector<std::string> args) {
    const Message message{id, std::move(args)};
    KLOG_INFO(english(message));
    if (onEvent_) onEvent_(message);
}

// ---------------------------------------------------------------- creating / connecting
//
// A network has no permanent lobby. The code defines a tag; when connecting we search Steam for a
// lobby with that tag. If nobody is there, we open a new lobby with the same tag.
// If two lobbies are created at once, everyone moves to the one with the lowest id.

void Network::create(const std::string& name) {
    connect(netcode::generate(), name.empty() ? (std::string(SteamFriends()->GetPersonaName()) + "'s network") : name);
}

void Network::connect(const netcode::NetworkCode& code, const std::string& nameHint) {
    if (state_ != State::Idle) {
        notify(Msg::AlreadyInNetwork);
        return;
    }
    code_ = code;
    const auto started = Clock::now();
    keys_ = netkeys::derive(code); // deliberately slow (Argon2id) – once per connect
    KLOG_INFO("Network keys derived in "
               << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count() << " ms");
    tag_ = keys_->tag;
    nameHint_ = nameHint;
    joinRetries_ = 0;
    state_ = State::Connecting;
    notify(Msg::Connecting, {netcode::format(code)});
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
    if (!code_) return; // the network was left in the meantime

    // lowest lobby id with our tag (the tag is checked again)
    uint64_t best = 0;
    const uint32 count = ioFailure ? 0 : r->m_nLobbiesMatching;
    for (uint32 i = 0; i < count; ++i) {
        const CSteamID id = SteamMatchmaking()->GetLobbyByIndex(int(i));
        if (std::string(SteamMatchmaking()->GetLobbyData(id, kKeyTag)) != tag_) continue;
        if (best == 0 || id.ConvertToUint64() < best) best = id.ConvertToUint64();
    }

    if (merge) {
        if (state_ != State::Connected || best == 0 || best >= lobby_.ConvertToUint64()) return;
        notify(Msg::Merging);
        leaveLobbyOnly();
        state_ = State::Connecting;
        enterLobby(CSteamID(uint64(best)));
        return;
    }

    if (state_ != State::Connecting) return;
    if (best != 0) {
        enterLobby(CSteamID(uint64(best)));
    } else {
        // nobody is there – open the network
        createCall_.Set(SteamMatchmaking()->CreateLobby(k_ELobbyTypeInvisible, kMaxMembers), this,
                        &Network::onLobbyCreated);
    }
}

void Network::onLobbyCreated(LobbyCreated_t* r, bool ioFailure) {
    if (state_ != State::Connecting || !code_) return;
    if (ioFailure || r->m_eResult != k_EResultOK) {
        resetState();
        notify(Msg::OpenFailed, {std::to_string(ioFailure ? -1 : int(r->m_eResult))});
        return;
    }
    lobby_ = CSteamID(uint64(r->m_ulSteamIDLobby));
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyMarker, kMarkerValue);
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyTag, tag_.c_str());
    const std::string name = text::sanitize(nameHint_, 60);
    // encrypted: only people who know the code can read the network's name
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyName,
                                     netkeys::sealName(*keys_, name.empty() ? std::string("KettleLAN network") : name).c_str());
    SteamMatchmaking()->SetLobbyData(lobby_, kKeyProto, std::to_string(protocol::kVersion).c_str());
    // The creator is in the lobby right away – Steam does not send a separate join result.
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
        // The lobby may have disappeared between search and join – search again.
        lobby_ = CSteamID();
        if (++joinRetries_ <= kMaxJoinRetries) {
            startSearch(false);
            return;
        }
        resetState();
        notify(Msg::JoinFailed, {std::to_string(ioFailure ? 0u : r->m_EChatRoomEnterResponse)});
        return;
    }
    if (std::string(SteamMatchmaking()->GetLobbyData(lobby_, kKeyTag)) != tag_) {
        SteamMatchmaking()->LeaveLobby(lobby_);
        resetState();
        notify(Msg::WrongLobby);
        return;
    }
    enterConnected();
}

void Network::enterConnected() {
    router_.setNetwork(*code_, *keys_);
    SteamMatchmaking()->SetLobbyMemberData(lobby_, "ip", selfAddress_.toString().c_str());
    state_ = State::Connected;
    nextMergeCheck_ = Clock::now() + kFirstMergeCheck;
    syncMembers();
    notify(Msg::Connected, {networkName(), selfAddress_.toString(), codeText()});
}

void Network::closeAllSessions() {
    for (const auto& peer : router_.peers()) SteamNetworkingMessages()->CloseSessionWithUser(identityFor(peer.steamId));
}

void Network::leaveLobbyOnly() {
    if (lobby_.IsValid()) SteamMatchmaking()->LeaveLobby(lobby_);
    closeAllSessions();
    router_.clear();
    if (code_ && keys_) router_.setNetwork(*code_, *keys_); // when merging two lobbies the code stays the same
    lobby_ = CSteamID();
}

void Network::leave() {
    if (state_ == State::Idle) return;
    leaveLobbyOnly();
    resetState();
    notify(Msg::Left);
}

void Network::resetState() {
    router_.clear();
    reportedBad_.clear();
    lobby_ = CSteamID();
    code_.reset();
    keys_.reset();
    tag_.clear();
    searchPending_ = false;
    state_ = State::Idle;
}

// ---------------------------------------------------------------- members

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
        if (verifiedBefore.count(id)) notify(Msg::PeerLeft, {nameOf(id)});
    }
}

void Network::onLobbyChatUpdate(LobbyChatUpdate_t* ev) {
    if (state_ != State::Connected || ev->m_ulSteamIDLobby != lobby_.ConvertToUint64()) return;
    syncMembers();
}

// ---------------------------------------------------------------- Steam Networking sessions

void Network::onSessionRequest(SteamNetworkingMessagesSessionRequest_t* ev) {
    const uint64_t from = ev->m_identityRemote.GetSteamID64();
    // Accept connections only from people in our lobby.
    if (state_ == State::Connected && isLobbyMember(from)) {
        SteamNetworkingMessages()->AcceptSessionWithUser(ev->m_identityRemote);
    } else {
        KLOG_DEBUG("Rejected session from " << from);
    }
}

void Network::onSessionFailed(SteamNetworkingMessagesSessionFailed_t* ev) {
    KLOG_WARN("Connection to " << nameOf(ev->m_info.m_identityRemote.GetSteamID64())
              << " lost: " << ev->m_info.m_szEndDebug);
}

// ---------------------------------------------------------------- HELLO (logic in router::Router)

void Network::sendHello(uint64_t to, bool ack) {
    auto msg = router_.makeHello(to, ack);
    if (!msg) return;
#ifdef KETTLELAN_SECURITY_TESTS
    if (debugWrongKey_) msg->proof ^= 0x5A5A5A5A5A5A5A5Aull; // test: pretend we do not know the key
#endif
    sendTo(to, protocol::encode(*msg), protocol::kChannelControl);
}

void Network::handleControl(uint64_t from, std::span<const uint8_t> data) {
    if (state_ != State::Connected) return;
    const auto msg = protocol::decode(data);
    if (!msg) return;

    // HELLO may arrive before Steam tells us about a new lobby member.
    if (!router_.isMember(from) && isLobbyMember(from)) syncMembers();

    using router::HelloResult;
    const HelloResult res = router_.onHello(from, *msg);
    switch (res) {
    case HelloResult::Verified:
        notify(Msg::PeerJoined, {nameOf(from), ip::addressForSteamId(from).toString()});
        break;
    case HelloResult::Conflict:
        if (reportedBad_.insert(from).second)
            notify(Msg::PeerConflict, {nameOf(from)});
        break;
    case HelloResult::BadProof:
        if (reportedBad_.insert(from).second)
            notify(Msg::PeerBadKey, {nameOf(from)});
        return;
    case HelloResult::BadVersion:
        if (reportedBad_.insert(from).second)
            notify(Msg::PeerBadVersion, {nameOf(from)});
        return;
    case HelloResult::BadAddress:
        if (reportedBad_.insert(from).second)
            KLOG_WARN(nameOf(from) << " announced someone else's address during verification – ignoring.");
        return;
    case HelloResult::Repeated:
        break;
    case HelloResult::UnknownPeer:
    case HelloResult::NoNetwork:
        return;
    }
    if (msg->type == protocol::MessageType::Hello) sendHello(from, true);
}

// ---------------------------------------------------------------- data (logic in router::Router)

void Network::sendTo(uint64_t steamId, std::span<const uint8_t> data, int channel) {
    const EResult r = SteamNetworkingMessages()->SendMessageToUser(
        identityFor(steamId), data.data(), uint32(data.size()),
        channel == protocol::kChannelData ? kDataFlags : kControlFlags, channel);
    if (r != k_EResultOK && r != k_EResultNoConnection) KLOG_DEBUG("Send to " << steamId << " failed: " << int(r));
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
            const char* why = fr.verdict == filter::Verdict::BlockedService ? "protected service"
                            : fr.verdict == filter::Verdict::BlockedProtocol ? "protocol not allowed"
                            : "suspicious IP fragment";
            KLOG_WARN("Blocked traffic from " << nameOf(from) << " to " << filter::serviceName(fr.dstPort) << " – " << why);
        }
        return;
    }
    if (res == router::InboundResult::Spoofed) {
        ++blockedPackets_;
        std::lock_guard lk(blockedLogMutex_);
        if (blockedLogged_.insert({from, 0xFFFF}).second)
            KLOG_WARN("Dropped packets from " << nameOf(from) << " with a forged source address (spoofing)");
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
        // ICMP echo request: IPv4 header (20 B) + ICMP (8 B)
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
        p[24] = 0x4B; p[25] = 0x4C; // identifier "KL"
        const uint16_t icmpSum = checksum(p.data() + 20, 8);
        p[22] = uint8_t(icmpSum >> 8); p[23] = uint8_t(icmpSum);
        sendTo(peer.steamId, p, protocol::kChannelData);
        ++sent;
    }
    return sent;
}
#endif

// ---------------------------------------------------------------- privacy

void Network::setHideAddress(bool hide) {
    // Without ICE, Steam exchanges no IP addresses with the other side – only Valve's relay is used.
    // Turning it off restores the default behaviour from the Steam client's settings.
    SteamNetworkingUtils()->SetGlobalConfigValueInt32(
        k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable,
        hide ? k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Disable
             : k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Default);
    KLOG_INFO((hide ? "Privacy: Steam relay only (IP address hidden)"
                    : "Privacy: direct connections allowed"));
    closeAllSessions(); // existing connections are re-established with the new setting
}

// ---------------------------------------------------------------- information

std::string Network::codeText() const { return code_ ? netcode::format(*code_) : std::string(); }

std::string Network::networkName() const {
    if (!lobby_.IsValid()) return {};
    // the name is set by whoever opened the network – encrypted, and still text from other people
    const auto name = keys_ ? netkeys::openName(*keys_, SteamMatchmaking()->GetLobbyData(lobby_, kKeyName)) : std::nullopt;
    const std::string clean = name ? text::sanitize(*name, 60) : std::string();
    return clean.empty() ? std::string("KettleLAN network") : clean;
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

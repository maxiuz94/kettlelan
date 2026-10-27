#include "network.h"
#include <algorithm>
#include <map>
#include "filter.h"
#include "log.h"
#include "protocol.h"
#include "text.h"
#include <algorithm>

namespace kettle::steam {

using Clock = std::chrono::steady_clock;

namespace {

constexpr int kMaxMembers = 250;
constexpr auto kHelloInterval = std::chrono::seconds(2);
constexpr int kHelloAttempts = 15; // about 30 s of attempts
// Keepalive: Steam closes idle sessions, and then no ping is known and the first game packet waits
// for a new session. A HELLO_ACK every 10 s keeps the session open (receivers treat it as a repeat).
constexpr auto kKeepaliveInterval = std::chrono::seconds(10);
constexpr int kDataFlags = k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_AutoRestartBrokenSession;
constexpr int kControlFlags = k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_AutoRestartBrokenSession;

constexpr auto kFirstMergeCheck = std::chrono::seconds(5);
constexpr auto kMergeInterval = std::chrono::seconds(30);
constexpr auto kOldVersionCheck = std::chrono::seconds(8);
constexpr int kMaxJoinRetries = 3;

constexpr const char* kKeyMarker = "kettlelan";
constexpr const char* kMarkerValue = "3"; // lobby format of KettleLAN 0.3
constexpr const char* kKeyTag = "tag";
constexpr const char* kKeyName = "name";
constexpr const char* kKeyProto = "proto";
constexpr const char* kKeyNick = "nick"; // member data: a member's nickname, encrypted
// KettleLAN 0.2 and early 0.3 builds wrote this when a network was opened without a name. It is
// treated as "name not known", so a member who knows the real name can put it back.
constexpr const char* kLegacyDefaultName = "KettleLAN network";
constexpr auto kNameRepairInterval = std::chrono::seconds(5);

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

// ---------------------------------------------------------------- one network = one Steam lobby
//
// A network has no permanent lobby. The code defines a tag; when connecting we search Steam for a
// lobby with that tag. If nobody is there, we open a new lobby with the same tag.
// If two lobbies are created at once, everyone moves to the one with the lowest id.

class Network::Lobby {
public:
    Lobby(Network& hub, NetworkHandle id, const netcode::NetworkCode& code, netkeys::Keys keys, std::string nameHint)
        : hub_(hub), id_(id), code_(code), keys_(std::move(keys)), knownName_(text::sanitize(nameHint, 60)) {
        if (knownName_ == kLegacyDefaultName) knownName_.clear();
    }

    NetworkHandle id() const { return id_; }
    State state() const { return state_; }
    CSteamID lobby() const { return lobby_; }
    const netcode::NetworkCode& code() const { return code_; }
    bool alive() const { return alive_; }

    void start() { search(false); }

    void leave() {
        alive_ = false;
        if (lobby_.IsValid()) SteamMatchmaking()->LeaveLobby(lobby_);
        lobby_ = CSteamID();
    }

    void tick(Clock::time_point now) {
        if (state_ != State::Connected) return;
        if (now >= nextMergeCheck_ && !searchPending_) {
            nextMergeCheck_ = now + kMergeInterval;
            search(true);
        }
        if (!oldVersionChecked_ && now >= oldVersionCheckAt_) {
            oldVersionChecked_ = true;
            checkOldVersion();
        }
        if (now >= nextNameRepair_) {
            nextNameRepair_ = now + kNameRepairInterval;
            adoptLobbyName();
            repairName();
        }
    }

    bool isMember(uint64_t steamId) const {
        if (!lobby_.IsValid() || state_ != State::Connected) return false;
        const int n = SteamMatchmaking()->GetNumLobbyMembers(lobby_);
        for (int i = 0; i < n; ++i)
            if (SteamMatchmaking()->GetLobbyMemberByIndex(lobby_, i).ConvertToUint64() == steamId) return true;
        return false;
    }

    void syncMembers() {
        if (state_ != State::Connected) return;
        std::vector<uint64_t> current;
        const int n = SteamMatchmaking()->GetNumLobbyMembers(lobby_);
        for (int i = 0; i < n; ++i) current.push_back(SteamMatchmaking()->GetLobbyMemberByIndex(lobby_, i).ConvertToUint64());
        applyMembers(current);
    }

    // The network's shared name: the one in the lobby data, else the one most members know (member
    // data), else empty ("not known yet"). Encrypted, and still text from other people.
    std::string name() const {
        if (!lobby_.IsValid()) return knownName_;
        const std::string fromLobby = lobbyName();
        if (!fromLobby.empty()) return fromLobby;
        const std::string fromMembers = membersName();
        return fromMembers.empty() ? knownName_ : fromMembers;
    }

    // Our nickname in this lobby's member data (encrypted); empty: the Steam name is used.
    void publishNick() {
        if (!lobby_.IsValid() || state_ != State::Connected) return;
        SteamMatchmaking()->SetLobbyMemberData(lobby_, kKeyNick,
                                               hub_.nick_.empty() ? "" : netkeys::sealNick(keys_, hub_.nick_).c_str());
    }

    // A member's nickname from this lobby, "" when none (text from other people – cleaned).
    std::string nickOf(uint64_t steamId) const {
        if (!lobby_.IsValid() || state_ != State::Connected) return {};
        const char* sealed = SteamMatchmaking()->GetLobbyMemberData(lobby_, CSteamID(uint64(steamId)), kKeyNick);
        if (!sealed || !*sealed) return {};
        const auto nick = netkeys::openNick(keys_, sealed);
        return nick ? text::sanitize(*nick, 32) : std::string();
    }

    bool isOwner() const {
        return lobby_.IsValid() && SteamMatchmaking()->GetLobbyOwner(lobby_).ConvertToUint64() == hub_.selfId_;
    }

    // Renames the network for everyone – only the lobby's owner (the host, ★) can. Steam makes someone
    // else the owner when the host leaves.
    bool rename(const std::string& newName) {
        const std::string clean = text::sanitize(newName, 60);
        if (state_ != State::Connected || !isOwner() || clean.empty()) return false;
        knownName_ = clean;
        SteamMatchmaking()->SetLobbyData(lobby_, kKeyName, netkeys::sealName(keys_, clean).c_str());
        publishKnownName();
        return true;
    }

private:
    void applyMembers(const std::vector<uint64_t>& current) {
        std::set<uint64_t> verifiedBefore;
        for (const auto& p : hub_.router_.peers(id_))
            if (p.verified) verifiedBefore.insert(p.steamId);
        const auto sync = hub_.router_.syncMembers(id_, current);
        hub_.closeSessions(sync.goneEverywhere);
        for (uint64_t id : sync.left)
            if (verifiedBefore.count(id)) hub_.notify(Msg::PeerLeft, {hub_.personName(id), hub_.displayName(*this)});
    }

    void search(bool merge) {
        searchPending_ = true;
        hub_.searchLobbies(this, keys_.tag, [this, merge](const std::vector<uint64_t>& found) { onSearch(found, merge); });
    }

    void onSearch(const std::vector<uint64_t>& found, bool merge) {
        searchPending_ = false;
        if (!alive_) return; // the network was left in the meantime
        // lowest lobby id with our tag (the tag is checked again)
        uint64_t best = 0;
        for (uint64_t raw : found) {
            const CSteamID id(uint64{raw});
            if (std::string(SteamMatchmaking()->GetLobbyData(id, kKeyTag)) != keys_.tag) continue;
            if (best == 0 || raw < best) best = raw;
        }

        if (merge) {
            if (state_ != State::Connected || best == 0 || best >= lobby_.ConvertToUint64()) return;
            hub_.notify(Msg::Merging);
            SteamMatchmaking()->LeaveLobby(lobby_);
            // everyone moves to the other lobby: forget the old member list without "left" messages
            hub_.closeSessions(hub_.router_.syncMembers(id_, {}).goneEverywhere);
            state_ = State::Connecting;
            enter(CSteamID(uint64{best}));
            return;
        }

        if (state_ != State::Connecting) return;
        if (best != 0) {
            enter(CSteamID(uint64{best}));
        } else {
            // nobody is there – open the network
            createCall_.Set(SteamMatchmaking()->CreateLobby(k_ELobbyTypeInvisible, kMaxMembers), this, &Lobby::onCreated);
        }
    }

    void onCreated(LobbyCreated_t* r, bool ioFailure) {
        if (!alive_ || state_ != State::Connecting) {
            if (!ioFailure && r->m_eResult == k_EResultOK) SteamMatchmaking()->LeaveLobby(CSteamID(uint64(r->m_ulSteamIDLobby)));
            return;
        }
        if (ioFailure || r->m_eResult != k_EResultOK) {
            hub_.notify(Msg::OpenFailed, {std::to_string(ioFailure ? -1 : int(r->m_eResult))});
            hub_.removeLobby(this);
            return;
        }
        lobby_ = CSteamID(uint64(r->m_ulSteamIDLobby));
        SteamMatchmaking()->SetLobbyData(lobby_, kKeyMarker, kMarkerValue);
        SteamMatchmaking()->SetLobbyData(lobby_, kKeyTag, keys_.tag.c_str());
        // encrypted: only people who know the code can read the network's name. Without a known name
        // the lobby stays unnamed until a member who knows it arrives (see repairName).
        if (!knownName_.empty())
            SteamMatchmaking()->SetLobbyData(lobby_, kKeyName, netkeys::sealName(keys_, knownName_).c_str());
        SteamMatchmaking()->SetLobbyData(lobby_, kKeyProto, std::to_string(protocol::kVersion).c_str());
        // The creator is in the lobby right away – Steam does not send a separate join result.
        connected();
    }

    void enter(CSteamID id) {
        lobby_ = id;
        enterCall_.Set(SteamMatchmaking()->JoinLobby(lobby_), this, &Lobby::onEntered);
    }

    void onEntered(LobbyEnter_t* r, bool ioFailure) {
        if (!alive_ || state_ != State::Connecting) return;
        const bool ok = !ioFailure && r->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess;
        if (!ok) {
            // The lobby may have disappeared between search and join – search again.
            lobby_ = CSteamID();
            if (++joinRetries_ <= kMaxJoinRetries) {
                search(false);
                return;
            }
            hub_.notify(Msg::JoinFailed, {std::to_string(ioFailure ? 0u : r->m_EChatRoomEnterResponse)});
            hub_.removeLobby(this);
            return;
        }
        if (std::string(SteamMatchmaking()->GetLobbyData(lobby_, kKeyTag)) != keys_.tag) {
            hub_.notify(Msg::WrongLobby);
            hub_.removeLobby(this);
            return;
        }
        connected();
    }

    void connected() {
        SteamMatchmaking()->SetLobbyMemberData(lobby_, "ip", hub_.selfAddress_.toString().c_str());
        adoptLobbyName();
        publishKnownName();
        state_ = State::Connected;
        publishNick(); // needs the Connected state – also when the nickname was set before joining
        const auto now = Clock::now();
        nextMergeCheck_ = now + kFirstMergeCheck;
        oldVersionCheckAt_ = now + kOldVersionCheck;
        syncMembers();
        hub_.notify(Msg::Connected, {hub_.displayName(*this), hub_.selfAddress_.toString(), netcode::format(code_)});
    }

    std::string lobbyName() const {
        const auto name = netkeys::openName(keys_, SteamMatchmaking()->GetLobbyData(lobby_, kKeyName));
        const std::string clean = name ? text::sanitize(*name, 60) : std::string();
        return clean == kLegacyDefaultName ? std::string() : clean;
    }

    // The name most members announce in their member data (ties: the first one seen).
    std::string membersName() const {
        std::map<std::string, int> votes;
        std::string best;
        const int n = SteamMatchmaking()->GetNumLobbyMembers(lobby_);
        for (int i = 0; i < n; ++i) {
            const CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(lobby_, i);
            const auto name = netkeys::openName(keys_, SteamMatchmaking()->GetLobbyMemberData(lobby_, member, kKeyName));
            const std::string clean = name ? text::sanitize(*name, 60) : std::string();
            if (clean.empty() || clean == kLegacyDefaultName) continue;
            if (++votes[clean] > (best.empty() ? 0 : votes[best])) best = clean;
        }
        return best;
    }

    // The lobby's name wins: remember it (and announce it) when it differs from ours.
    void adoptLobbyName() {
        const std::string fromLobby = lobbyName();
        if (!fromLobby.empty() && fromLobby != knownName_) {
            knownName_ = fromLobby;
            publishKnownName();
        }
    }

    // Our known name in our member data, so the lobby owner can repair an unnamed lobby.
    void publishKnownName() {
        if (!lobby_.IsValid() || knownName_.empty()) return;
        SteamMatchmaking()->SetLobbyMemberData(lobby_, kKeyName, netkeys::sealName(keys_, knownName_).c_str());
    }

    // The owner of an unnamed lobby takes the name its members know.
    void repairName() {
        if (!isOwner() || !lobbyName().empty()) return;
        const std::string fromMembers = membersName();
        const std::string name = !knownName_.empty() ? knownName_ : fromMembers;
        if (name.empty()) return;
        knownName_ = name;
        SteamMatchmaking()->SetLobbyData(lobby_, kKeyName, netkeys::sealName(keys_, name).c_str());
    }

    // KettleLAN 0.2 used another tag for the same code: if such a lobby exists, someone is in it
    // (Steam removes empty lobbies) and cannot see this network.
    void checkOldVersion() {
        const std::string legacy = protocol::legacyLobbyTag(code_);
        hub_.searchLobbies(this, legacy, [this, legacy](const std::vector<uint64_t>& found) {
            if (!alive_) return;
            for (uint64_t raw : found) {
                if (std::string(SteamMatchmaking()->GetLobbyData(CSteamID(uint64{raw}), kKeyTag)) == legacy) {
                    hub_.notify(Msg::OldVersionInNetwork, {hub_.displayName(*this)});
                    return;
                }
            }
        });
    }

    Network& hub_;
    const NetworkHandle id_;
    const netcode::NetworkCode code_;
    const netkeys::Keys keys_; // derived once per connect (Argon2id)
    std::string knownName_; // the shared name as far as we know it ("" = not known)

    State state_ = State::Connecting;
    bool alive_ = true;
    CSteamID lobby_;
    bool searchPending_ = false;
    int joinRetries_ = 0;
    bool oldVersionChecked_ = false;
    Clock::time_point nextMergeCheck_{};
    Clock::time_point oldVersionCheckAt_{};
    Clock::time_point nextNameRepair_{};
    CCallResult<Lobby, LobbyCreated_t> createCall_;
    CCallResult<Lobby, LobbyEnter_t> enterCall_;
};

// ---------------------------------------------------------------- hub

Network::Network(uint64_t selfSteamId, EventHandler onEvent)
    : selfId_(selfSteamId), selfAddress_(ip::addressForSteamId(selfSteamId)), onEvent_(std::move(onEvent)),
      router_(selfSteamId) {}

Network::~Network() { leaveAll(); }

void Network::notify(Msg id, std::vector<std::string> args) {
    const Message message{id, std::move(args)};
    KLOG_INFO(english(message));
    if (onEvent_) onEvent_(message);
}

ConnectResult Network::create(const std::string& name, NetworkHandle* handle) {
    return connect(netcode::generate(), name.empty() ? (text::sanitize(SteamFriends()->GetPersonaName(), 32) + "'s network") : name,
                   handle);
}

ConnectResult Network::connect(const netcode::NetworkCode& code, const std::string& nameHint, NetworkHandle* handle) {
    for (const auto& l : lobbies_) {
        if (l->code() == code) {
            notify(Msg::AlreadyInThisNetwork, {netcode::format(code)});
            if (handle) *handle = l->id();
            return ConnectResult::AlreadyConnected;
        }
    }
    if (router_.networks().size() >= router_.maxNetworks()) { // before the slow key derivation
        notify(Msg::NetworkLimit, {std::to_string(router_.maxNetworks())});
        return ConnectResult::LimitReached;
    }
    const auto started = Clock::now();
    netkeys::Keys keys = netkeys::derive(code); // deliberately slow (Argon2id) – once per connect
    KLOG_INFO("Network keys derived in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count() << " ms");
    const auto id = router_.addNetwork(code, keys);
    if (!id) {
        notify(Msg::NetworkLimit, {std::to_string(router_.maxNetworks())});
        return ConnectResult::LimitReached;
    }
    notify(Msg::Connecting, {netcode::format(code)});
    lobbies_.push_back(std::make_unique<Lobby>(*this, *id, code, std::move(keys), nameHint));
    if (handle) *handle = *id;
    lobbies_.back()->start();
    return ConnectResult::Started;
}

Network::Lobby* Network::lobbyFor(NetworkHandle network) const {
    for (const auto& l : lobbies_)
        if (l->id() == network) return l.get();
    return nullptr;
}

bool Network::inAnyLobby(uint64_t steamId) const {
    for (const auto& l : lobbies_)
        if (l->isMember(steamId)) return true;
    return false;
}

void Network::removeLobby(Lobby* lobby) {
    const auto it = std::find_if(lobbies_.begin(), lobbies_.end(), [lobby](const auto& l) { return l.get() == lobby; });
    if (it == lobbies_.end()) return;
    lobby->leave();
    closeSessions(router_.removeNetwork(lobby->id()));
    // drop its queued searches; one already sent to Steam only loses its callback
    for (auto s = searches_.begin(); s != searches_.end();) {
        if (s->owner != lobby) { ++s; continue; }
        if (s == searches_.begin() && searching_) { s->done = nullptr; ++s; }
        else s = searches_.erase(s);
    }
    // Steam may still deliver results of this lobby's calls – keep the object until the end.
    retired_.push_back(std::move(*it));
    lobbies_.erase(it);
}

void Network::leave(NetworkHandle network) {
    Lobby* lobby = lobbyFor(network);
    if (!lobby) return;
    removeLobby(lobby);
    notify(Msg::Left, {});
}

void Network::leaveAll() {
    while (!lobbies_.empty()) leave(lobbies_.front()->id());
}

void Network::closeSessions(const std::vector<uint64_t>& people) {
    for (uint64_t id : people) {
        SteamNetworkingMessages()->CloseSessionWithUser(identityFor(id));
        reportedBad_.erase(id);
    }
}

// ---------------------------------------------------------------- lobby search queue

void Network::searchLobbies(const Lobby* owner, const std::string& tag, SearchDone done) {
    searches_.push_back({owner, tag, std::move(done)});
    if (!searching_) runNextSearch();
}

void Network::runNextSearch() {
    if (searches_.empty()) {
        searching_ = false;
        return;
    }
    searching_ = true;
    SteamMatchmaking()->AddRequestLobbyListStringFilter(kKeyTag, searches_.front().tag.c_str(), k_ELobbyComparisonEqual);
    SteamMatchmaking()->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
    SteamMatchmaking()->AddRequestLobbyListResultCountFilter(10);
    listCall_.Set(SteamMatchmaking()->RequestLobbyList(), this, &Network::onLobbyList);
}

void Network::onLobbyList(LobbyMatchList_t* r, bool ioFailure) {
    std::vector<uint64_t> found;
    const uint32 count = ioFailure ? 0 : r->m_nLobbiesMatching;
    for (uint32 i = 0; i < count; ++i) found.push_back(SteamMatchmaking()->GetLobbyByIndex(int(i)).ConvertToUint64());
    if (!searches_.empty()) {
        SearchDone done = std::move(searches_.front().done);
        searches_.pop_front();
        if (done) done(found);
    }
    runNextSearch();
}

// ---------------------------------------------------------------- members

void Network::onLobbyChatUpdate(LobbyChatUpdate_t* ev) {
    for (const auto& l : lobbies_)
        if (l->state() == State::Connected && l->lobby().ConvertToUint64() == ev->m_ulSteamIDLobby) l->syncMembers();
}

// ---------------------------------------------------------------- Steam Networking sessions

void Network::onSessionRequest(SteamNetworkingMessagesSessionRequest_t* ev) {
    const uint64_t from = ev->m_identityRemote.GetSteamID64();
    // Accept connections only from people in one of our lobbies whom we did not block.
    if (inAnyLobby(from) && !router_.isBlocked(from)) {
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

void Network::sendHello(NetworkHandle network, uint64_t to, bool ack) {
    auto msg = router_.makeHello(network, to, ack);
    if (!msg) return;
#ifdef KETTLELAN_SECURITY_TESTS
    if (debugWrongKey_) msg->proof ^= 0x5A5A5A5A5A5A5A5Aull; // test: pretend we do not know the key
#endif
    sendTo(to, protocol::encode(*msg), protocol::kChannelControl);
}

void Network::handleControl(uint64_t from, std::span<const uint8_t> data) {
    if (lobbies_.empty()) return;
    const auto msg = protocol::decode(data);
    if (!msg) return;

    // HELLO may arrive before Steam tells us about a new lobby member.
    if (!router_.isMember(from))
        for (const auto& l : lobbies_)
            if (l->isMember(from)) l->syncMembers();

    using router::HelloResult;
    const auto outcome = router_.onHello(from, *msg);
    const Lobby* lobby = lobbyFor(outcome.network);
    switch (outcome.result) {
    case HelloResult::Verified:
        notify(Msg::PeerJoined, {personName(from), ip::addressForSteamId(from).toString(), lobby ? displayName(*lobby) : std::string()});
        break;
    case HelloResult::Conflict:
        if (reportedBad_.insert(from).second)
            notify(Msg::PeerConflict, {personName(from)});
        break;
    case HelloResult::BadProof:
        if (reportedBad_.insert(from).second)
            notify(Msg::PeerBadKey, {personName(from)});
        return;
    case HelloResult::BadVersion:
        if (reportedBad_.insert(from).second)
            notify(Msg::PeerBadVersion, {personName(from)});
        return;
    case HelloResult::BadAddress:
        if (reportedBad_.insert(from).second)
            KLOG_WARN(nameOf(from) << " announced someone else's address during verification – ignoring.");
        return;
    case HelloResult::Repeated:
        break;
    case HelloResult::Blocked:
    case HelloResult::UnknownPeer:
    case HelloResult::NoNetwork:
        return;
    }
    if (msg->type == protocol::MessageType::Hello) sendHello(outcome.network, from, true);
}

// ---------------------------------------------------------------- data (logic in router::Router)

void Network::sendTo(uint64_t steamId, std::span<const uint8_t> data, int channel) {
    const EResult r = SteamNetworkingMessages()->SendMessageToUser(
        identityFor(steamId), data.data(), uint32(data.size()),
        channel == protocol::kChannelData ? kDataFlags : kControlFlags, channel);
    if (r != k_EResultOK && r != k_EResultNoConnection) KLOG_DEBUG("Send to " << steamId << " failed: " << int(r));
}

void Network::sendPacket(std::span<const uint8_t> packet) {
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
                const auto now = Clock::now();
                if (!router_.admit(from, now)) {
                    // a flood: dropped before anything else is done with it, reported every 30 s at most
                    ++blockedPackets_;
                    auto& last = floodLogged_[from];
                    if (last == Clock::time_point{} || now - last > std::chrono::seconds(30)) {
                        last = now;
                        KLOG_WARN("Too much traffic from " << nameOf(from) << " – dropping what is above "
                                  << int(router::Router::kRatePerSecond) << " messages a second");
                    }
                } else if (channel == protocol::kChannelControl) {
                    handleControl(from, data);
                } else {
                    handleData(from, data);
                }
                msgs[i]->Release();
            }
        }
    }
}

void Network::tick() {
    const auto now = Clock::now();
    // lobbies may remove themselves during tick (failed merge) – iterate over a copy
    std::vector<Lobby*> current;
    for (const auto& l : lobbies_) current.push_back(l.get());
    for (Lobby* l : current) l->tick(now);
    for (const auto& [network, id] : router_.dueHellos(now, kHelloInterval, kHelloAttempts)) sendHello(network, id, false);
    if (now >= nextKeepalive_) {
        nextKeepalive_ = now + kKeepaliveInterval;
        std::set<uint64_t> sent; // once per person, even when they are in several of our networks
        for (const auto network : router_.networks())
            for (const auto& peer : router_.peers(network))
                if (peer.verified && !peer.conflict && sent.insert(peer.steamId).second) sendHello(network, peer.steamId, true);
    }
    // forget left lobbies once Steam had time to deliver their pending results
    if (retired_.size() > 8) retired_.erase(retired_.begin(), retired_.begin() + long(retired_.size() - 8));
}

#ifdef KETTLELAN_SECURITY_TESTS
int Network::debugSendSpoofed(ip::Address4 fakeSource) {
    std::set<uint64_t> targets;
    for (const auto network : router_.networks())
        for (const auto& peer : router_.peers(network))
            if (peer.verified) targets.insert(peer.steamId);
    int sent = 0;
    for (uint64_t steamId : targets) {
        // ICMP echo request: IPv4 header (20 B) + ICMP (8 B)
        std::vector<uint8_t> p(28, 0);
        p[0] = 0x45; p[3] = 28; p[8] = 64; p[9] = 1;
        const auto s = fakeSource.bytes(), d = ip::addressForSteamId(steamId).bytes();
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
        sendTo(steamId, p, protocol::kChannelData);
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
    // existing connections are re-established with the new setting
    std::set<uint64_t> everyone;
    for (const auto network : router_.networks())
        for (const auto& peer : router_.peers(network)) everyone.insert(peer.steamId);
    for (uint64_t id : everyone) SteamNetworkingMessages()->CloseSessionWithUser(identityFor(id));
}

// ---------------------------------------------------------------- information

std::vector<PeerInfo> Network::peersOf(const Lobby& lobby) const {
    std::vector<PeerInfo> out;
    if (lobby.state() != State::Connected) return out;
    const uint64_t owner = SteamMatchmaking()->GetLobbyOwner(lobby.lobby()).ConvertToUint64();

    PeerInfo self;
    self.steamId = selfId_;
    self.name = nick_.empty() ? text::sanitize(SteamFriends()->GetPersonaName(), 32) : nick_;
    self.address = selfAddress_;
    self.self = true;
    self.verified = true;
    self.owner = owner == selfId_;
    out.push_back(self);

    for (const auto& peer : router_.peers(lobby.id())) {
        PeerInfo p;
        p.steamId = peer.steamId;
        p.name = personName(peer.steamId);
        p.steamFriend = SteamFriends()->HasFriend(CSteamID(uint64(peer.steamId)), k_EFriendFlagImmediate);
        if (p.steamFriend) p.steamName = nameOf(peer.steamId);
        p.address = peer.address;
        p.owner = owner == peer.steamId;
        p.verified = peer.verified;
        p.conflict = peer.conflict;
        p.blocked = peer.blocked;
        SteamNetConnectionInfo_t info{};
        SteamNetConnectionRealTimeStatus_t status{};
        const auto st = SteamNetworkingMessages()->GetSessionConnectionInfo(identityFor(peer.steamId), &info, &status);
        if (st == k_ESteamNetworkingConnectionState_Connected) {
            p.pingMs = status.m_nPing;
            p.relayed = (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) != 0;
        }
        out.push_back(p);
    }
    std::stable_sort(out.begin() + 1, out.end(), [](const PeerInfo& a, const PeerInfo& b) {
        auto rank = [](const PeerInfo& p) { return p.blocked ? 4 : p.conflict ? 3 : !p.verified ? 2 : p.pingMs < 0 ? 1 : 0; };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        if (a.verified && b.verified && a.pingMs >= 0 && b.pingMs >= 0 && a.pingMs != b.pingMs) return a.pingMs < b.pingMs;
        return a.name < b.name;
    });
    return out;
}

std::vector<NetworkInfo> Network::networks() const {
    std::vector<NetworkInfo> out;
    for (const auto& l : lobbies_) {
        NetworkInfo info;
        info.id = l->id();
        info.state = l->state();
        info.code = netcode::format(l->code());
        if (l->state() == State::Connected) {
            info.name = l->name();
            info.owner = l->isOwner();
        }
        info.peers = peersOf(*l);
        out.push_back(std::move(info));
    }
    return out;
}

void Network::setNick(const std::string& nick) {
    nick_ = text::sanitize(nick, 32);
    KLOG_INFO((nick_.empty() ? std::string("Your name in networks: your Steam name") : "Your name in networks: " + nick_));
    for (const auto& l : lobbies_) l->publishNick();
}

void Network::setBlocked(const std::set<uint64_t>& people) {
    const auto newlyBlocked = router_.setBlocked(people);
    for (uint64_t id : newlyBlocked) KLOG_INFO("Blocked " << nameOf(id) << " – nothing is exchanged with them now");
    closeSessions(newlyBlocked);
    if (floodLogged_.size() > 1000) floodLogged_.clear();
}

std::string Network::personName(uint64_t steamId) const {
    bool inLobby = false;
    for (const auto& l : lobbies_) {
        if (!l->isMember(steamId)) continue;
        inLobby = true;
        // Anybody in the lobby can copy someone else's encrypted nickname into their own lobby data,
        // so a nickname is shown only for people who proved the network's key (HELLO).
        if (!router_.isVerified(l->id(), steamId)) continue;
        const std::string nick = l->nickOf(steamId);
        if (!nick.empty()) {
            nicks_[steamId] = nick;
            return nick;
        }
    }
    if (inLobby) nicks_.erase(steamId); // in a lobby, without a (verified) nickname: the Steam name
    const auto it = nicks_.find(steamId); // already left the lobby: the nickname we saw last
    return it != nicks_.end() ? it->second : nameOf(steamId);
}

bool Network::rename(NetworkHandle network, const std::string& name) {
    Lobby* lobby = lobbyFor(network);
    return lobby && lobby->rename(name);
}

std::string Network::displayName(const Lobby& lobby) const {
    const std::string name = lobby.name();
    if (name.empty()) return netcode::format(lobby.code());
    for (const auto& other : lobbies_)
        if (other.get() != &lobby && other->state() == State::Connected && other->name() == name)
            return name + " (" + netcode::format(lobby.code()) + ")";
    return name;
}

State Network::state() const { return lobbies_.empty() ? State::Idle : lobbies_.front()->state(); }

std::string Network::codeText() const { return lobbies_.empty() ? std::string() : netcode::format(lobbies_.front()->code()); }

std::string Network::networkName() const {
    return lobbies_.empty() || lobbies_.front()->state() != State::Connected ? std::string() : lobbies_.front()->name();
}

std::vector<PeerInfo> Network::peers() const { return lobbies_.empty() ? std::vector<PeerInfo>{} : peersOf(*lobbies_.front()); }

} // namespace kettle::steam

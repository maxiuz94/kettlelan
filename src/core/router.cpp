#include "router.h"

#include <algorithm>
#include <mutex>
#include <set>

namespace kettle::router {

bool equalConstantTime(uint64_t a, uint64_t b) {
    uint64_t diff = a ^ b;
    diff |= diff >> 32;
    diff |= diff >> 16;
    diff |= diff >> 8;
    return (diff & 0xFF) == 0;
}

Router::Router(uint64_t selfSteamId) : selfId_(selfSteamId), selfAddress_(ip::addressForSteamId(selfSteamId)) {}

// ---------------------------------------------------------------- networks

std::optional<NetworkId> Router::addNetwork(const netcode::NetworkCode& code, const netkeys::Keys& keys) {
    std::unique_lock lk(mutex_);
    if (nets_.size() >= maxNetworks_) return std::nullopt;
    const NetworkId id = nextId_++;
    nets_[id] = Network{code, keys, {}};
    return id;
}

std::vector<uint64_t> Router::removeNetwork(NetworkId network) {
    std::unique_lock lk(mutex_);
    std::vector<uint64_t> gone;
    const auto it = nets_.find(network);
    if (it == nets_.end()) return gone;
    std::vector<uint64_t> members;
    for (const auto& [id, m] : it->second.members) members.push_back(id);
    nets_.erase(it);
    for (uint64_t id : members) forgetIfUnused(id, gone);
    return gone;
}

std::vector<NetworkId> Router::networks() const {
    std::shared_lock lk(mutex_);
    std::vector<NetworkId> out;
    for (const auto& [id, n] : nets_) out.push_back(id);
    return out;
}

std::optional<netcode::NetworkCode> Router::code(NetworkId network) const {
    std::shared_lock lk(mutex_);
    const auto it = nets_.find(network);
    if (it == nets_.end()) return std::nullopt;
    return it->second.code;
}

void Router::clear() {
    std::unique_lock lk(mutex_);
    nets_.clear();
    peers_.clear();
    routes_.clear();
}

// ---------------------------------------------------------------- members

bool Router::verifiedAnywhere(uint64_t id) const {
    for (const auto& [nid, n] : nets_) {
        const auto m = n.members.find(id);
        if (m != n.members.end() && m->second.verified) return true;
    }
    return false;
}

bool Router::memberAnywhere(uint64_t id) const {
    for (const auto& [nid, n] : nets_)
        if (n.members.count(id)) return true;
    return false;
}

void Router::forgetIfUnused(uint64_t id, std::vector<uint64_t>& goneEverywhere) {
    const auto p = peers_.find(id);
    if (p == peers_.end()) return;
    if (!verifiedAnywhere(id)) {
        // no longer trusted anywhere: no traffic from or to this person
        const auto r = routes_.find(p->second.address.value());
        if (r != routes_.end() && r->second == id) routes_.erase(r);
    }
    if (!memberAnywhere(id)) {
        peers_.erase(p);
        goneEverywhere.push_back(id);
    }
}

SyncResult Router::syncMembers(NetworkId network, const std::vector<uint64_t>& members) {
    std::unique_lock lk(mutex_);
    SyncResult result;
    const auto nit = nets_.find(network);
    if (nit == nets_.end()) return result;
    auto& netMembers = nit->second.members;
    for (uint64_t id : members) {
        if (id == selfId_) continue;
        netMembers.try_emplace(id);
        auto [p, inserted] = peers_.try_emplace(id);
        if (inserted) p->second.address = ip::addressForSteamId(id);
    }
    for (auto it = netMembers.begin(); it != netMembers.end();) {
        if (std::find(members.begin(), members.end(), it->first) == members.end()) {
            result.left.push_back(it->first);
            it = netMembers.erase(it);
        } else {
            ++it;
        }
    }
    for (uint64_t id : result.left) forgetIfUnused(id, result.goneEverywhere);
    return result;
}

bool Router::isMember(uint64_t steamId) const {
    std::shared_lock lk(mutex_);
    return peers_.count(steamId) != 0;
}

// ---------------------------------------------------------------- verification

HelloOutcome Router::onHello(uint64_t from, const protocol::HelloMessage& msg) {
    std::unique_lock lk(mutex_);
    if (nets_.empty()) return {HelloResult::NoNetwork, 0};
    const auto pit = peers_.find(from);
    if (pit == peers_.end()) return {HelloResult::UnknownPeer, 0};
    Peer& peer = pit->second;

    // The proof must come from the key of a network in whose lobby the sender is.
    NetworkId matched = 0;
    for (const auto& [nid, n] : nets_) {
        if (!n.members.count(from)) continue;
        if (equalConstantTime(msg.proof, protocol::computeProof(n.keys, from, selfId_))) {
            matched = nid;
            break;
        }
    }
    if (!matched) return {HelloResult::BadProof, 0};
    if (msg.version != protocol::kVersion) return {HelloResult::BadVersion, 0};
    if (msg.address != peer.address) return {HelloResult::BadAddress, 0};

    Membership& m = nets_[matched].members[from];
    if (m.verified) return {peer.conflict ? HelloResult::Conflict : HelloResult::Repeated, matched};
    const bool trustedBefore = verifiedAnywhere(from);
    m.verified = true;
    if (trustedBefore) return {peer.conflict ? HelloResult::Conflict : HelloResult::Verified, matched};

    const auto existing = routes_.find(peer.address.value());
    if (peer.address == selfAddress_ || (existing != routes_.end() && existing->second != from)) {
        peer.conflict = true;
        return {HelloResult::Conflict, matched};
    }
    peer.conflict = false;
    routes_[peer.address.value()] = from;
    return {HelloResult::Verified, matched};
}

std::optional<protocol::HelloMessage> Router::makeHello(NetworkId network, uint64_t to, bool ack) const {
    std::shared_lock lk(mutex_);
    const auto it = nets_.find(network);
    if (it == nets_.end()) return std::nullopt;
    protocol::HelloMessage msg;
    msg.type = ack ? protocol::MessageType::HelloAck : protocol::MessageType::Hello;
    msg.address = selfAddress_;
    msg.proof = protocol::computeProof(it->second.keys, selfId_, to);
    return msg;
}

std::vector<std::pair<NetworkId, uint64_t>> Router::dueHellos(Clock::time_point now, std::chrono::milliseconds interval,
                                                              int maxAttempts) {
    std::unique_lock lk(mutex_);
    std::vector<std::pair<NetworkId, uint64_t>> due;
    for (auto& [nid, n] : nets_) {
        for (auto& [id, m] : n.members) {
            if (m.verified || m.hellosSent >= maxAttempts || now < m.nextHello) continue;
            m.hellosSent++;
            m.nextHello = now + interval;
            due.emplace_back(nid, id);
        }
    }
    return due;
}

// ---------------------------------------------------------------- packets

InboundResult Router::onData(uint64_t from, std::span<const uint8_t> packet, filter::Result* filterResult) const {
    if (packet.size() > 65535) return InboundResult::Malformed;
    const auto hdr = ip::parseIPv4(packet);
    if (!hdr) return InboundResult::Malformed;
    if (hdr->dst != selfAddress_ && !ip::isGroupDestination(hdr->dst)) return InboundResult::NotForUs;
    {
        std::shared_lock lk(mutex_);
        const auto p = peers_.find(from);
        if (p == peers_.end() || p->second.conflict || !verifiedAnywhere(from)) return InboundResult::UnknownSender;
        const auto r = routes_.find(hdr->src.value());
        if (r == routes_.end() || r->second != from) return InboundResult::Spoofed;
    }
    if (protect_) {
        const auto res = filter::inspectInbound(packet);
        if (filterResult) *filterResult = res;
        if (res.verdict != filter::Verdict::Allow) return InboundResult::Filtered;
    }
    return InboundResult::Deliver;
}

std::vector<uint64_t> Router::recipients(std::span<const uint8_t> packet) const {
    std::vector<uint64_t> out;
    if (packet.size() > 65535) return out;
    const auto hdr = ip::parseIPv4(packet);
    if (!hdr || hdr->src != selfAddress_) return out; // we only send as ourselves
    const bool group = ip::isGroupDestination(hdr->dst);
    if (group && protect_ && filter::isDiscoveryBroadcast(packet)) return out;

    std::shared_lock lk(mutex_);
    if (nets_.empty()) return out;
    if (group) {
        for (const auto& [addr, id] : routes_) out.push_back(id); // one route per person: no duplicates
    } else if (const auto it = routes_.find(hdr->dst.value()); it != routes_.end()) {
        out.push_back(it->second);
    }
    return out;
}

std::vector<PeerState> Router::peers(NetworkId network) const {
    std::shared_lock lk(mutex_);
    std::vector<PeerState> out;
    const auto it = nets_.find(network);
    if (it == nets_.end()) return out;
    for (const auto& [id, m] : it->second.members) {
        const auto p = peers_.find(id);
        if (p == peers_.end()) continue;
        out.push_back({id, p->second.address, m.verified, p->second.conflict});
    }
    return out;
}

} // namespace kettle::router

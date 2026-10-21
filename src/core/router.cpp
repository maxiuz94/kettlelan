#include "router.h"

#include <algorithm>
#include <mutex>

namespace kettle::router {

bool equalConstantTime(uint64_t a, uint64_t b) {
    uint64_t diff = a ^ b;
    diff |= diff >> 32;
    diff |= diff >> 16;
    diff |= diff >> 8;
    return (diff & 0xFF) == 0;
}

Router::Router(uint64_t selfSteamId) : selfId_(selfSteamId), selfAddress_(ip::addressForSteamId(selfSteamId)) {}

void Router::setCode(std::optional<netcode::NetworkCode> code) {
    std::unique_lock lk(mutex_);
    code_ = code;
}

std::optional<netcode::NetworkCode> Router::code() const {
    std::shared_lock lk(mutex_);
    return code_;
}

std::vector<uint64_t> Router::syncMembers(const std::vector<uint64_t>& members) {
    std::unique_lock lk(mutex_);
    for (uint64_t id : members) {
        if (id == selfId_) continue;
        auto [it, inserted] = peers_.try_emplace(id);
        if (inserted) it->second.address = ip::addressForSteamId(id);
    }
    std::vector<uint64_t> gone;
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (std::find(members.begin(), members.end(), it->first) == members.end()) {
            gone.push_back(it->first);
            const auto r = routes_.find(it->second.address.value());
            if (r != routes_.end() && r->second == it->first) routes_.erase(r);
            it = peers_.erase(it);
        } else {
            ++it;
        }
    }
    return gone;
}

bool Router::isMember(uint64_t steamId) const {
    std::shared_lock lk(mutex_);
    return peers_.count(steamId) != 0;
}

void Router::clear() {
    std::unique_lock lk(mutex_);
    peers_.clear();
    routes_.clear();
    code_.reset();
}

HelloResult Router::onHello(uint64_t from, const protocol::HelloMessage& msg) {
    std::unique_lock lk(mutex_);
    if (!code_) return HelloResult::NoNetwork;
    const auto it = peers_.find(from);
    if (it == peers_.end()) return HelloResult::UnknownPeer;
    Peer& peer = it->second;

    if (!equalConstantTime(msg.proof, protocol::computeProof(*code_, from, selfId_))) return HelloResult::BadProof;
    if (msg.version != protocol::kVersion) return HelloResult::BadVersion;
    if (msg.address != peer.address) return HelloResult::BadAddress;

    if (peer.verified) return peer.conflict ? HelloResult::Conflict : HelloResult::Repeated;
    peer.verified = true;
    const auto existing = routes_.find(peer.address.value());
    if (peer.address == selfAddress_ || (existing != routes_.end() && existing->second != from)) {
        peer.conflict = true;
        return HelloResult::Conflict;
    }
    routes_[peer.address.value()] = from;
    return HelloResult::Verified;
}

std::optional<protocol::HelloMessage> Router::makeHello(uint64_t to, bool ack) const {
    std::shared_lock lk(mutex_);
    if (!code_) return std::nullopt;
    protocol::HelloMessage msg;
    msg.type = ack ? protocol::MessageType::HelloAck : protocol::MessageType::Hello;
    msg.address = selfAddress_;
    msg.proof = protocol::computeProof(*code_, selfId_, to);
    return msg;
}

std::vector<uint64_t> Router::dueHellos(Clock::time_point now, std::chrono::milliseconds interval, int maxAttempts) {
    std::unique_lock lk(mutex_);
    std::vector<uint64_t> due;
    if (!code_) return due;
    for (auto& [id, peer] : peers_) {
        if (peer.verified || peer.hellosSent >= maxAttempts || now < peer.nextHello) continue;
        peer.hellosSent++;
        peer.nextHello = now + interval;
        due.push_back(id);
    }
    return due;
}

InboundResult Router::onData(uint64_t from, std::span<const uint8_t> packet, filter::Result* filterResult) const {
    if (packet.size() > 65535) return InboundResult::Malformed;
    const auto hdr = ip::parseIPv4(packet);
    if (!hdr) return InboundResult::Malformed;
    if (hdr->dst != selfAddress_ && !ip::isGroupDestination(hdr->dst)) return InboundResult::NotForUs;
    {
        std::shared_lock lk(mutex_);
        const auto p = peers_.find(from);
        if (p == peers_.end() || !p->second.verified || p->second.conflict) return InboundResult::UnknownSender;
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
    if (!code_) return out;
    if (group) {
        for (const auto& [addr, id] : routes_) out.push_back(id);
    } else if (const auto it = routes_.find(hdr->dst.value()); it != routes_.end()) {
        out.push_back(it->second);
    }
    return out;
}

std::vector<PeerState> Router::peers() const {
    std::shared_lock lk(mutex_);
    std::vector<PeerState> out;
    for (const auto& [id, p] : peers_) out.push_back({id, p.address, p.verified, p.conflict});
    return out;
}

} // namespace kettle::router

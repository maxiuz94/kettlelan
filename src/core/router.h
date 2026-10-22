// KettleLAN – the network's security decisions: who is a member, whom we trust, which packets
// we accept and where we send them. No Steam dependency, so it is fully testable.
//
// Threads: all methods are thread-safe (Steam thread + TUN adapter thread).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <shared_mutex>
#include <span>
#include <vector>

#include "filter.h"
#include "ip.h"
#include "netcode.h"
#include "netkeys.h"
#include "protocol.h"

namespace kettle::router {

enum class HelloResult {
    Verified,     // valid proof – the person has just been verified
    Repeated,     // valid proof from someone already verified
    Conflict,     // valid proof, but the address clashes with someone else – no traffic
    UnknownPeer,  // not in the lobby
    BadProof,     // does not know the network key
    BadVersion,   // different protocol version
    BadAddress,   // the address does not match their SteamID (attempt to take over someone's address)
    NoNetwork,    // we are not in a network
};

enum class InboundResult {
    Deliver,       // hand over to the system
    Malformed,     // not a valid IPv4 packet
    NotForUs,      // destination is neither our address nor a broadcast
    UnknownSender, // the sender is not a verified member
    Spoofed,       // the source address does not belong to the sender
    Filtered,      // blocked by service protection (details in `filter`)
};

struct PeerState {
    uint64_t steamId = 0;
    ip::Address4 address;
    bool verified = false;
    bool conflict = false;
};

class Router {
public:
    using Clock = std::chrono::steady_clock;

    explicit Router(uint64_t selfSteamId);

    uint64_t selfId() const { return selfId_; }
    ip::Address4 selfAddress() const { return selfAddress_; }

    // Network to use; the keys are derived once with netkeys::derive (slow), not here.
    void setNetwork(const netcode::NetworkCode& code, const netkeys::Keys& keys);
    std::optional<netcode::NetworkCode> code() const;
    void setProtectServices(bool on) { protect_ = on; }
    bool protectServices() const { return protect_; }

    // --- lobby members (from Steam's list)
    // Sets the current member list; returns the SteamIDs of people who disappeared from it.
    std::vector<uint64_t> syncMembers(const std::vector<uint64_t>& members);
    bool isMember(uint64_t steamId) const;
    void clear();

    // --- verification
    HelloResult onHello(uint64_t from, const protocol::HelloMessage& msg);
    // The proof we send to `to`. Empty when we are not in a network.
    std::optional<protocol::HelloMessage> makeHello(uint64_t to, bool ack) const;
    // Who should get a (repeated) HELLO – unverified people, with a delay between attempts.
    std::vector<uint64_t> dueHellos(Clock::time_point now, std::chrono::milliseconds interval, int maxAttempts);

    // --- packets
    InboundResult onData(uint64_t from, std::span<const uint8_t> packet, filter::Result* filterResult = nullptr) const;
    // Where to send a packet from our system (empty = do not send).
    std::vector<uint64_t> recipients(std::span<const uint8_t> packet) const;

    std::vector<PeerState> peers() const;

private:
    struct Peer {
        ip::Address4 address;
        bool verified = false;
        bool conflict = false;
        int hellosSent = 0;
        Clock::time_point nextHello{};
    };

    const uint64_t selfId_;
    const ip::Address4 selfAddress_;
    std::atomic<bool> protect_{true};

    mutable std::shared_mutex mutex_;
    std::optional<netcode::NetworkCode> code_;
    std::optional<netkeys::Keys> keys_;
    std::map<uint64_t, Peer> peers_;
    std::map<uint32_t, uint64_t> routes_; // address → SteamID (verified members without conflicts only)
};

// Constant-time comparison.
bool equalConstantTime(uint64_t a, uint64_t b);

} // namespace kettle::router

// KettleLAN – the network's security decisions: who is a member, whom we trust, which packets
// we accept and where we send them. No Steam dependency, so it is fully testable.
//
// Threads: all methods are thread-safe (Steam thread + TUN adapter thread).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
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
    Blocked,      // we blocked this person – no verification, no traffic
    NoNetwork,    // we are not in a network
};

enum class InboundResult {
    Deliver,       // hand over to the system
    Malformed,     // not a valid IPv4 packet
    NotForUs,      // destination is neither our address nor a broadcast
    UnknownSender, // the sender is not a verified member
    Spoofed,       // the source address does not belong to the sender
    Filtered,      // blocked by service protection (details in `filter`)
    Blocked,       // from a person we blocked
};

struct PeerState {
    uint64_t steamId = 0;
    ip::Address4 address;
    bool verified = false;
    bool conflict = false;
    bool blocked = false;
};

using NetworkId = uint32_t; // handle given by the router, never 0

struct HelloOutcome {
    HelloResult result = HelloResult::NoNetwork;
    NetworkId network = 0; // the network the proof belongs to (Verified/Repeated/Conflict)
};

struct SyncResult {
    std::vector<uint64_t> left;      // people who left this network
    std::vector<uint64_t> goneEverywhere; // …and are in none of our networks any more (close their sessions)
};

class Router {
public:
    using Clock = std::chrono::steady_clock;
    // Networks at the same time. Steam itself allowed at least six lobbies in tests; the limit keeps
    // the number of lobby searches and the user interface manageable.
    static constexpr size_t kMaxNetworks = 5;
    // Messages one person may send us: on average kRatePerSecond, up to kRateBurst at once. Far above any
    // game (a busy game server gets a few hundred per player), so only a flood is cut off.
    static constexpr double kRatePerSecond = 10000;
    static constexpr double kRateBurst = 20000;

    explicit Router(uint64_t selfSteamId);

    // Security/limit tests only: allow more networks to see how Steam itself reacts.
    void setMaxNetworksForTests(size_t n) { maxNetworks_ = n; }
    size_t maxNetworks() const { return maxNetworks_; }

    uint64_t selfId() const { return selfId_; }
    ip::Address4 selfAddress() const { return selfAddress_; }

    // --- networks. Keys are derived once with netkeys::derive (slow), not here.
    // Returns nothing when the limit of networks is reached.
    std::optional<NetworkId> addNetwork(const netcode::NetworkCode& code, const netkeys::Keys& keys);
    // Returns the people who are in none of our networks any more.
    std::vector<uint64_t> removeNetwork(NetworkId network);
    std::vector<NetworkId> networks() const;
    std::optional<netcode::NetworkCode> code(NetworkId network) const;
    void clear(); // all networks

    void setProtectServices(bool on) { protect_ = on; }
    bool protectServices() const { return protect_; }

    // --- lobby members of one network (from Steam's list)
    SyncResult syncMembers(NetworkId network, const std::vector<uint64_t>& members);
    bool isMember(uint64_t steamId) const; // member of any of our networks
    bool isVerified(NetworkId network, uint64_t steamId) const; // proved the key of this network

    // --- verification. A proof verifies a person only in the network whose key made it.
    HelloOutcome onHello(uint64_t from, const protocol::HelloMessage& msg);
    // The proof we send to `to` for `network`. Empty for an unknown network.
    std::optional<protocol::HelloMessage> makeHello(NetworkId network, uint64_t to, bool ack) const;
    // Who should get a (repeated) HELLO for which network – with a delay between attempts.
    std::vector<std::pair<NetworkId, uint64_t>> dueHellos(Clock::time_point now, std::chrono::milliseconds interval,
                                                          int maxAttempts);

    // --- people we blocked (on this computer only, in every network). A blocked person is not
    // verified, gets no HELLO and no packets from us, and nothing from them reaches the system.
    // Returns the people who were blocked just now (close their sessions).
    std::vector<uint64_t> setBlocked(const std::set<uint64_t>& people);
    bool isBlocked(uint64_t steamId) const;

    // --- flood protection: whether to process another message from this person now (token bucket
    // per person). Called for every message before anything else is done with it.
    bool admit(uint64_t from, Clock::time_point now);

    // --- packets (across all networks: one adapter, one address)
    InboundResult onData(uint64_t from, std::span<const uint8_t> packet, filter::Result* filterResult = nullptr) const;
    // Where to send a packet from our system (empty = do not send). Each person at most once.
    std::vector<uint64_t> recipients(std::span<const uint8_t> packet) const;

    // Members of one network; `verified` means verified in that network.
    std::vector<PeerState> peers(NetworkId network) const;

private:
    struct Membership {
        bool verified = false;
        int hellosSent = 0;
        Clock::time_point nextHello{};
    };
    struct Network {
        netcode::NetworkCode code;
        netkeys::Keys keys;
        std::map<uint64_t, Membership> members;
    };
    struct Peer {
        ip::Address4 address;
        bool conflict = false;
    };

    bool verifiedAnywhere(uint64_t id) const;  // caller holds the lock
    bool memberAnywhere(uint64_t id) const;    // caller holds the lock
    void forgetIfUnused(uint64_t id, std::vector<uint64_t>& goneEverywhere); // caller holds the lock

    const uint64_t selfId_;
    const ip::Address4 selfAddress_;
    std::atomic<bool> protect_{true};

    mutable std::shared_mutex mutex_;
    NetworkId nextId_ = 1;
    size_t maxNetworks_ = kMaxNetworks;
    std::map<NetworkId, Network> nets_;
    std::map<uint64_t, Peer> peers_;      // everyone in at least one of our networks
    std::map<uint32_t, uint64_t> routes_; // address → SteamID (verified somewhere, without conflicts)
    std::set<uint64_t> blocked_;

    struct Bucket {
        double tokens = kRateBurst;
        Clock::time_point last{};
    };
    std::mutex rateMutex_;
    std::map<uint64_t, Bucket> buckets_;
};

} // namespace kettle::router

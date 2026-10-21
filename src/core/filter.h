// KettleLAN – protects this computer's services from other network members.
//
// The filter works on packets RECEIVED from other members, before they reach the system.
// It blocks connections to common services (SSH, file sharing, remote desktop, databases…)
// that should not be visible to other players. Game traffic passes unchanged.
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace kettle::filter {

enum class Verdict {
    Allow,
    BlockedService,   // port of a protected service
    BlockedProtocol,  // protocol other than TCP/UDP/ICMP
    BlockedFragment,  // suspicious IP fragment (e.g. an attempt to hide the port number)
    Malformed,
};

struct Result {
    Verdict verdict = Verdict::Allow;
    uint8_t protocol = 0;
    uint16_t dstPort = 0;
};

// Inspects an IPv4 packet coming from the KettleLAN network.
Result inspectInbound(std::span<const uint8_t> packet);

// Whether a packet SENT by our system to all members (broadcast/multicast) is a service
// announcement that reveals the computer's name and shared resources
// (mDNS/Avahi, NetBIOS, SSDP, KDE Connect, Spotify, Steam Remote Play…).
bool isDiscoveryBroadcast(std::span<const uint8_t> packet);

// Whether the port belongs to a protected service (TCP or UDP).
bool isProtectedPort(uint16_t port);

// Service name for the log, e.g. "SSH", "file sharing (SMB)".
std::string serviceName(uint16_t port);

} // namespace kettle::filter

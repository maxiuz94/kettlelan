// KettleLAN – IPv4 addresses and packets.
// Based on ip.cpp from PartyLAN (c) 2023 Yifan Gu, BSD-3 license.
#include "ip.h"

#include <charconv>

namespace kettle::ip {

Address4 Address4::fromBytes(std::span<const uint8_t, 4> b) {
    return Address4(b[0], b[1], b[2], b[3]);
}

std::optional<Address4> Address4::parse(const std::string& text) {
    uint32_t value = 0;
    const char* p = text.data();
    const char* end = p + text.size();
    for (int i = 0; i < 4; ++i) {
        unsigned part = 0;
        auto [next, ec] = std::from_chars(p, end, part);
        if (ec != std::errc() || part > 255 || next == p) return std::nullopt;
        value = (value << 8) | part;
        p = next;
        if (i < 3) {
            if (p == end || *p != '.') return std::nullopt;
            ++p;
        }
    }
    if (p != end) return std::nullopt;
    return Address4(value);
}

std::array<uint8_t, 4> Address4::bytes() const {
    return {uint8_t(value_ >> 24), uint8_t(value_ >> 16), uint8_t(value_ >> 8), uint8_t(value_)};
}

std::string Address4::toString() const {
    auto b = bytes();
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." +
           std::to_string(b[3]);
}

static uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

Address4 addressForSteamId(uint64_t steamId) {
    const uint32_t size = kVirtualNet.size();
    uint32_t host = uint32_t(splitmix64(steamId) % size);
    // skip the network address and the subnet broadcast
    if (host == 0) host = 1;
    if (host == size - 1) host = size - 2;
    return Address4(kVirtualNet.network().value() + host);
}

bool isGroupDestination(Address4 dst) {
    return dst.isLimitedBroadcast() || dst.isMulticast() || dst == kVirtualNet.broadcast();
}

std::optional<Packet4View> parseIPv4(std::span<const uint8_t> p) {
    if (p.size() < 20) return std::nullopt;
    if ((p[0] >> 4) != 4) return std::nullopt;
    const size_t ihl = size_t(p[0] & 0x0F) * 4;
    if (ihl < 20 || ihl > p.size()) return std::nullopt;
    const size_t total = (size_t(p[2]) << 8) | p[3];
    if (total < ihl || total > p.size()) return std::nullopt;
    return Packet4View{Address4::fromBytes(p.subspan<12, 4>()), Address4::fromBytes(p.subspan<16, 4>())};
}

} // namespace kettle::ip

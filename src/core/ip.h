// KettleLAN – adresy i pakiety IPv4.
// Na podstawie ip.h/ip.cpp z PartyLAN (c) 2023 Yifan Gu, licencja BSD-3.
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace kettle::ip {

class Address4 {
public:
    constexpr Address4() = default;
    constexpr explicit Address4(uint32_t value) : value_(value) {}
    constexpr Address4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : value_((uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | d) {}

    static Address4 fromBytes(std::span<const uint8_t, 4> bytes);
    static std::optional<Address4> parse(const std::string& text);

    constexpr uint32_t value() const { return value_; }
    std::array<uint8_t, 4> bytes() const;
    std::string toString() const;

    bool isLimitedBroadcast() const { return value_ == 0xFFFFFFFFu; }
    bool isMulticast() const { return (value_ >> 28) == 0xE; }

    constexpr auto operator<=>(const Address4&) const = default;

private:
    uint32_t value_ = 0;
};

class Subnet4 {
public:
    constexpr Subnet4(Address4 base, uint8_t prefix) : base_(base), prefix_(prefix) {}

    constexpr uint8_t prefix() const { return prefix_; }
    constexpr uint32_t mask() const { return prefix_ == 0 ? 0 : 0xFFFFFFFFu << (32 - prefix_); }
    constexpr uint32_t size() const { return prefix_ == 0 ? 0 : (1u << (32 - prefix_)); }
    constexpr Address4 network() const { return Address4(base_.value() & mask()); }
    constexpr Address4 broadcast() const { return Address4(network().value() | ~mask()); }
    constexpr bool contains(Address4 a) const { return (a.value() & mask()) == network().value(); }

private:
    Address4 base_;
    uint8_t prefix_;
};

// Wirtualna sieć KettleLAN: 100.64.0.0/10 (zakres CGNAT, nie koliduje z domowymi LAN-ami).
inline constexpr Subnet4 kVirtualNet{Address4(100, 64, 0, 0), 10};

// Stały adres wyliczany z 64-bitowego SteamID (ten sam przy każdym uruchomieniu).
Address4 addressForSteamId(uint64_t steamId);

// Czy adres docelowy oznacza „wszyscy w sieci” (broadcast, multicast, broadcast podsieci).
bool isGroupDestination(Address4 dst);

// Minimalny widok nagłówka IPv4 – bez kopiowania i bez modyfikowania pakietu.
struct Packet4View {
    Address4 src;
    Address4 dst;
};
std::optional<Packet4View> parseIPv4(std::span<const uint8_t> packet);

} // namespace kettle::ip

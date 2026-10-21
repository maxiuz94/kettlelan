// KettleLAN – network code, e.g. "KTL-7F3Q-9XM2-A4KD".
//
// A code carries 60 bits: a 32-bit permanent network id + a 28-bit random key.
// The code does not depend on a Steam lobby – a network can be closed and reopened with the same code.
// Encoding: Crockford Base32 (no I, L, O, U – they cannot be confused with digits).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace kettle::netcode {

struct NetworkCode {
    uint32_t networkId = 0; // random, permanent network id
    uint32_t secret = 0;         // 28 bits – network key, never stored in lobby data

    bool operator==(const NetworkCode&) const = default;
};

inline constexpr uint32_t kSecretMask = (1u << 28) - 1;

std::string format(const NetworkCode& code);

// New random network code.
NetworkCode generate();

// Accepts the code with or without dashes, in any letter case, with or without the "KTL" prefix.
std::optional<NetworkCode> parse(const std::string& text);

} // namespace kettle::netcode

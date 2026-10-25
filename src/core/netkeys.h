// KettleLAN – keys derived from a network code.
//
// Everything that goes into Steam lobby data or to people who have not proved anything yet is
// derived from the code with Argon2id – a deliberately slow, memory-hard function. Someone who
// lists the lobbies or joins one without the code can only test code guesses at the speed of
// Argon2id (tens of milliseconds and 32 MiB per guess), so the 60-bit code cannot be guessed.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "netcode.h"

namespace kettle::netkeys {

struct Keys {
    std::string tag;                  // 32 hex characters, public: finds the network's lobby
    std::array<uint8_t, 16> proofKey; // SipHash key for HELLO proofs
    std::array<uint8_t, 32> nameKey;  // XChaCha20-Poly1305 key for the network name
};

enum class Cost {
    Normal, // used by the program
    Test,   // tiny parameters, for unit tests only
};

// Derives the keys of a network (Normal cost: ~0.1–0.3 s and 32 MiB of memory).
Keys derive(const netcode::NetworkCode& code, Cost cost = Cost::Normal);

// Encrypts the network name for lobby data: hex(nonce[24] || mac[16] || ciphertext).
// The name is cut to 60 UTF-8 bytes first.
std::string sealName(const Keys& keys, std::string_view name);

// Decrypts a sealed name; nothing if it was not made with these keys or was changed.
std::optional<std::string> openName(const Keys& keys, std::string_view sealed);

// A member's nickname in this network's lobby (member data), sealed like the network name but in its
// own context, so a sealed network name cannot be passed off as a nickname or the other way round.
// The nickname is cut to 32 characters (at most 128 UTF-8 bytes).
std::string sealNick(const Keys& keys, std::string_view nick);
std::optional<std::string> openNick(const Keys& keys, std::string_view sealed);

} // namespace kettle::netkeys

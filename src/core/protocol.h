// KettleLAN – control messages between network members (channel 1).
//
// Every new member sends HELLO to everyone in the lobby, and they answer with HELLO_ACK.
// Both messages carry a proof of knowing the network key:
//   proof = SipHash(key(secret, network), sender || receiver)
// A proof goes directly to one person (encrypted by Steam), so someone who joins the lobby
// without the code never sees a valid proof and cannot guess the key offline.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ip.h"
#include "netkeys.h"

namespace kettle::protocol {

inline constexpr int kChannelData = 0;    // IP packets
inline constexpr int kChannelControl = 1; // HELLO / HELLO_ACK
inline constexpr uint8_t kVersion = 3; // 3: keys derived with Argon2id (KettleLAN 0.3)

enum class MessageType : uint8_t { Hello = 1, HelloAck = 2 };

struct HelloMessage {
    MessageType type = MessageType::Hello;
    uint8_t version = kVersion;
    ip::Address4 address;
    uint64_t proof = 0;
};

std::vector<uint8_t> encode(const HelloMessage& msg);
std::optional<HelloMessage> decode(std::span<const uint8_t> data);


uint64_t computeProof(const netkeys::Keys& keys, uint64_t senderSteamId, uint64_t receiverSteamId);

// Lobby tag of KettleLAN 0.2 (protocol 2). Used only to tell the user that someone in the same
// network still runs an old version – never to join such a lobby.
std::string legacyLobbyTag(const netcode::NetworkCode& code);

} // namespace kettle::protocol

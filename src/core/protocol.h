// KettleLAN – wiadomości sterujące między członkami sieci (kanał 1).
//
// Każdy nowy członek wysyła HELLO do wszystkich osób w lobby, a one odpowiadają HELLO_ACK.
// Obie wiadomości zawierają dowód znajomości klucza sieci:
//   proof = SipHash(klucz(secret, lobby), nadawca || odbiorca)
// Dowód idzie bezpośrednio do jednej osoby (szyfrowany przez Steam), więc ktoś, kto dołączy
// do lobby bez kodu, nie zobaczy żadnego poprawnego dowodu i nie może zgadywać klucza offline.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ip.h"
#include "netcode.h"

namespace kettle::protocol {

inline constexpr int kChannelData = 0;    // pakiety IP
inline constexpr int kChannelControl = 1; // HELLO / HELLO_ACK
inline constexpr uint8_t kVersion = 2;

enum class MessageType : uint8_t { Hello = 1, HelloAck = 2 };

struct HelloMessage {
    MessageType type = MessageType::Hello;
    uint8_t version = kVersion;
    ip::Address4 address;
    uint64_t proof = 0;
};

std::vector<uint8_t> encode(const HelloMessage& msg);
std::optional<HelloMessage> decode(std::span<const uint8_t> data);

// Znacznik, po którym KettleLAN odnajduje lobby sieci w Steamie (16 znaków szesnastkowych).
// Wyliczany z całego kodu, więc bez kodu nie da się znaleźć ani podejrzeć cudzej sieci.
std::string lobbyTag(const netcode::NetworkCode& code);

uint64_t computeProof(const netcode::NetworkCode& code, uint64_t senderSteamId, uint64_t receiverSteamId);

} // namespace kettle::protocol

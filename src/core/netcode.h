// KettleLAN – kod sieci, np. „KTL-7F3Q-9XM2-A4KD”.
//
// Kod niesie 60 bitów: 32 bity stałego identyfikatora sieci + 28 bitów losowego klucza.
// Kod nie zależy od lobby Steama – sieć można zamknąć i otworzyć ponownie z tym samym kodem.
// Zapis: Crockford Base32 (bez liter I, L, O, U – nie da się ich pomylić z cyframi).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace kettle::netcode {

struct NetworkCode {
    uint32_t networkId = 0; // losowy, stały identyfikator sieci
    uint32_t secret = 0;         // 28 bitów – klucz sieci, nigdy nie trafia do danych lobby

    bool operator==(const NetworkCode&) const = default;
};

inline constexpr uint32_t kSecretMask = (1u << 28) - 1;

std::string format(const NetworkCode& code);

// Nowy, losowy kod sieci.
NetworkCode generate();

// Przyjmuje kod z myślnikami lub bez, małymi lub wielkimi literami, z prefiksem „KTL” lub bez.
std::optional<NetworkCode> parse(const std::string& text);

} // namespace kettle::netcode

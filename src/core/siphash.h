// KettleLAN – SipHash-2-4 (Aumasson, Bernstein), 64-bitowy skrót z kluczem.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace kettle::crypto {

using SipKey = std::array<uint8_t, 16>;

uint64_t siphash24(const SipKey& key, std::span<const uint8_t> data);

} // namespace kettle::crypto

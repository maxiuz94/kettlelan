// KettleLAN – random bytes from the operating system's cryptographic generator.
#pragma once

#include <cstdint>
#include <span>

namespace kettle::random {

// Fills `out` with cryptographically secure random bytes (getrandom on Linux, BCryptGenRandom on
// Windows). Aborts the program if the system generator fails – there is no safe fallback.
void fill(std::span<uint8_t> out);

} // namespace kettle::random

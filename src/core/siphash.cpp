// KettleLAN – SipHash-2-4, implemented after the reference specification.
#include "siphash.h"

namespace kettle::crypto {

static inline uint64_t rotl(uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

static inline uint64_t load64le(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

static inline void sipround(uint64_t& v0, uint64_t& v1, uint64_t& v2, uint64_t& v3) {
    v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32);
    v2 += v3; v3 = rotl(v3, 16); v3 ^= v2;
    v0 += v3; v3 = rotl(v3, 21); v3 ^= v0;
    v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32);
}

uint64_t siphash24(const SipKey& key, std::span<const uint8_t> data) {
    const uint64_t k0 = load64le(key.data());
    const uint64_t k1 = load64le(key.data() + 8);
    uint64_t v0 = 0x736f6d6570736575ull ^ k0;
    uint64_t v1 = 0x646f72616e646f6dull ^ k1;
    uint64_t v2 = 0x6c7967656e657261ull ^ k0;
    uint64_t v3 = 0x7465646279746573ull ^ k1;

    const size_t len = data.size();
    const size_t full = len & ~size_t(7);
    for (size_t i = 0; i < full; i += 8) {
        const uint64_t m = load64le(data.data() + i);
        v3 ^= m;
        sipround(v0, v1, v2, v3);
        sipround(v0, v1, v2, v3);
        v0 ^= m;
    }
    uint64_t b = uint64_t(len) << 56;
    for (size_t i = 0; i < (len & 7); ++i) b |= uint64_t(data[full + i]) << (8 * i);
    v3 ^= b;
    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);
    v0 ^= b;
    v2 ^= 0xff;
    for (int i = 0; i < 4; ++i) sipround(v0, v1, v2, v3);
    return v0 ^ v1 ^ v2 ^ v3;
}

} // namespace kettle::crypto

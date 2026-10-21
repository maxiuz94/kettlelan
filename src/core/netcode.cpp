#include "netcode.h"

#include <cctype>
#include <random>

namespace kettle::netcode {

static constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static constexpr int kChars = 12; // 12 × 5 bits = 60 bits

static int decodeChar(char c) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    for (int i = 0; i < 32; ++i)
        if (kAlphabet[i] == c) return i;
    return -1;
}

std::string format(const NetworkCode& code) {
    const uint64_t bits = (uint64_t(code.networkId) << 28) | (code.secret & kSecretMask);
    std::string out = "KTL";
    for (int i = 0; i < kChars; ++i) {
        if (i % 4 == 0) out += '-';
        const int shift = 5 * (kChars - 1 - i);
        out += kAlphabet[(bits >> shift) & 0x1F];
    }
    return out;
}

NetworkCode generate() {
    std::random_device rd;
    NetworkCode c;
    while (c.networkId == 0) c.networkId = uint32_t(rd());
    while (c.secret == 0) c.secret = uint32_t(rd()) & kSecretMask;
    return c;
}

std::optional<NetworkCode> parse(const std::string& text) {
    std::string s;
    for (char c : text) {
        if (c == '-' || c == ' ' || c == '\t') continue;
        s += char(std::toupper(static_cast<unsigned char>(c)));
    }
    if (s.rfind("KTL", 0) == 0 && s.size() == kChars + 3) s.erase(0, 3);
    if (s.size() != kChars) return std::nullopt;

    uint64_t bits = 0;
    for (char c : s) {
        const int v = decodeChar(c);
        if (v < 0) return std::nullopt;
        bits = (bits << 5) | uint64_t(v);
    }
    NetworkCode code;
    code.networkId = uint32_t(bits >> 28);
    code.secret = uint32_t(bits & kSecretMask);
    if (code.networkId == 0) return std::nullopt;
    return code;
}

} // namespace kettle::netcode

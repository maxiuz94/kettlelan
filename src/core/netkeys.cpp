#include "netkeys.h"

#include <cstdlib>
#include <vector>

#include "monocypher.h"
#include "random.h"

namespace kettle::netkeys {

namespace {

constexpr char kSalt[] = "KettleLAN network keys v3"; // fixed, public; codes are random anyway
constexpr size_t kMaxName = 60;
constexpr char kHex[] = "0123456789abcdef";

std::string toHex(const uint8_t* data, size_t size) {
    std::string out(size * 2, '0');
    for (size_t i = 0; i < size; ++i) {
        out[2 * i] = kHex[data[i] >> 4];
        out[2 * i + 1] = kHex[data[i] & 0xF];
    }
    return out;
}

std::optional<std::vector<uint8_t>> fromHex(std::string_view hex) {
    if (hex.size() % 2) return std::nullopt;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = digit(hex[2 * i]), lo = digit(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = uint8_t(hi << 4 | lo);
    }
    return out;
}

// Cuts a UTF-8 string to at most `max` bytes without splitting a character.
std::string_view cutUtf8(std::string_view s, size_t max) {
    if (s.size() <= max) return s;
    size_t end = max;
    while (end > 0 && (uint8_t(s[end]) & 0xC0) == 0x80) --end;
    return s.substr(0, end);
}

} // namespace

Keys derive(const netcode::NetworkCode& code, Cost cost) {
    const std::string pass = netcode::format(code);
    crypto_argon2_config config{};
    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = cost == Cost::Normal ? 32768 : 8; // 32 MiB (blocks of 1 KiB)
    config.nb_passes = cost == Cost::Normal ? 3 : 1;
    config.nb_lanes = 1;
    crypto_argon2_inputs inputs{};
    inputs.pass = reinterpret_cast<const uint8_t*>(pass.data());
    inputs.pass_size = uint32_t(pass.size());
    inputs.salt = reinterpret_cast<const uint8_t*>(kSalt);
    inputs.salt_size = uint32_t(sizeof(kSalt) - 1);

    std::vector<uint8_t> work(size_t(config.nb_blocks) * 1024);
    uint8_t out[64];
    crypto_argon2(out, sizeof(out), work.data(), config, inputs, crypto_argon2_no_extras);
    crypto_wipe(work.data(), work.size());

    Keys keys;
    keys.tag = toHex(out, 16);
    std::copy(out + 16, out + 32, keys.proofKey.begin());
    std::copy(out + 32, out + 64, keys.nameKey.begin());
    crypto_wipe(out, sizeof(out));
    return keys;
}

std::string sealName(const Keys& keys, std::string_view name) {
    const std::string_view text = cutUtf8(name, kMaxName);
    std::vector<uint8_t> buf(24 + 16 + text.size());
    uint8_t* nonce = buf.data();
    uint8_t* mac = buf.data() + 24;
    uint8_t* cipher = buf.data() + 40;
    random::fill({nonce, 24});
    // the tag is authenticated too: a name cannot be moved to another network's lobby
    crypto_aead_lock(cipher, mac, keys.nameKey.data(), nonce, reinterpret_cast<const uint8_t*>(keys.tag.data()),
                     keys.tag.size(), reinterpret_cast<const uint8_t*>(text.data()), text.size());
    return toHex(buf.data(), buf.size());
}

std::optional<std::string> openName(const Keys& keys, std::string_view sealed) {
    if (sealed.size() > 2 * (40 + kMaxName)) return std::nullopt;
    const auto buf = fromHex(sealed);
    if (!buf || buf->size() < 40) return std::nullopt;
    std::string text(buf->size() - 40, '\0');
    if (crypto_aead_unlock(reinterpret_cast<uint8_t*>(text.data()), buf->data() + 24, keys.nameKey.data(), buf->data(),
                           reinterpret_cast<const uint8_t*>(keys.tag.data()), keys.tag.size(), buf->data() + 40,
                           text.size()) != 0)
        return std::nullopt;
    return text;
}

} // namespace kettle::netkeys

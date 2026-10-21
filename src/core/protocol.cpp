#include "protocol.h"

#include "siphash.h"

namespace kettle::protocol {

// Format: 'K' 'T' 'L' type version address[4] proof[8, little-endian]  = 17 bytes
static constexpr size_t kHelloSize = 17;

std::vector<uint8_t> encode(const HelloMessage& msg) {
    std::vector<uint8_t> out{'K', 'T', 'L', uint8_t(msg.type), msg.version};
    for (uint8_t b : msg.address.bytes()) out.push_back(b);
    for (int i = 0; i < 8; ++i) out.push_back(uint8_t(msg.proof >> (8 * i)));
    return out;
}

std::optional<HelloMessage> decode(std::span<const uint8_t> d) {
    if (d.size() != kHelloSize) return std::nullopt;
    if (d[0] != 'K' || d[1] != 'T' || d[2] != 'L') return std::nullopt;
    if (d[3] != uint8_t(MessageType::Hello) && d[3] != uint8_t(MessageType::HelloAck)) return std::nullopt;
    HelloMessage msg;
    msg.type = MessageType(d[3]);
    msg.version = d[4];
    msg.address = ip::Address4::fromBytes(d.subspan<5, 4>());
    msg.proof = 0;
    for (int i = 7; i >= 0; --i) msg.proof = (msg.proof << 8) | d[9 + i];
    return msg;
}

std::string lobbyTag(const netcode::NetworkCode& code) {
    const crypto::SipKey key{'K', 'e', 't', 't', 'l', 'e', 'L', 'A', 'N', '-', 't', 'a', 'g', '-', 'v', '2'};
    std::array<uint8_t, 8> msg{};
    for (int i = 0; i < 4; ++i) {
        msg[i] = uint8_t(code.networkId >> (8 * i));
        msg[4 + i] = uint8_t(code.secret >> (8 * i));
    }
    const uint64_t h = crypto::siphash24(key, msg);
    static constexpr char hex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 0; i < 16; ++i) out[15 - i] = hex[(h >> (4 * i)) & 0xF];
    return out;
}

uint64_t computeProof(const netcode::NetworkCode& code, uint64_t sender, uint64_t receiver) {
    crypto::SipKey key{'K', 'e', 't', 't', 'l', 'e', 'L', 'A', 0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        key[8 + i] = uint8_t(code.secret >> (8 * i));
        key[12 + i] = uint8_t(code.networkId >> (8 * i));
    }
    std::array<uint8_t, 16> msg{};
    for (int i = 0; i < 8; ++i) {
        msg[i] = uint8_t(sender >> (8 * i));
        msg[8 + i] = uint8_t(receiver >> (8 * i));
    }
    return crypto::siphash24(key, msg);
}

} // namespace kettle::protocol

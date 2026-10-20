// KettleLAN – testy rdzenia (bez Steama). Uruchom: ctest --test-dir build
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

#include "ip.h"
#include "netcode.h"
#include "protocol.h"
#include "siphash.h"

using namespace kettle;

static int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::printf("NIEUDANE: %s (linia %d)\n", #cond, __LINE__);                             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static void testSiphash() {
    crypto::SipKey key;
    for (int i = 0; i < 16; ++i) key[i] = uint8_t(i);
    std::vector<uint8_t> empty;
    CHECK(crypto::siphash24(key, empty) == 0x726fdb47dd0e0e31ull);
    std::vector<uint8_t> msg;
    for (int i = 0; i < 15; ++i) msg.push_back(uint8_t(i));
    CHECK(crypto::siphash24(key, msg) == 0xa129ca6149be45e5ull);
}

static void testNetcode() {
    netcode::NetworkCode c{0xDEADBEEF, 0x0ABCDEF};
    const auto text = netcode::format(c);
    CHECK(text.size() == 3 + 3 * 5); // "KTL-XXXX-XXXX-XXXX"
    CHECK(netcode::parse(text) == c);
    std::string lower;
    for (char ch : text) lower += char(std::tolower(ch));
    CHECK(netcode::parse(lower) == c);
    std::string bare;
    for (char ch : text.substr(4)) if (ch != '-') bare += ch;
    CHECK(netcode::parse(bare) == c);
    CHECK(!netcode::parse("KTL-1234"));
    CHECK(!netcode::parse("KTL-UUUU-UUUU-UUUU"));
}

static void testAddresses() {
    std::set<uint32_t> seen;
    int collisions = 0;
    for (uint64_t id = 76561197960265728ull; id < 76561197960265728ull + 2000; ++id) {
        const auto a = ip::addressForSteamId(id);
        CHECK(ip::kVirtualNet.contains(a));
        CHECK(a != ip::kVirtualNet.network() && a != ip::kVirtualNet.broadcast());
        if (!seen.insert(a.value()).second) ++collisions;
    }
    CHECK(collisions <= 2);
    CHECK(ip::addressForSteamId(76561198000000000ull) == ip::addressForSteamId(76561198000000000ull));
    CHECK(ip::Address4::parse("100.64.18.203")->toString() == "100.64.18.203");
    CHECK(!ip::Address4::parse("100.64.18"));
    CHECK(!ip::Address4::parse("256.1.1.1"));
    CHECK(ip::isGroupDestination(ip::Address4(255, 255, 255, 255)));
    CHECK(ip::isGroupDestination(ip::Address4(100, 127, 255, 255)));
    CHECK(ip::isGroupDestination(ip::Address4(224, 0, 0, 251)));
    CHECK(!ip::isGroupDestination(ip::Address4(100, 64, 1, 1)));
}

static void testIPv4Parse() {
    std::vector<uint8_t> pkt(28, 0);
    pkt[0] = 0x45;
    pkt[3] = 28;
    pkt[12] = 100; pkt[13] = 64; pkt[14] = 1; pkt[15] = 2;
    pkt[16] = 100; pkt[17] = 65; pkt[18] = 3; pkt[19] = 4;
    auto v = ip::parseIPv4(pkt);
    CHECK(v && v->src.toString() == "100.64.1.2" && v->dst.toString() == "100.65.3.4");
    pkt[0] = 0x60;
    CHECK(!ip::parseIPv4(pkt));
    CHECK(!ip::parseIPv4(std::span<const uint8_t>(pkt.data(), 10)));
}

static void testProtocol() {
    netcode::NetworkCode code{12345, 0x1234567};
    protocol::HelloMessage m;
    m.type = protocol::MessageType::HelloAck;
    m.address = ip::Address4(100, 70, 1, 9);
    m.proof = protocol::computeProof(code, 111, 222);
    auto bytes = protocol::encode(m);
    auto back = protocol::decode(bytes);
    CHECK(back && back->type == m.type && back->address == m.address && back->proof == m.proof);
    CHECK(protocol::computeProof(code, 111, 222) != protocol::computeProof(code, 222, 111));
    netcode::NetworkCode other = code;
    other.secret ^= 1;
    CHECK(protocol::computeProof(code, 111, 222) != protocol::computeProof(other, 111, 222));
    bytes[0] = 'X';
    CHECK(!protocol::decode(bytes));

    const auto tag = protocol::lobbyTag(code);
    CHECK(tag.size() == 16);
    CHECK(tag == protocol::lobbyTag(code));
    CHECK(tag != protocol::lobbyTag(other));
}

static void testGenerate() {
    const auto a = netcode::generate();
    const auto b = netcode::generate();
    CHECK(a.networkId != 0 && a.secret != 0 && a.secret <= netcode::kSecretMask);
    CHECK(!(a == b));
    CHECK(netcode::parse(netcode::format(a)) == a);
}

int main() {
    testSiphash();
    testNetcode();
    testAddresses();
    testIPv4Parse();
    testProtocol();
    testGenerate();
    if (failures == 0) std::printf("Wszystkie testy rdzenia OK\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

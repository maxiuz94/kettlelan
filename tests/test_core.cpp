// KettleLAN – core tests (no Steam needed). Run: ctest --test-dir build
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

#include "filter.h"
#include "ip.h"
#include "netcode.h"
#include "protocol.h"
#include "siphash.h"
#include "text.h"

using namespace kettle;

static int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);                                 \
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

// Builds an IPv4 packet with a TCP/UDP header (ports) for the filter tests.
static std::vector<uint8_t> makePacket(uint8_t proto, uint16_t dstPort, uint16_t frag = 0, size_t l4len = 20) {
    std::vector<uint8_t> p(20 + l4len, 0);
    p[0] = 0x45;
    p[2] = uint8_t(p.size() >> 8);
    p[3] = uint8_t(p.size());
    p[6] = uint8_t(frag >> 8);
    p[7] = uint8_t(frag);
    p[9] = proto;
    if (l4len >= 4) {
        p[20] = 0x9C; p[21] = 0x40; // source port 40000
        p[22] = uint8_t(dstPort >> 8);
        p[23] = uint8_t(dstPort);
    }
    return p;
}

static void testFilter() {
    using filter::Verdict;
    CHECK(filter::inspectInbound(makePacket(17, 27960)).verdict == Verdict::Allow);   // Quake 3
    CHECK(filter::inspectInbound(makePacket(6, 25565)).verdict == Verdict::Allow);    // Minecraft
    CHECK(filter::inspectInbound(makePacket(6, 22)).verdict == Verdict::BlockedService);     // SSH
    CHECK(filter::inspectInbound(makePacket(6, 445)).verdict == Verdict::BlockedService);    // SMB
    CHECK(filter::inspectInbound(makePacket(17, 137)).verdict == Verdict::BlockedService);   // NetBIOS
    CHECK(filter::inspectInbound(makePacket(6, 1716)).verdict == Verdict::BlockedService);   // KDE Connect
    CHECK(filter::inspectInbound(makePacket(17, 5353)).verdict == Verdict::BlockedService);  // mDNS
    CHECK(filter::inspectInbound(makePacket(1, 0)).verdict == Verdict::Allow);        // ICMP (ping)
    CHECK(filter::inspectInbound(makePacket(47, 0)).verdict == Verdict::BlockedProtocol); // GRE
    // fragmentation: offset 1 (port overwrite) and a too short first fragment
    CHECK(filter::inspectInbound(makePacket(6, 0, 0x0001, 20)).verdict == Verdict::BlockedFragment);
    CHECK(filter::inspectInbound(makePacket(6, 22, 0x2000, 8)).verdict == Verdict::BlockedFragment);
    CHECK(filter::inspectInbound(makePacket(17, 27960, 0x2000, 8)).verdict == Verdict::Allow);
    CHECK(filter::inspectInbound(makePacket(17, 22, 0x2000, 8)).verdict == Verdict::BlockedService);
    CHECK(filter::inspectInbound(makePacket(6, 0, 0x00B9, 20)).verdict == Verdict::Allow); // later fragment
    CHECK(filter::inspectInbound(std::span<const uint8_t>()).verdict == Verdict::Malformed);
    CHECK(filter::serviceName(22) == "SSH");

    CHECK(filter::isDiscoveryBroadcast(makePacket(17, 5353, 0, 8)));   // mDNS
    CHECK(filter::isDiscoveryBroadcast(makePacket(17, 1716, 0, 8)));   // KDE Connect
    CHECK(!filter::isDiscoveryBroadcast(makePacket(17, 27960, 0, 8))); // Quake 3 – server discovery
    CHECK(!filter::isDiscoveryBroadcast(makePacket(6, 5353)));         // TCP – not relevant
}

static void testSanitize() {
    CHECK(text::sanitize("maxiuZ") == "maxiuZ");
    CHECK(text::sanitize("Grüße, naïve café – 日本") == "Grüße, naïve café – 日本"); // non-ASCII is kept
    CHECK(text::sanitize("bad\x1b[2Jname") == "bad[2Jname");       // ESC removed
    CHECK(text::sanitize("a\xe2\x80\xae" "b") == "ab");               // U+202E (right-to-left override)
    CHECK(text::sanitize("\xc2\x9b" "31m") == "31m");                  // C1 control character
    CHECK(text::sanitize("  spacje  ") == "spacje");
    CHECK(text::sanitize(std::string(200, 'x')).size() == 64);
    CHECK(text::sanitize("\xff\xfeok") == "ok");                    // invalid UTF-8
}

int main() {
    testSiphash();
    testNetcode();
    testAddresses();
    testIPv4Parse();
    testProtocol();
    testGenerate();
    testFilter();
    testSanitize();
    if (failures == 0) std::printf("All core tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

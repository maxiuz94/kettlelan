// KettleLAN – core tests (no Steam needed). Run: ctest --test-dir build
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>
#include <vector>

#include "filter.h"
#include "ip.h"
#include "netcode.h"
#include "netkeys.h"
#include "svcproto.h"
#include "monocypher.h"
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
    netcode::NetworkCode other = code;
    other.secret ^= 1;
    const auto keys = netkeys::derive(code, netkeys::Cost::Test);
    const auto otherKeys = netkeys::derive(other, netkeys::Cost::Test);
    protocol::HelloMessage m;
    m.type = protocol::MessageType::HelloAck;
    m.address = ip::Address4(100, 70, 1, 9);
    m.proof = protocol::computeProof(keys, 111, 222);
    auto bytes = protocol::encode(m);
    auto back = protocol::decode(bytes);
    CHECK(back && back->type == m.type && back->address == m.address && back->proof == m.proof);
    CHECK(protocol::computeProof(keys, 111, 222) != protocol::computeProof(keys, 222, 111));
    CHECK(protocol::computeProof(keys, 111, 222) != protocol::computeProof(otherKeys, 111, 222));
    bytes[0] = 'X';
    CHECK(!protocol::decode(bytes));
}

// Argon2id test vector from RFC 9106, section 5.3 – checks the bundled Monocypher.
static void testArgon2Vector() {
    uint8_t pass[32], salt[16], secret[8], ad[12];
    std::fill(std::begin(pass), std::end(pass), uint8_t(0x01));
    std::fill(std::begin(salt), std::end(salt), uint8_t(0x02));
    std::fill(std::begin(secret), std::end(secret), uint8_t(0x03));
    std::fill(std::begin(ad), std::end(ad), uint8_t(0x04));
    crypto_argon2_config config{CRYPTO_ARGON2_ID, 32, 3, 4};
    crypto_argon2_inputs inputs{pass, salt, sizeof(pass), sizeof(salt)};
    crypto_argon2_extras extras{secret, ad, sizeof(secret), sizeof(ad)};
    std::vector<uint8_t> work(32 * 1024);
    uint8_t out[32];
    crypto_argon2(out, sizeof(out), work.data(), config, inputs, extras);
    const uint8_t expected[32] = {0x0d, 0x64, 0x0d, 0xf5, 0x8d, 0x78, 0x76, 0x6c, 0x08, 0xc0, 0x37,
                                  0xa3, 0x4a, 0x8b, 0x53, 0xc9, 0xd0, 0x1e, 0xf0, 0x45, 0x2d, 0x75,
                                  0xb6, 0x5e, 0xb5, 0x25, 0x20, 0xe9, 0x6b, 0x01, 0xe6, 0x59};
    CHECK(std::equal(std::begin(out), std::end(out), std::begin(expected)));
}

static void testNetworkKeys() {
    const netcode::NetworkCode code{0xCAFE1234, 0x0ABCDEF};
    netcode::NetworkCode other = code;
    other.secret ^= 1;

    // the real parameters: same code -> same keys, any other code -> different keys
    const auto keys = netkeys::derive(code);
    CHECK(keys.tag.size() == 32);
    CHECK(keys.tag == netkeys::derive(code).tag);
    CHECK(keys.proofKey == netkeys::derive(code).proofKey);
    const auto otherKeys = netkeys::derive(other);
    CHECK(keys.tag != otherKeys.tag && keys.proofKey != otherKeys.proofKey && keys.nameKey != otherKeys.nameKey);
    CHECK(keys.tag != netkeys::derive(code, netkeys::Cost::Test).tag);

    // encrypted network name
    const auto sealed = netkeys::sealName(keys, "Quake night");
    CHECK(netkeys::openName(keys, sealed) == std::optional<std::string>("Quake night"));
    CHECK(sealed.find("Quake") == std::string::npos);                    // not readable without the code
    CHECK(netkeys::sealName(keys, "Quake night") != sealed);              // random nonce every time
    CHECK(!netkeys::openName(otherKeys, sealed));                         // wrong key
    netkeys::Keys moved = keys;
    moved.tag = otherKeys.tag;
    CHECK(!netkeys::openName(moved, sealed));                             // copied to another lobby
    std::string changed = sealed;
    changed[changed.size() - 1] = changed.back() == '0' ? '1' : '0';
    CHECK(!netkeys::openName(keys, changed));                             // modified
    CHECK(!netkeys::openName(keys, sealed.substr(0, sealed.size() - 2))); // cut
    CHECK(!netkeys::openName(keys, ""));
    CHECK(!netkeys::openName(keys, "zz"));
    CHECK(!netkeys::openName(keys, std::string(1000, 'a')));
    CHECK(!netkeys::openName(keys, "Quake night"));                       // plain text from an old version

    // nicknames: own context – a sealed network name is not a nickname and the other way round
    const auto nick = netkeys::sealNick(keys, "Bob");
    CHECK(netkeys::openNick(keys, nick) == std::optional<std::string>("Bob"));
    CHECK(!netkeys::openNick(otherKeys, nick));
    CHECK(!netkeys::openName(keys, nick));
    CHECK(!netkeys::openNick(keys, sealed));
    std::string nickChanged = nick;
    nickChanged[nickChanged.size() - 1] = nickChanged.back() == '0' ? '1' : '0';
    CHECK(!netkeys::openNick(keys, nickChanged));
    const auto longNick = netkeys::openNick(keys, netkeys::sealNick(keys, std::string(40, 'x')));
    CHECK(longNick && longNick->size() == 32);
    const auto polish = netkeys::openNick(keys, netkeys::sealNick(keys, "Zażółć gęślą jaźń – bardzo długi nick"));
    CHECK(polish && polish->rfind("Zażółć", 0) == 0);
    CHECK(!netkeys::openNick(keys, std::string(1000, 'a')));

    // long names are cut to 60 bytes without breaking a UTF-8 character
    const std::string longName(30, 'x');
    const auto opened = netkeys::openName(keys, netkeys::sealName(keys, longName + std::string(20, '\xC5') + "ąąąąąąąąąą"));
    CHECK(opened && opened->size() <= 60);
    const auto emoji = netkeys::openName(keys, netkeys::sealName(keys, std::string(59, 'a') + "ą"));
    CHECK(emoji && *emoji == std::string(59, 'a'));
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


// Program ↔ Windows service messages and the packet check done by the service.
static void testServiceProtocol() {
    const ip::Address4 me(100, 80, 46, 79), peer(100, 65, 115, 91);

    // frames survive any split of the byte stream
    std::vector<uint8_t> stream = svc::encodeHello(me);
    const std::vector<uint8_t> pkt(40, 0x45);
    for (auto f : {svc::encode(svc::Type::Packet, pkt), svc::encode(svc::Type::Ready, {}), svc::encodeError(svc::ErrorCode::Busy, "busy")})
        stream.insert(stream.end(), f.begin(), f.end());
    for (size_t chunk : {size_t(1), size_t(2), size_t(7), stream.size()}) {
        svc::FrameReader r;
        std::vector<svc::Frame> got;
        for (size_t i = 0; i < stream.size(); i += chunk) {
            CHECK(r.feed(std::span<const uint8_t>(stream).subspan(i, std::min(chunk, stream.size() - i))));
            while (auto f = r.next()) got.push_back(std::move(*f));
        }
        CHECK(got.size() == 4);
        CHECK(got.size() == 4 && got[0].type == svc::Type::Hello && got[1].payload == pkt && got[2].type == svc::Type::Ready);
        if (got.size() == 4) {
            const auto h = svc::decodeHello(got[0].payload);
            CHECK(h && h->version == svc::kVersion && h->address == me);
            const auto e = svc::decodeError(got[3].payload);
            CHECK(e && e->code == svc::ErrorCode::Busy && e->text == "busy");
        }
    }
    // a broken stream (unknown type) is rejected for good
    svc::FrameReader bad;
    const uint8_t junk[] = {0x99, 0x00, 0x01, 0x00};
    CHECK(!bad.feed(junk));
    CHECK(!bad.next());
    const uint8_t hello[] = {1, 0, 9, 'K', 'T', 'L', 'S', 1, 100, 1, 2, 3};
    CHECK(!bad.feed(hello));
    // hello must be exact
    CHECK(!svc::decodeHello(std::vector<uint8_t>{'K', 'T', 'L', 'X', 1, 100, 1, 2, 3}));
    CHECK(!svc::decodeHello(std::vector<uint8_t>{'K', 'T', 'L', 'S', 1, 100, 1, 2}));
    CHECK(!svc::decodeError({}));

    // packets the service may hand to Windows
    auto ipv4 = [](ip::Address4 src, ip::Address4 dst) {
        std::vector<uint8_t> p(28, 0);
        p[0] = 0x45; p[3] = 28; p[8] = 64; p[9] = 17;
        const auto s = src.bytes(), d = dst.bytes();
        for (int i = 0; i < 4; ++i) { p[12 + i] = s[i]; p[16 + i] = d[i]; }
        return p;
    };
    CHECK(svc::acceptFromProgram(ipv4(peer, me), me));                                  // peer → us
    CHECK(svc::acceptFromProgram(ipv4(peer, ip::Address4(255, 255, 255, 255)), me));    // broadcast
    CHECK(!svc::acceptFromProgram(ipv4(peer, ip::Address4(192, 168, 1, 1)), me));       // to the home network
    CHECK(!svc::acceptFromProgram(ipv4(peer, ip::Address4(8, 8, 8, 8)), me));           // to the internet
    CHECK(!svc::acceptFromProgram(ipv4(ip::Address4(192, 168, 1, 5), me), me));         // source outside KettleLAN
    CHECK(!svc::acceptFromProgram(ipv4(ip::Address4(127, 0, 0, 1), me), me));           // forged localhost
    CHECK(!svc::acceptFromProgram(ipv4(me, me), me));                                   // forged own address
    CHECK(!svc::acceptFromProgram(std::vector<uint8_t>(10, 0x45), me));                 // too short
    std::vector<uint8_t> v6 = ipv4(peer, me);
    v6[0] = 0x60;
    CHECK(!svc::acceptFromProgram(v6, me));                                             // IPv6
    CHECK(!svc::acceptFromProgram(std::vector<uint8_t>(70000, 0x45), me));              // too big
}

int main() {
    testSiphash();
    testNetcode();
    testAddresses();
    testIPv4Parse();
    testProtocol();
    testArgon2Vector();
    testNetworkKeys();
    testServiceProtocol();
    testGenerate();
    testFilter();
    testSanitize();
    if (failures == 0) std::printf("All core tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

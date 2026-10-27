// KettleLAN – security tests: attack scenarios against the network logic and checks that they fail.
//
// Participants:  ME (our computer), ALICE (honest member who knows the code),
//                EVE (in the lobby but without the key), STRANGER (outside the lobby),
//                BOB (honest member of a second network B, unknown to network A).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

#include "router.h"

using namespace kettle;
using router::HelloResult;
using router::InboundResult;

static int failures = 0;
static int checks = 0;
#define EXPECT(cond, what)                                                                         \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            std::printf("  FAILED: %s  [%s, line %d]\n", what, #cond, __LINE__);                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
#define SCENARIO(name) std::printf("- %s\n", name)

constexpr uint64_t ME = 76561198002501818ull;
constexpr uint64_t ALICE = 76561198019664098ull;
constexpr uint64_t EVE = 76561198000000001ull;
constexpr uint64_t STRANGER = 76561198000000002ull;
constexpr uint64_t BOB = 76561198000000003ull;

static const netcode::NetworkCode CODE{0x12345678, 0x0ABCDEF};
static const netcode::NetworkCode OTHER_CODE{0x12345678, 0x0ABCDEE}; // same network id, different key
static const netkeys::Keys KEYS = netkeys::derive(CODE, netkeys::Cost::Test);
static const netkeys::Keys OTHER_KEYS = netkeys::derive(OTHER_CODE, netkeys::Cost::Test);
static const netcode::NetworkCode CODE_B{0x0BADC0DE, 0x0123456}; // a second, unrelated network
static const netkeys::Keys KEYS_B = netkeys::derive(CODE_B, netkeys::Cost::Test);

static ip::Address4 addr(uint64_t id) { return ip::addressForSteamId(id); }

// IPv4 packet with the given protocol and destination port.
static std::vector<uint8_t> packet(ip::Address4 src, ip::Address4 dst, uint8_t proto = 17, uint16_t port = 27960) {
    std::vector<uint8_t> p(40, 0);
    p[0] = 0x45;
    p[3] = uint8_t(p.size());
    p[8] = 64;
    p[9] = proto;
    const auto s = src.bytes(), d = dst.bytes();
    for (int i = 0; i < 4; ++i) { p[12 + i] = s[i]; p[16 + i] = d[i]; }
    p[20] = 0x9C; p[21] = 0x40;
    p[22] = uint8_t(port >> 8);
    p[23] = uint8_t(port);
    return p;
}

static protocol::HelloMessage hello(const netkeys::Keys& keys, uint64_t from, uint64_t to,
                                    ip::Address4 address, uint8_t version = protocol::kVersion) {
    protocol::HelloMessage m;
    m.address = address;
    m.version = version;
    m.proof = protocol::computeProof(keys, from, to);
    return m;
}

// Network A: ME, ALICE and EVE in the lobby; ALICE verified.
static router::NetworkId setUp(router::Router& r) {
    const auto net = *r.addNetwork(CODE, KEYS);
    r.syncMembers(net, {ME, ALICE, EVE});
    r.onHello(ALICE, hello(KEYS, ALICE, ME, addr(ALICE)));
    return net;
}

static HelloResult helloResult(router::Router& r, uint64_t from, const protocol::HelloMessage& m) {
    return r.onHello(from, m).result;
}

int main() {
    std::printf("KettleLAN security tests\n");

    SCENARIO("An honest member with the right code gets verified");
    {
        router::Router r(ME);
        const auto net = *r.addNetwork(CODE, KEYS);
        r.syncMembers(net, {ME, ALICE});
        EXPECT(helloResult(r, ALICE, hello(KEYS, ALICE, ME, addr(ALICE))) == HelloResult::Verified, "first HELLO verifies");
        EXPECT(helloResult(r, ALICE, hello(KEYS, ALICE, ME, addr(ALICE))) == HelloResult::Repeated, "a repeated HELLO breaks nothing");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::Deliver, "their game packet is delivered");
    }

    SCENARIO("EVE is in the lobby but does not know the key – verification fails");
    {
        router::Router r(ME);
        setUp(r);
        EXPECT(helloResult(r, EVE, hello(OTHER_KEYS, EVE, ME, addr(EVE))) == HelloResult::BadProof, "wrong key rejected");
        EXPECT(helloResult(r, EVE, hello(KEYS, EVE, ALICE, addr(EVE))) == HelloResult::BadProof,
               "a proof made for another receiver does not work");
        protocol::HelloMessage guessed = hello(KEYS, EVE, ME, addr(EVE));
        guessed.proof ^= 1;
        EXPECT(helloResult(r, EVE, guessed) == HelloResult::BadProof, "a proof off by one bit is rejected");
        EXPECT(r.onData(EVE, packet(addr(EVE), addr(ME))) == InboundResult::UnknownSender, "her packets are not delivered");
    }

    SCENARIO("EVE replays someone else's proof");
    {
        router::Router r(ME);
        setUp(r);
        // EVE captured ALICE's proof sent to us (in practice encrypted by Steam) and sends it as her own
        EXPECT(helloResult(r, EVE, hello(KEYS, ALICE, ME, addr(EVE))) == HelloResult::BadProof, "ALICE's proof does not work for EVE");
        EXPECT(helloResult(r, EVE, hello(KEYS, ALICE, ME, addr(ALICE))) == HelloResult::BadProof, "not even with ALICE's address");
    }

    SCENARIO("A STRANGER outside the lobby sends valid-looking data");
    {
        router::Router r(ME);
        setUp(r);
        EXPECT(helloResult(r, STRANGER, hello(KEYS, STRANGER, ME, addr(STRANGER))) == HelloResult::UnknownPeer, "HELLO from a stranger rejected");
        EXPECT(r.onData(STRANGER, packet(addr(STRANGER), addr(ME))) == InboundResult::UnknownSender, "their packets rejected");
        EXPECT(r.onData(STRANGER, packet(addr(ALICE), addr(ME))) == InboundResult::UnknownSender,
               "also when pretending to be ALICE");
    }

    SCENARIO("A member tries to take over someone else's address during verification");
    {
        router::Router r(ME);
        const auto net = *r.addNetwork(CODE, KEYS);
        r.syncMembers(net, {ME, ALICE, EVE});
        // EVE knows the code (e.g. she was invited once) but announces ALICE's address
        EXPECT(helloResult(r, EVE, hello(KEYS, EVE, ME, addr(ALICE))) == HelloResult::BadAddress, "someone else's address rejected");
        EXPECT(helloResult(r, EVE, hello(KEYS, EVE, ME, addr(ME))) == HelloResult::BadAddress, "our address rejected");
        EXPECT(r.onData(EVE, packet(addr(ALICE), addr(ME))) == InboundResult::UnknownSender, "still no traffic");
    }

    SCENARIO("A verified member impersonates others (source address spoofing)");
    {
        router::Router r(ME);
        setUp(r);
        r.onHello(EVE, hello(KEYS, EVE, ME, addr(EVE))); // EVE verified too
        EXPECT(r.onData(ALICE, packet(addr(EVE), addr(ME))) == InboundResult::Spoofed, "ALICE as EVE – rejected");
        EXPECT(r.onData(ALICE, packet(addr(ME), addr(ME))) == InboundResult::Spoofed, "ALICE as us – rejected");
        EXPECT(r.onData(ALICE, packet(ip::Address4(192, 168, 8, 1), addr(ME))) == InboundResult::Spoofed,
               "ALICE as the home router – rejected");
        EXPECT(r.onData(ALICE, packet(ip::Address4(127, 0, 0, 1), addr(ME))) == InboundResult::Spoofed,
               "ALICE as localhost – rejected");
    }

    SCENARIO("A member tries to reach further through us (home network, internet, others)");
    {
        router::Router r(ME);
        setUp(r);
        EXPECT(r.onData(ALICE, packet(addr(ALICE), ip::Address4(192, 168, 8, 153))) == InboundResult::NotForUs,
               "to a NAS on the home network – rejected");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), ip::Address4(127, 0, 0, 1))) == InboundResult::NotForUs,
               "to localhost – rejected");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), ip::Address4(8, 8, 8, 8))) == InboundResult::NotForUs,
               "to the internet – rejected");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(EVE))) == InboundResult::NotForUs,
               "to another member through us – rejected");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), ip::Address4(255, 255, 255, 255))) == InboundResult::Deliver,
               "broadcast (LAN server discovery) – allowed");
    }

    SCENARIO("A member tries to connect to services on our computer");
    {
        router::Router r(ME);
        setUp(r);
        const auto a = addr(ALICE), m = addr(ME);
        EXPECT(r.onData(ALICE, packet(a, m, 6, 22)) == InboundResult::Filtered, "SSH blocked");
        EXPECT(r.onData(ALICE, packet(a, m, 6, 445)) == InboundResult::Filtered, "Samba blocked");
        EXPECT(r.onData(ALICE, packet(a, m, 6, 3389)) == InboundResult::Filtered, "RDP blocked");
        EXPECT(r.onData(ALICE, packet(a, m, 6, 5432)) == InboundResult::Filtered, "PostgreSQL blocked");
        EXPECT(r.onData(ALICE, packet(a, ip::Address4(224, 0, 0, 251), 17, 5353)) == InboundResult::Filtered,
               "mDNS queries blocked");
        EXPECT(r.onData(ALICE, packet(a, m, 47, 0)) == InboundResult::Filtered, "other protocol (GRE) blocked");
        EXPECT(r.onData(ALICE, packet(a, m, 17, 27960)) == InboundResult::Deliver, "Quake 3 allowed");
        EXPECT(r.onData(ALICE, packet(a, m, 6, 25565)) == InboundResult::Deliver, "Minecraft allowed");
        auto ping = packet(a, m, 1, 0);
        ping[20] = 8; // echo request
        EXPECT(r.onData(ALICE, ping) == InboundResult::Deliver, "ping allowed");
        auto redirect = packet(a, m, 1, 0);
        redirect[20] = 5; // "use another router"
        EXPECT(r.onData(ALICE, redirect) == InboundResult::Filtered, "ICMP redirect blocked");
        r.setProtectServices(false);
        EXPECT(r.onData(ALICE, packet(a, m, 6, 22)) == InboundResult::Deliver, "SSH passes once protection is off");
    }

    SCENARIO("Our system sends nothing outside the network or to unverified people");
    {
        router::Router r(ME);
        setUp(r);
        const auto m = addr(ME);
        EXPECT(r.recipients(packet(m, addr(ALICE))) == std::vector<uint64_t>{ALICE}, "to ALICE – only to her");
        EXPECT(r.recipients(packet(m, addr(EVE))).empty(), "to unverified EVE – nothing");
        EXPECT(r.recipients(packet(ip::Address4(192, 168, 8, 2), addr(ALICE))).empty(),
               "packet with our home address – not sent (no leak)");
        EXPECT(r.recipients(packet(m, ip::Address4(255, 255, 255, 255))) == std::vector<uint64_t>{ALICE},
               "broadcast – verified members only");
        EXPECT(r.recipients(packet(m, ip::Address4(224, 0, 0, 251), 17, 5353)).empty(),
               "mDNS announcements (computer name) – not sent");
        EXPECT(r.recipients(packet(m, ip::Address4(100, 127, 255, 255), 17, 137)).empty(),
               "NetBIOS announcements – not sent");
        EXPECT(r.recipients(packet(m, ip::Address4(100, 127, 255, 255), 17, 27960)) == std::vector<uint64_t>{ALICE},
               "game server discovery – sent");
    }

    SCENARIO("Someone who left the lobby loses access immediately");
    {
        router::Router r(ME);
        const auto net = setUp(r);
        const auto sync = r.syncMembers(net, {ME, EVE});
        EXPECT(sync.left == std::vector<uint64_t>{ALICE} && sync.goneEverywhere == std::vector<uint64_t>{ALICE},
               "ALICE leaving is detected");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::UnknownSender, "her packets rejected");
        EXPECT(r.recipients(packet(addr(ME), addr(ALICE))).empty(), "nothing is sent to her");
        r.syncMembers(net, {ME, EVE, ALICE});
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::UnknownSender,
               "after coming back she must verify again");
    }

    SCENARIO("Nothing passes after leaving the network");
    {
        router::Router r(ME);
        setUp(r);
        r.clear();
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::UnknownSender, "nothing accepted");
        EXPECT(r.recipients(packet(addr(ME), ip::Address4(255, 255, 255, 255))).empty(), "nothing sent");
        EXPECT(helloResult(r, ALICE, hello(KEYS, ALICE, ME, addr(ALICE))) == HelloResult::NoNetwork, "no verification");
    }

    SCENARIO("A different program version is not let in");
    {
        router::Router r(ME);
        const auto net = *r.addNetwork(CODE, KEYS);
        r.syncMembers(net, {ME, ALICE});
        EXPECT(helloResult(r, ALICE, hello(KEYS, ALICE, ME, addr(ALICE), protocol::kVersion + 1)) == HelloResult::BadVersion,
               "different protocol version rejected");
    }

    SCENARIO("Two people with the same address (collision) – neither takes over the other's traffic");
    {
        // find two SteamIDs giving the same address (birthday paradox, a few thousand tries)
        std::map<uint32_t, uint64_t> seen;
        uint64_t a = 0, b = 0;
        for (uint64_t id = 76561198100000000ull; a == 0; ++id) {
            const auto [it, ok] = seen.emplace(addr(id).value(), id);
            if (!ok) { a = it->second; b = id; }
        }
        router::Router r(ME);
        const auto net = *r.addNetwork(CODE, KEYS);
        r.syncMembers(net, {ME, a, b});
        EXPECT(helloResult(r, a, hello(KEYS, a, ME, addr(a))) == HelloResult::Verified, "first person verified");
        EXPECT(helloResult(r, b, hello(KEYS, b, ME, addr(b))) == HelloResult::Conflict, "second one – conflict");
        EXPECT(r.onData(b, packet(addr(b), addr(ME))) == InboundResult::UnknownSender, "the second cannot send as the first");
        EXPECT(r.recipients(packet(addr(ME), addr(a))) == std::vector<uint64_t>{a}, "traffic goes to the first");
    }

    SCENARIO("Broken and oversized packets");
    {
        router::Router r(ME);
        setUp(r);
        std::vector<uint8_t> huge(70000, 0);
        huge[0] = 0x45;
        EXPECT(r.onData(ALICE, huge) == InboundResult::Malformed, "over 64 KB rejected");
        EXPECT(r.onData(ALICE, std::vector<uint8_t>(10, 0x45)) == InboundResult::Malformed, "too short rejected");
        auto ipv6 = packet(addr(ALICE), addr(ME));
        ipv6[0] = 0x60;
        EXPECT(r.onData(ALICE, ipv6) == InboundResult::Malformed, "IPv6 rejected");
        auto lying = packet(addr(ALICE), addr(ME));
        lying[2] = 0xFF; // declared length larger than the packet
        EXPECT(r.onData(ALICE, lying) == InboundResult::Malformed, "false length rejected");
    }

    SCENARIO("HELLO retries are limited (no flooding)");
    {
        router::Router r(ME);
        const auto net = *r.addNetwork(CODE, KEYS);
        r.syncMembers(net, {ME, EVE});
        auto t = router::Router::Clock::now();
        int sent = 0;
        for (int i = 0; i < 100; ++i, t += std::chrono::seconds(3))
            sent += int(r.dueHellos(t, std::chrono::seconds(2), 15).size());
        EXPECT(sent == 15, "at most 15 attempts to someone who does not answer");
    }


    // ------------------------------------------------------------ several networks at once
    SCENARIO("Two networks: a proof of network A does not verify anybody in network B");
    {
        router::Router r(ME);
        const auto a = *r.addNetwork(CODE, KEYS);
        const auto b = *r.addNetwork(CODE_B, KEYS_B);
        r.syncMembers(a, {ME, ALICE});
        r.syncMembers(b, {ME, ALICE, BOB});
        const auto first = r.onHello(ALICE, hello(KEYS, ALICE, ME, addr(ALICE)));
        EXPECT(first.result == HelloResult::Verified && first.network == a, "ALICE verified in A with the A key");
        bool inB = false;
        for (const auto& p : r.peers(b)) if (p.steamId == ALICE) inB = p.verified;
        EXPECT(!inB, "…but not verified in B");
        EXPECT(helloResult(r, BOB, hello(KEYS, BOB, ME, addr(BOB))) == HelloResult::BadProof,
               "BOB (only in B's lobby) cannot use the A key");
        const auto second = r.onHello(ALICE, hello(KEYS_B, ALICE, ME, addr(ALICE)));
        EXPECT(second.result == HelloResult::Verified && second.network == b, "ALICE verified in B with the B key");
    }

    SCENARIO("Two networks with the same network id but different keys stay separate");
    {
        router::Router r(ME);
        const auto a = *r.addNetwork(CODE, KEYS);
        const auto other = *r.addNetwork(OTHER_CODE, OTHER_KEYS);
        r.syncMembers(a, {ME});
        r.syncMembers(other, {ME, EVE});
        const auto res = r.onHello(EVE, hello(OTHER_KEYS, EVE, ME, addr(EVE)));
        EXPECT(res.result == HelloResult::Verified && res.network == other, "EVE verified only in her own network");
        for (const auto& p : r.peers(a)) EXPECT(p.steamId != EVE, "EVE is not a member of A");
    }

    SCENARIO("A member of network A cannot reach members of network B through us");
    {
        router::Router r(ME);
        const auto a = *r.addNetwork(CODE, KEYS);
        const auto b = *r.addNetwork(CODE_B, KEYS_B);
        r.syncMembers(a, {ME, ALICE});
        r.syncMembers(b, {ME, BOB});
        r.onHello(ALICE, hello(KEYS, ALICE, ME, addr(ALICE)));
        r.onHello(BOB, hello(KEYS_B, BOB, ME, addr(BOB)));
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(BOB))) == InboundResult::NotForUs, "ALICE → BOB through us – rejected");
        EXPECT(r.onData(ALICE, packet(addr(BOB), addr(ME))) == InboundResult::Spoofed, "ALICE pretending to be BOB – rejected");
        EXPECT(r.onData(BOB, packet(addr(BOB), addr(ME))) == InboundResult::Deliver, "BOB → us – delivered");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::Deliver, "ALICE → us – delivered");
        EXPECT(r.recipients(packet(addr(ALICE), addr(BOB))).empty(), "we never send on someone else's packet");
        const auto toBob = r.recipients(packet(addr(ME), addr(BOB)));
        EXPECT(toBob.size() == 1 && toBob[0] == BOB, "our own packet to BOB goes to BOB only");
    }

    SCENARIO("A nickname belongs to the network where its owner proved the key");
    {
        router::Router r(ME);
        const auto a = *r.addNetwork(CODE, KEYS);
        const auto b = *r.addNetwork(CODE_B, KEYS_B);
        r.syncMembers(a, {ME, ALICE, EVE});
        r.syncMembers(b, {ME, ALICE});
        EXPECT(!r.isVerified(a, ALICE), "before HELLO ALICE is not verified (her nickname is not shown yet)");
        r.onHello(ALICE, hello(KEYS, ALICE, ME, addr(ALICE)));
        EXPECT(r.isVerified(a, ALICE), "after HELLO with the A key – verified in A");
        EXPECT(!r.isVerified(b, ALICE), "…but not in B");
        r.onHello(EVE, hello(OTHER_KEYS, EVE, ME, addr(EVE)));
        EXPECT(!r.isVerified(a, EVE), "EVE without the key is never verified – a copied nickname is not shown");
        EXPECT(!r.isVerified(9999, ALICE), "unknown network – nobody is verified");
    }

    SCENARIO("Broadcasts reach the verified members of both networks, each person once");
    {
        router::Router r(ME);
        const auto a = *r.addNetwork(CODE, KEYS);
        const auto b = *r.addNetwork(CODE_B, KEYS_B);
        r.syncMembers(a, {ME, ALICE, EVE});
        r.syncMembers(b, {ME, ALICE, BOB});
        r.onHello(ALICE, hello(KEYS, ALICE, ME, addr(ALICE)));
        r.onHello(ALICE, hello(KEYS_B, ALICE, ME, addr(ALICE)));
        r.onHello(BOB, hello(KEYS_B, BOB, ME, addr(BOB)));
        auto out = r.recipients(packet(addr(ME), ip::Address4(255, 255, 255, 255)));
        std::sort(out.begin(), out.end());
        std::vector<uint64_t> expected{ALICE, BOB};
        std::sort(expected.begin(), expected.end());
        EXPECT(out == expected, "ALICE (in both) once, BOB once, unverified EVE not at all");
    }

    SCENARIO("Leaving network A keeps the people who are also in network B");
    {
        router::Router r(ME);
        const auto a = *r.addNetwork(CODE, KEYS);
        const auto b = *r.addNetwork(CODE_B, KEYS_B);
        r.syncMembers(a, {ME, ALICE, EVE});
        r.syncMembers(b, {ME, ALICE, BOB});
        r.onHello(ALICE, hello(KEYS, ALICE, ME, addr(ALICE)));
        r.onHello(ALICE, hello(KEYS_B, ALICE, ME, addr(ALICE)));
        r.onHello(BOB, hello(KEYS_B, BOB, ME, addr(BOB)));
        const auto gone = r.removeNetwork(a);
        EXPECT(gone == std::vector<uint64_t>{EVE}, "only EVE (A only) is gone – her session can be closed");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::Deliver, "ALICE still reachable through B");
        EXPECT(r.code(a) == std::nullopt && r.code(b) == CODE_B, "network A forgotten, B kept");
        const auto sync = r.syncMembers(b, {ME, BOB});
        EXPECT(sync.goneEverywhere == std::vector<uint64_t>{ALICE}, "ALICE leaving B too – gone everywhere");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::UnknownSender, "…and her packets are rejected");
    }

    SCENARIO("Verified in A, only in the lobby of B: leaving A removes the trust");
    {
        router::Router r(ME);
        const auto a = *r.addNetwork(CODE, KEYS);
        const auto b = *r.addNetwork(CODE_B, KEYS_B);
        r.syncMembers(a, {ME, ALICE});
        r.syncMembers(b, {ME, ALICE}); // ALICE sits in B's lobby but never proves B's key
        r.onHello(ALICE, hello(KEYS, ALICE, ME, addr(ALICE)));
        r.removeNetwork(a);
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::UnknownSender,
               "without a verified network ALICE has no access");
        EXPECT(r.recipients(packet(addr(ME), addr(ALICE))).empty(), "nothing is sent to her");
    }

    SCENARIO("At most kMaxNetworks networks at the same time");
    {
        router::Router r(ME);
        bool allAccepted = true;
        for (uint32_t i = 0; i < router::Router::kMaxNetworks; ++i) {
            const netcode::NetworkCode c{0x10000000u + i, 0x0123456};
            allAccepted = allAccepted && r.addNetwork(c, netkeys::derive(c, netkeys::Cost::Test)).has_value();
        }
        EXPECT(allAccepted, "up to the limit – accepted");
        EXPECT(!r.addNetwork(OTHER_CODE, OTHER_KEYS).has_value(), "one more – refused");
        r.removeNetwork(r.networks().front());
        EXPECT(r.addNetwork(OTHER_CODE, OTHER_KEYS).has_value(), "after leaving one, another can be added");
    }

    SCENARIO("HELLO is sent separately for each network the person is in");
    {
        router::Router r(ME);
        const auto a = *r.addNetwork(CODE, KEYS);
        const auto b = *r.addNetwork(CODE_B, KEYS_B);
        r.syncMembers(a, {ME, ALICE});
        r.syncMembers(b, {ME, ALICE});
        const auto due = r.dueHellos(router::Router::Clock::now(), std::chrono::seconds(2), 15);
        EXPECT(due.size() == 2, "one HELLO per network");
        const auto ha = r.makeHello(a, ALICE, false), hb = r.makeHello(b, ALICE, false);
        EXPECT(ha && hb && ha->proof != hb->proof, "different proofs for A and B");
        EXPECT(!r.makeHello(9999, ALICE, false), "no proof for an unknown network");
    }

    // ------------------------------------------------------------ blocking a person
    SCENARIO("A blocked member gets nothing from us and nothing of theirs reaches the system");
    {
        router::Router r(ME);
        const auto net = setUp(r);
        r.syncMembers(net, {ME, ALICE, EVE, BOB});
        r.onHello(BOB, hello(KEYS, BOB, ME, addr(BOB)));
        const auto blockedNow = r.setBlocked({ALICE});
        EXPECT(blockedNow.size() == 1 && blockedNow[0] == ALICE, "ALICE reported as blocked just now (close her session)");
        EXPECT(r.isBlocked(ALICE) && !r.isBlocked(BOB), "only ALICE is blocked");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::Blocked, "her packets are dropped");
        EXPECT(r.recipients(packet(addr(ME), addr(ALICE))).empty(), "we send her nothing");
        const auto everyone = r.recipients(packet(addr(ME), ip::Address4(100, 127, 255, 255)));
        EXPECT(everyone.size() == 1 && everyone[0] == BOB, "broadcasts go to BOB only");
        EXPECT(helloResult(r, ALICE, hello(KEYS, ALICE, ME, addr(ALICE))) == HelloResult::Blocked,
               "her valid proof does not verify her again");
        for (const auto& [n, id] : r.dueHellos(router::Router::Clock::now() + std::chrono::hours(1), std::chrono::seconds(2), 15))
            EXPECT(id != ALICE, "no HELLO to her");
        bool shownBlocked = false;
        for (const auto& p : r.peers(net)) if (p.steamId == ALICE) shownBlocked = p.blocked && !p.verified;
        EXPECT(shownBlocked, "the list shows her as blocked, not verified");
        EXPECT(r.onData(BOB, packet(addr(BOB), addr(ME))) == InboundResult::Deliver, "BOB is not affected");
    }

    SCENARIO("Unblocking starts the verification again");
    {
        router::Router r(ME);
        const auto net = setUp(r);
        r.setBlocked({ALICE});
        EXPECT(r.setBlocked({}).empty(), "nobody is blocked just now");
        bool helloDue = false;
        for (const auto& [n, id] : r.dueHellos(router::Router::Clock::now(), std::chrono::seconds(2), 15))
            if (id == ALICE && n == net) helloDue = true;
        EXPECT(helloDue, "a new HELLO to ALICE");
        EXPECT(helloResult(r, ALICE, hello(KEYS, ALICE, ME, addr(ALICE))) == HelloResult::Verified, "she is verified again");
        EXPECT(r.onData(ALICE, packet(addr(ALICE), addr(ME))) == InboundResult::Deliver, "her packets arrive again");
    }

    SCENARIO("Blocking works in every network and before the person joins");
    {
        router::Router r(ME);
        const auto a = *r.addNetwork(CODE, KEYS);
        const auto b = *r.addNetwork(CODE_B, KEYS_B);
        r.syncMembers(a, {ME, ALICE});
        r.syncMembers(b, {ME, ALICE});
        r.onHello(ALICE, hello(KEYS, ALICE, ME, addr(ALICE)));
        r.onHello(ALICE, hello(KEYS_B, ALICE, ME, addr(ALICE)));
        r.setBlocked({ALICE, BOB});
        EXPECT(!r.isVerified(a, ALICE) && !r.isVerified(b, ALICE), "ALICE is not verified in A nor in B");
        r.syncMembers(b, {ME, ALICE, BOB});
        EXPECT(helloResult(r, BOB, hello(KEYS_B, BOB, ME, addr(BOB))) == HelloResult::Blocked, "BOB, blocked earlier, joins B – blocked");
        EXPECT(r.setBlocked({ME}).empty() && !r.isBlocked(ME), "blocking ourselves is ignored");
    }

    // ------------------------------------------------------------ flood protection
    SCENARIO("A flood from one member is cut off, other members are not affected");
    {
        router::Router r(ME);
        const auto t = router::Router::Clock::now();
        int accepted = 0;
        for (int i = 0; i < 30000; ++i) accepted += r.admit(ALICE, t) ? 1 : 0;
        EXPECT(accepted == int(router::Router::kRateBurst), "ALICE: the burst is accepted, the rest is dropped");
        EXPECT(r.admit(BOB, t), "BOB is not affected");
        int later = 0;
        for (int i = 0; i < 30000; ++i) later += r.admit(ALICE, t + std::chrono::seconds(1)) ? 1 : 0;
        EXPECT(later == int(router::Router::kRatePerSecond), "a second later ALICE may send the per-second amount again");
        int normal = 0; // a busy game: 1000 messages a second for 30 s
        for (int i = 0; i < 30000; ++i) normal += r.admit(EVE, t + std::chrono::milliseconds(i)) ? 1 : 0;
        EXPECT(normal == 30000, "normal game traffic is never cut off");
    }

    std::printf("\n%d checks, %d failed\n", checks, failures);
    if (failures == 0) std::printf("All security tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

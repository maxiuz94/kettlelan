// KettleLAN – fuzzing of every function that reads data coming from other people.
// Built with AddressSanitizer + UndefinedBehaviorSanitizer in CI: any memory error stops the test.
//   ./kettlelan_fuzz [iterations]
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "filter.h"
#include "ip.h"
#include "netcode.h"
#include "netkeys.h"
#include "protocol.h"
#include "text.h"

using namespace kettle;

// A valid IPv4/UDP packet as the starting point for mutations (seeded fuzzing).
static std::vector<uint8_t> seedPacket(std::mt19937& rng) {
    std::vector<uint8_t> p(28 + rng() % 64, 0);
    p[0] = 0x45;
    p[2] = uint8_t(p.size() >> 8);
    p[3] = uint8_t(p.size());
    p[9] = (rng() % 3 == 0) ? 6 : 17;
    p[12] = 100; p[13] = 80; p[14] = 46; p[15] = 79;
    p[16] = 100; p[17] = 65; p[18] = 115; p[19] = 91;
    p[22] = uint8_t(rng()); p[23] = uint8_t(rng());
    return p;
}

static void mutate(std::vector<uint8_t>& d, std::mt19937& rng) {
    const int n = 1 + int(rng() % 8);
    for (int i = 0; i < n; ++i) {
        switch (rng() % 5) {
        case 0: if (!d.empty()) d[rng() % d.size()] = uint8_t(rng()); break;           // random byte
        case 1: if (!d.empty()) d[rng() % d.size()] ^= uint8_t(1u << (rng() % 8)); break; // single bit
        case 2: if (!d.empty()) d.resize(rng() % d.size()); break;                      // truncate
        case 3: d.push_back(uint8_t(rng())); break;                                     // append
        case 4: if (d.size() > 3) { const uint8_t v[] = {0x00, 0xFF, 0x7F, 0x80}; d[rng() % 4] = v[rng() % 4]; } break;
        }
    }
}

int main(int argc, char** argv) {
    const long iterations = argc > 1 ? std::atol(argv[1]) : 2000000;
    std::mt19937 rng(12345);
    long allowed = 0, blocked = 0, parsed = 0, codes = 0;

    const auto fuzzKeys = netkeys::derive(netcode::NetworkCode{1, 2}, netkeys::Cost::Test);
    for (long it = 0; it < iterations; ++it) {
        std::vector<uint8_t> d;
        if (it % 3 == 0) {
            d.resize(rng() % 128);
            for (auto& b : d) b = uint8_t(rng());
        } else {
            d = seedPacket(rng);
            mutate(d, rng);
        }
        const std::span<const uint8_t> s(d.data(), d.size());

        if (ip::parseIPv4(s)) ++parsed;
        const auto r = filter::inspectInbound(s);
        (r.verdict == filter::Verdict::Allow ? allowed : blocked)++;
        (void)filter::isDiscoveryBroadcast(s);
        (void)protocol::decode(s);

        // text: names and network names from strangers
        const std::string str(d.begin(), d.end());
        const std::string clean = text::sanitize(str, 1 + rng() % 80);
        for (unsigned char c : clean)
            if (c < 0x20 || c == 0x7F) { std::printf("ERROR: control character left after sanitizing\n"); return 1; }

        // network codes typed / pasted by the user
        if (netcode::parse(str)) ++codes;
        // encrypted network names from lobby data (text from strangers)
        if (netkeys::openName(fuzzKeys, str)) { std::printf("ERROR: a random string opened as a network name\n"); return 1; }
        std::string code = "KTL-";
        for (int i = 0; i < 14; ++i) code += char(32 + rng() % 95);
        if (const auto c = netcode::parse(code)) {
            if (!(netcode::parse(netcode::format(*c)) == *c)) { std::printf("ERROR: code does not round-trip\n"); return 1; }
        }
    }
    std::printf("Fuzzing OK: %ld inputs, valid IPv4: %ld, allowed: %ld, blocked: %ld, codes: %ld\n",
                iterations, parsed, allowed, blocked, codes);
    return 0;
}

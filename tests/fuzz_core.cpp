// KettleLAN – test odporności (fuzzing) funkcji czytających dane od innych osób.
// Budowany z AddressSanitizer + UndefinedBehaviorSanitizer: każdy błąd pamięci przerywa test.
//   ./kettlelan_fuzz [liczba_iteracji]
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "filter.h"
#include "ip.h"
#include "netcode.h"
#include "protocol.h"
#include "text.h"

using namespace kettle;

// Poprawny pakiet IPv4/UDP jako punkt wyjścia do mutacji (fuzzing „z ziarnem”).
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
        case 0: if (!d.empty()) d[rng() % d.size()] = uint8_t(rng()); break;           // losowy bajt
        case 1: if (!d.empty()) d[rng() % d.size()] ^= uint8_t(1u << (rng() % 8)); break; // jeden bit
        case 2: if (!d.empty()) d.resize(rng() % d.size()); break;                      // ucięcie
        case 3: d.push_back(uint8_t(rng())); break;                                     // dopisanie
        case 4: if (d.size() > 3) { const uint8_t v[] = {0x00, 0xFF, 0x7F, 0x80}; d[rng() % 4] = v[rng() % 4]; } break;
        }
    }
}

int main(int argc, char** argv) {
    const long iterations = argc > 1 ? std::atol(argv[1]) : 2000000;
    std::mt19937 rng(12345);
    long allowed = 0, blocked = 0, parsed = 0, codes = 0;

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

        // tekst: nicki i nazwy sieci od obcych
        const std::string str(d.begin(), d.end());
        const std::string clean = text::sanitize(str, 1 + rng() % 80);
        for (unsigned char c : clean)
            if (c < 0x20 || c == 0x7F) { std::printf("BŁĄD: znak sterujący po oczyszczeniu\n"); return 1; }

        // kody sieci wpisywane / wklejane przez użytkownika
        if (netcode::parse(str)) ++codes;
        std::string code = "KTL-";
        for (int i = 0; i < 14; ++i) code += char(32 + rng() % 95);
        if (const auto c = netcode::parse(code)) {
            if (!(netcode::parse(netcode::format(*c)) == *c)) { std::printf("BŁĄD: kod nie przechodzi w obie strony\n"); return 1; }
        }
    }
    std::printf("Fuzzing OK: %ld prób, parsowalne IPv4: %ld, przepuszczone: %ld, zablokowane: %ld, kody: %ld\n",
                iterations, parsed, allowed, blocked, codes);
    return 0;
}

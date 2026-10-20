// KettleLAN – testy bezpieczeństwa: scenariusze ataków na sieć i sprawdzenie, że zostają odparte.
//
// Uczestnicy:  JA (nasz komputer), ADAM (uczciwy członek znający kod),
//              EWA (jest w lobby, ale nie zna klucza), OBCY (spoza lobby).
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
#define EXPECT(cond, opis)                                                                         \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(cond)) {                                                                             \
            std::printf("  NIEUDANE: %s  [%s, linia %d]\n", opis, #cond, __LINE__);                \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
#define SCENARIUSZ(nazwa) std::printf("- %s\n", nazwa)

constexpr uint64_t JA = 76561198002501818ull;
constexpr uint64_t ADAM = 76561198019664098ull;
constexpr uint64_t EWA = 76561198000000001ull;
constexpr uint64_t OBCY = 76561198000000002ull;

static const netcode::NetworkCode KOD{0x12345678, 0x0ABCDEF};
static const netcode::NetworkCode INNY_KOD{0x12345678, 0x0ABCDEE}; // ten sam identyfikator, inny klucz

static ip::Address4 adres(uint64_t id) { return ip::addressForSteamId(id); }

// Pakiet IPv4 z danym protokołem i portem docelowym.
static std::vector<uint8_t> pakiet(ip::Address4 src, ip::Address4 dst, uint8_t proto = 17, uint16_t port = 27960) {
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

static protocol::HelloMessage hello(const netcode::NetworkCode& kod, uint64_t od, uint64_t doKogo,
                                    ip::Address4 adr, uint8_t wersja = protocol::kVersion) {
    protocol::HelloMessage m;
    m.address = adr;
    m.version = wersja;
    m.proof = protocol::computeProof(kod, od, doKogo);
    return m;
}

// Sieć: JA, ADAM i EWA w lobby; ADAM zweryfikowany.
static void siec(router::Router& r) {
    r.setCode(KOD);
    r.syncMembers({JA, ADAM, EWA});
    r.onHello(ADAM, hello(KOD, ADAM, JA, adres(ADAM)));
}

int main() {
    std::printf("Testy bezpieczeństwa KettleLAN\n");

    SCENARIUSZ("Uczciwy członek z poprawnym kodem zostaje zweryfikowany");
    {
        router::Router r(JA);
        r.setCode(KOD);
        r.syncMembers({JA, ADAM});
        EXPECT(r.onHello(ADAM, hello(KOD, ADAM, JA, adres(ADAM))) == HelloResult::Verified, "pierwsze HELLO weryfikuje");
        EXPECT(r.onHello(ADAM, hello(KOD, ADAM, JA, adres(ADAM))) == HelloResult::Repeated, "kolejne HELLO nic nie psuje");
        EXPECT(r.onData(ADAM, pakiet(adres(ADAM), adres(JA))) == InboundResult::Deliver, "jego pakiet gry dociera");
    }

    SCENARIUSZ("EWA jest w lobby, ale nie zna klucza – nie przejdzie weryfikacji");
    {
        router::Router r(JA);
        siec(r);
        EXPECT(r.onHello(EWA, hello(INNY_KOD, EWA, JA, adres(EWA))) == HelloResult::BadProof, "zły klucz odrzucony");
        EXPECT(r.onHello(EWA, hello(KOD, EWA, ADAM, adres(EWA))) == HelloResult::BadProof,
               "dowód wyliczony dla innego odbiorcy nie działa");
        protocol::HelloMessage zgadywany = hello(KOD, EWA, JA, adres(EWA));
        zgadywany.proof ^= 1;
        EXPECT(r.onHello(EWA, zgadywany) == HelloResult::BadProof, "dowód różniący się 1 bitem odrzucony");
        EXPECT(r.onData(EWA, pakiet(adres(EWA), adres(JA))) == InboundResult::UnknownSender, "jej pakiety nie docierają");
    }

    SCENARIUSZ("EWA odtwarza (replay) cudzy dowód");
    {
        router::Router r(JA);
        siec(r);
        // EWA podsłuchała dowód ADAMA wysłany do nas (w praktyce szyfrowany przez Steam) i wysyła go jako swój
        EXPECT(r.onHello(EWA, hello(KOD, ADAM, JA, adres(EWA))) == HelloResult::BadProof, "dowód ADAMA nie działa dla EWY");
        EXPECT(r.onHello(EWA, hello(KOD, ADAM, JA, adres(ADAM))) == HelloResult::BadProof, "nawet z adresem ADAMA");
    }

    SCENARIUSZ("OBCY spoza lobby wysyła poprawnie wyglądające dane");
    {
        router::Router r(JA);
        siec(r);
        EXPECT(r.onHello(OBCY, hello(KOD, OBCY, JA, adres(OBCY))) == HelloResult::UnknownPeer, "HELLO od obcego odrzucone");
        EXPECT(r.onData(OBCY, pakiet(adres(OBCY), adres(JA))) == InboundResult::UnknownSender, "jego pakiety odrzucone");
        EXPECT(r.onData(OBCY, pakiet(adres(ADAM), adres(JA))) == InboundResult::UnknownSender,
               "także podszywając się pod adres ADAMA");
    }

    SCENARIUSZ("Członek próbuje przejąć cudzy adres przy weryfikacji");
    {
        router::Router r(JA);
        r.setCode(KOD);
        r.syncMembers({JA, ADAM, EWA});
        // EWA zna kod (np. była kiedyś zaproszona), ale ogłasza adres ADAMA
        EXPECT(r.onHello(EWA, hello(KOD, EWA, JA, adres(ADAM))) == HelloResult::BadAddress, "cudzy adres odrzucony");
        EXPECT(r.onHello(EWA, hello(KOD, EWA, JA, adres(JA))) == HelloResult::BadAddress, "nasz adres odrzucony");
        EXPECT(r.onData(EWA, pakiet(adres(ADAM), adres(JA))) == InboundResult::UnknownSender, "nadal bez ruchu");
    }

    SCENARIUSZ("Zweryfikowany członek podszywa się pod innych (spoofing adresu źródłowego)");
    {
        router::Router r(JA);
        siec(r);
        r.syncMembers({JA, ADAM, EWA});
        r.onHello(EWA, hello(KOD, EWA, JA, adres(EWA))); // EWA też zweryfikowana
        EXPECT(r.onData(ADAM, pakiet(adres(EWA), adres(JA))) == InboundResult::Spoofed, "ADAM jako EWA – odrzucone");
        EXPECT(r.onData(ADAM, pakiet(adres(JA), adres(JA))) == InboundResult::Spoofed, "ADAM jako my – odrzucone");
        EXPECT(r.onData(ADAM, pakiet(ip::Address4(192, 168, 8, 1), adres(JA))) == InboundResult::Spoofed,
               "ADAM jako router domowy – odrzucone");
        EXPECT(r.onData(ADAM, pakiet(ip::Address4(127, 0, 0, 1), adres(JA))) == InboundResult::Spoofed,
               "ADAM jako localhost – odrzucone");
    }

    SCENARIUSZ("Członek próbuje dostać się przez nas dalej (sieć domowa, internet, inni)");
    {
        router::Router r(JA);
        siec(r);
        EXPECT(r.onData(ADAM, pakiet(adres(ADAM), ip::Address4(192, 168, 8, 153))) == InboundResult::NotForUs,
               "do NAS-a w sieci domowej – odrzucone");
        EXPECT(r.onData(ADAM, pakiet(adres(ADAM), ip::Address4(127, 0, 0, 1))) == InboundResult::NotForUs,
               "do localhost – odrzucone");
        EXPECT(r.onData(ADAM, pakiet(adres(ADAM), ip::Address4(8, 8, 8, 8))) == InboundResult::NotForUs,
               "do internetu – odrzucone");
        EXPECT(r.onData(ADAM, pakiet(adres(ADAM), adres(EWA))) == InboundResult::NotForUs,
               "do innego członka przez nas – odrzucone");
        EXPECT(r.onData(ADAM, pakiet(adres(ADAM), ip::Address4(255, 255, 255, 255))) == InboundResult::Deliver,
               "broadcast (wykrywanie serwerów LAN) – dozwolony");
    }

    SCENARIUSZ("Członek próbuje połączyć się z usługami naszego komputera");
    {
        router::Router r(JA);
        siec(r);
        const auto a = adres(ADAM), j = adres(JA);
        EXPECT(r.onData(ADAM, pakiet(a, j, 6, 22)) == InboundResult::Filtered, "SSH zablokowane");
        EXPECT(r.onData(ADAM, pakiet(a, j, 6, 445)) == InboundResult::Filtered, "Samba zablokowana");
        EXPECT(r.onData(ADAM, pakiet(a, j, 6, 3389)) == InboundResult::Filtered, "RDP zablokowany");
        EXPECT(r.onData(ADAM, pakiet(a, j, 6, 5432)) == InboundResult::Filtered, "PostgreSQL zablokowany");
        EXPECT(r.onData(ADAM, pakiet(a, ip::Address4(224, 0, 0, 251), 17, 5353)) == InboundResult::Filtered,
               "zapytania mDNS zablokowane");
        EXPECT(r.onData(ADAM, pakiet(a, j, 47, 0)) == InboundResult::Filtered, "inny protokół (GRE) zablokowany");
        EXPECT(r.onData(ADAM, pakiet(a, j, 17, 27960)) == InboundResult::Deliver, "Quake 3 dozwolony");
        EXPECT(r.onData(ADAM, pakiet(a, j, 6, 25565)) == InboundResult::Deliver, "Minecraft dozwolony");
        EXPECT(r.onData(ADAM, pakiet(a, j, 1, 0)) == InboundResult::Deliver, "ping dozwolony");
        r.setProtectServices(false);
        EXPECT(r.onData(ADAM, pakiet(a, j, 6, 22)) == InboundResult::Deliver, "po wyłączeniu ochrony SSH przechodzi");
    }

    SCENARIUSZ("Nasz system nie wysyła nic poza siecią ani do niezweryfikowanych");
    {
        router::Router r(JA);
        siec(r);
        const auto j = adres(JA);
        EXPECT(r.recipients(pakiet(j, adres(ADAM))) == std::vector<uint64_t>{ADAM}, "do ADAMA – tylko do niego");
        EXPECT(r.recipients(pakiet(j, adres(EWA))).empty(), "do niezweryfikowanej EWY – nic");
        EXPECT(r.recipients(pakiet(ip::Address4(192, 168, 8, 2), adres(ADAM))).empty(),
               "pakiet z naszym domowym adresem – nie wychodzi (bez wycieku)");
        EXPECT(r.recipients(pakiet(j, ip::Address4(255, 255, 255, 255))) == std::vector<uint64_t>{ADAM},
               "broadcast – tylko do zweryfikowanych");
        EXPECT(r.recipients(pakiet(j, ip::Address4(224, 0, 0, 251), 17, 5353)).empty(),
               "ogłoszenia mDNS (nazwa komputera) – nie wychodzą");
        EXPECT(r.recipients(pakiet(j, ip::Address4(100, 127, 255, 255), 17, 137)).empty(),
               "ogłoszenia NetBIOS – nie wychodzą");
        EXPECT(r.recipients(pakiet(j, ip::Address4(100, 127, 255, 255), 17, 27960)) == std::vector<uint64_t>{ADAM},
               "wykrywanie serwerów gry – wychodzi");
    }

    SCENARIUSZ("Osoba, która opuściła lobby, traci dostęp natychmiast");
    {
        router::Router r(JA);
        siec(r);
        const auto zniknely = r.syncMembers({JA, EWA});
        EXPECT(zniknely == std::vector<uint64_t>{ADAM}, "wykryto odejście ADAMA");
        EXPECT(r.onData(ADAM, pakiet(adres(ADAM), adres(JA))) == InboundResult::UnknownSender, "jego pakiety odrzucone");
        EXPECT(r.recipients(pakiet(adres(JA), adres(ADAM))).empty(), "nic do niego nie wysyłamy");
        r.syncMembers({JA, EWA, ADAM});
        EXPECT(r.onData(ADAM, pakiet(adres(ADAM), adres(JA))) == InboundResult::UnknownSender,
               "po powrocie musi zweryfikować się od nowa");
    }

    SCENARIUSZ("Po opuszczeniu sieci nic nie przechodzi");
    {
        router::Router r(JA);
        siec(r);
        r.clear();
        EXPECT(r.onData(ADAM, pakiet(adres(ADAM), adres(JA))) == InboundResult::UnknownSender, "brak przyjmowania");
        EXPECT(r.recipients(pakiet(adres(JA), ip::Address4(255, 255, 255, 255))).empty(), "brak wysyłania");
        EXPECT(r.onHello(ADAM, hello(KOD, ADAM, JA, adres(ADAM))) == HelloResult::NoNetwork, "brak weryfikacji");
    }

    SCENARIUSZ("Inna wersja programu nie zostaje wpuszczona");
    {
        router::Router r(JA);
        r.setCode(KOD);
        r.syncMembers({JA, ADAM});
        EXPECT(r.onHello(ADAM, hello(KOD, ADAM, JA, adres(ADAM), protocol::kVersion + 1)) == HelloResult::BadVersion,
               "inna wersja protokołu odrzucona");
    }

    SCENARIUSZ("Dwie osoby z tym samym adresem (kolizja) – żadna nie przejmuje ruchu drugiej");
    {
        // szukamy dwóch SteamID dających ten sam adres (paradoks dnia urodzin, ok. kilku tysięcy prób)
        std::map<uint32_t, uint64_t> seen;
        uint64_t a = 0, b = 0;
        for (uint64_t id = 76561198100000000ull; a == 0; ++id) {
            const auto [it, ok] = seen.emplace(adres(id).value(), id);
            if (!ok) { a = it->second; b = id; }
        }
        router::Router r(JA);
        r.setCode(KOD);
        r.syncMembers({JA, a, b});
        EXPECT(r.onHello(a, hello(KOD, a, JA, adres(a))) == HelloResult::Verified, "pierwsza osoba zweryfikowana");
        EXPECT(r.onHello(b, hello(KOD, b, JA, adres(b))) == HelloResult::Conflict, "druga – konflikt");
        EXPECT(r.onData(b, pakiet(adres(b), adres(JA))) == InboundResult::UnknownSender, "druga nie wysyła jako pierwsza");
        EXPECT(r.recipients(pakiet(adres(JA), adres(a))) == std::vector<uint64_t>{a}, "ruch trafia do pierwszej");
    }

    SCENARIUSZ("Uszkodzone i za duże pakiety");
    {
        router::Router r(JA);
        siec(r);
        std::vector<uint8_t> duzy(70000, 0);
        duzy[0] = 0x45;
        EXPECT(r.onData(ADAM, duzy) == InboundResult::Malformed, "ponad 64 KB odrzucony");
        EXPECT(r.onData(ADAM, std::vector<uint8_t>(10, 0x45)) == InboundResult::Malformed, "za krótki odrzucony");
        auto ipv6 = pakiet(adres(ADAM), adres(JA));
        ipv6[0] = 0x60;
        EXPECT(r.onData(ADAM, ipv6) == InboundResult::Malformed, "IPv6 odrzucony");
        auto klamstwo = pakiet(adres(ADAM), adres(JA));
        klamstwo[2] = 0xFF; // deklarowana długość większa niż pakiet
        EXPECT(r.onData(ADAM, klamstwo) == InboundResult::Malformed, "fałszywa długość odrzucona");
    }

    SCENARIUSZ("Ponawianie HELLO ma limit (brak zalewania sieci)");
    {
        router::Router r(JA);
        r.setCode(KOD);
        r.syncMembers({JA, EWA});
        auto t = router::Router::Clock::now();
        int wyslane = 0;
        for (int i = 0; i < 100; ++i, t += std::chrono::seconds(3))
            wyslane += int(r.dueHellos(t, std::chrono::seconds(2), 15).size());
        EXPECT(wyslane == 15, "maksymalnie 15 prób do osoby, która nie odpowiada");
    }

    std::printf("\n%d sprawdzeń, %d nieudanych\n", checks, failures);
    if (failures == 0) std::printf("Wszystkie testy bezpieczeństwa OK\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

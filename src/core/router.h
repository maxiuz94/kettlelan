// KettleLAN – decyzje bezpieczeństwa sieci: kto jest członkiem, komu wierzymy, które pakiety
// przyjmujemy i dokąd wysyłamy. Bez zależności od Steama, więc w pełni testowalne.
//
// Wątki: wszystkie metody są bezpieczne wielowątkowo (wątek Steama + wątek karty TUN).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <shared_mutex>
#include <span>
#include <vector>

#include "filter.h"
#include "ip.h"
#include "netcode.h"
#include "protocol.h"

namespace kettle::router {

enum class HelloResult {
    Verified,     // poprawny dowód – osoba właśnie została zweryfikowana
    Repeated,     // poprawny dowód od osoby już zweryfikowanej
    Conflict,     // poprawny dowód, ale adres koliduje z kimś innym – bez ruchu
    UnknownPeer,  // nie ma jej w lobby
    BadProof,     // nie zna klucza sieci
    BadVersion,   // inna wersja protokołu
    BadAddress,   // adres nie wynika z jej SteamID (próba przejęcia cudzego adresu)
    NoNetwork,    // nie jesteśmy w żadnej sieci
};

enum class InboundResult {
    Deliver,       // oddajemy systemowi
    Malformed,     // to nie jest poprawny pakiet IPv4
    NotForUs,      // adres docelowy nie jest nasz ani rozgłoszeniowy
    UnknownSender, // nadawca nie jest zweryfikowanym członkiem
    Spoofed,       // adres źródłowy nie należy do nadawcy
    Filtered,      // zablokowane przez ochronę usług (szczegóły w `filter`)
};

struct PeerState {
    uint64_t steamId = 0;
    ip::Address4 address;
    bool verified = false;
    bool conflict = false;
};

class Router {
public:
    using Clock = std::chrono::steady_clock;

    explicit Router(uint64_t selfSteamId);

    uint64_t selfId() const { return selfId_; }
    ip::Address4 selfAddress() const { return selfAddress_; }

    void setCode(std::optional<netcode::NetworkCode> code);
    std::optional<netcode::NetworkCode> code() const;
    void setProtectServices(bool on) { protect_ = on; }
    bool protectServices() const { return protect_; }

    // --- członkowie lobby (z listy Steama)
    // Ustawia aktualną listę członków; zwraca SteamID osób, które z niej zniknęły.
    std::vector<uint64_t> syncMembers(const std::vector<uint64_t>& members);
    bool isMember(uint64_t steamId) const;
    void clear();

    // --- weryfikacja
    HelloResult onHello(uint64_t from, const protocol::HelloMessage& msg);
    // Dowód, który wysyłamy osobie `to`. Pusty, gdy nie jesteśmy w sieci.
    std::optional<protocol::HelloMessage> makeHello(uint64_t to, bool ack) const;
    // Komu należy (ponownie) wysłać HELLO – osoby niezweryfikowane, z odstępem między próbami.
    std::vector<uint64_t> dueHellos(Clock::time_point now, std::chrono::milliseconds interval, int maxAttempts);

    // --- pakiety
    InboundResult onData(uint64_t from, std::span<const uint8_t> packet, filter::Result* filterResult = nullptr) const;
    // Do kogo wysłać pakiet z naszego systemu (pusto = nie wysyłamy).
    std::vector<uint64_t> recipients(std::span<const uint8_t> packet) const;

    std::vector<PeerState> peers() const;

private:
    struct Peer {
        ip::Address4 address;
        bool verified = false;
        bool conflict = false;
        int hellosSent = 0;
        Clock::time_point nextHello{};
    };

    const uint64_t selfId_;
    const ip::Address4 selfAddress_;
    std::atomic<bool> protect_{true};

    mutable std::shared_mutex mutex_;
    std::optional<netcode::NetworkCode> code_;
    std::map<uint64_t, Peer> peers_;
    std::map<uint32_t, uint64_t> routes_; // adres → SteamID (tylko zweryfikowani, bez konfliktów)
};

// Porównanie w stałym czasie.
bool equalConstantTime(uint64_t a, uint64_t b);

} // namespace kettle::router

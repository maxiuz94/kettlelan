// KettleLAN – ochrona usług komputera przed członkami sieci.
//
// Filtr działa na pakietach ODEBRANYCH od innych członków, zanim trafią do systemu.
// Blokuje połączenia do typowych usług (SSH, udostępnianie plików, zdalny pulpit, bazy danych…),
// które nie powinny być widoczne dla graczy. Ruch gier przechodzi bez zmian.
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace kettle::filter {

enum class Verdict {
    Allow,
    BlockedService,   // port chronionej usługi
    BlockedProtocol,  // protokół inny niż TCP/UDP/ICMP
    BlockedFragment,  // podejrzany fragment IP (np. próba ukrycia numeru portu)
    Malformed,
};

struct Result {
    Verdict verdict = Verdict::Allow;
    uint8_t protocol = 0;
    uint16_t dstPort = 0;
};

// Ocena pakietu IPv4 przychodzącego z sieci KettleLAN.
Result inspectInbound(std::span<const uint8_t> packet);

// Czy pakiet WYSYŁANY przez nasz system do wszystkich członków sieci (broadcast/multicast)
// jest ogłoszeniem usług systemowych, które zdradza nazwę komputera i udostępniane zasoby
// (mDNS/Avahi, NetBIOS, SSDP, KDE Connect, Spotify, Steam Remote Play…).
bool isDiscoveryBroadcast(std::span<const uint8_t> packet);

// Czy port jest na liście chronionych usług (TCP lub UDP).
bool isProtectedPort(uint16_t port);

// Nazwa usługi do dziennika, np. „SSH”, „udostępnianie plików (SMB)”.
std::string serviceName(uint16_t port);

} // namespace kettle::filter

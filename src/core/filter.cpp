#include "filter.h"

namespace kettle::filter {

namespace {

constexpr uint8_t kIcmp = 1;
constexpr uint8_t kTcp = 6;
constexpr uint8_t kUdp = 17;

struct Range {
    uint16_t from, to;
    const char* name;
};

// Usługi, do których członkowie sieci nie powinni mieć dostępu.
constexpr Range kProtected[] = {
    {22, 22, "SSH"},
    {23, 23, "Telnet"},
    {25, 25, "poczta (SMTP)"},
    {53, 53, "DNS"},
    {80, 80, "serwer WWW"},
    {111, 111, "RPC (NFS)"},
    {135, 135, "RPC Windows"},
    {137, 139, "udostępnianie plików (NetBIOS)"},
    {443, 443, "serwer WWW (HTTPS)"},
    {445, 445, "udostępnianie plików (SMB)"},
    {548, 548, "udostępnianie plików (AFP)"},
    {631, 631, "drukarki (CUPS)"},
    {1433, 1434, "baza danych (MS SQL)"},
    {1714, 1764, "KDE Connect"},
    {1900, 1900, "UPnP / SSDP"},
    {2049, 2049, "udostępnianie plików (NFS)"},
    {3306, 3306, "baza danych (MySQL/MariaDB)"},
    {3389, 3389, "zdalny pulpit (RDP)"},
    {5000, 5001, "panel NAS / UPnP"},
    {5353, 5353, "mDNS (nazwa komputera)"},
    {5355, 5355, "LLMNR (nazwa komputera)"},
    {5432, 5432, "baza danych (PostgreSQL)"},
    {5900, 5910, "zdalny pulpit (VNC)"},
    {6000, 6007, "serwer X11"},
    {6379, 6379, "baza danych (Redis)"},
    {8080, 8080, "serwer WWW"},
    {8096, 8096, "Jellyfin"},
    {8443, 8443, "serwer WWW (HTTPS)"},
    {9000, 9000, "panel administracyjny"},
    {9090, 9091, "panel administracyjny (Cockpit/Transmission)"},
    {9443, 9443, "panel administracyjny"},
    {10000, 10000, "Webmin"},
    {11211, 11211, "Memcached"},
    {27017, 27017, "baza danych (MongoDB)"},
    {27036, 27037, "Steam Remote Play"},
    {32400, 32400, "Plex"},
};

const Range* findRange(uint16_t port) {
    for (const auto& r : kProtected)
        if (port >= r.from && port <= r.to) return &r;
    return nullptr;
}

} // namespace

bool isDiscoveryBroadcast(std::span<const uint8_t> p) {
    if (p.size() < 28 || (p[0] >> 4) != 4 || p[9] != kUdp) return false;
    const size_t ihl = size_t(p[0] & 0x0F) * 4;
    if (ihl < 20 || p.size() < ihl + 4) return false;
    if ((((p[6] << 8) | p[7]) & 0x1FFF) != 0) return false; // dalszy fragment – bez portów
    const uint16_t dst = uint16_t((p[ihl + 2] << 8) | p[ihl + 3]);
    switch (dst) {
    case 137: case 138:  // NetBIOS
    case 1900:           // SSDP / UPnP
    case 3702:           // WS-Discovery
    case 5353:           // mDNS (Avahi, Bonjour)
    case 5355:           // LLMNR
    case 1716:           // KDE Connect
    case 17500:          // Dropbox LAN sync
    case 27036:          // Steam Remote Play
    case 57621:          // Spotify Connect
        return true;
    default:
        return false;
    }
}

bool isProtectedPort(uint16_t port) { return findRange(port) != nullptr; }

std::string serviceName(uint16_t port) {
    const Range* r = findRange(port);
    return r ? std::string(r->name) : std::string("port ") + std::to_string(port);
}

Result inspectInbound(std::span<const uint8_t> p) {
    Result res;
    if (p.size() < 20 || (p[0] >> 4) != 4) return {Verdict::Malformed, 0, 0};
    const size_t ihl = size_t(p[0] & 0x0F) * 4;
    const size_t total = (size_t(p[2]) << 8) | p[3];
    if (ihl < 20 || total < ihl || total > p.size()) return {Verdict::Malformed, 0, 0};

    res.protocol = p[9];
    const uint16_t fragField = uint16_t((p[6] << 8) | p[7]);
    const uint16_t fragOffset = fragField & 0x1FFF; // w jednostkach 8 bajtów

    if (res.protocol == kIcmp) return res; // ping i komunikaty ICMP – dozwolone

    if (res.protocol != kTcp && res.protocol != kUdp) {
        res.verdict = Verdict::BlockedProtocol;
        return res;
    }

    if (fragOffset != 0) {
        // Dalsze fragmenty nie mają nagłówka z portem. Fragment z przesunięciem 1 (8 bajtów)
        // to znana sztuczka nadpisania numeru portu (RFC 1858) – odrzucamy. Pozostałe przepuszczamy:
        // bez pierwszego fragmentu (sprawdzanego niżej) system i tak nie złoży pakietu.
        if (fragOffset == 1) res.verdict = Verdict::BlockedFragment;
        return res;
    }

    // Pierwszy (lub jedyny) fragment musi zawierać pełne numery portów.
    const auto l4 = p.subspan(ihl, total - ihl);
    const size_t minHeader = res.protocol == kTcp ? 20 : 8;
    const bool moreFragments = (fragField & 0x2000) != 0;
    if (l4.size() < (moreFragments ? minHeader : 4)) {
        res.verdict = moreFragments ? Verdict::BlockedFragment : Verdict::Malformed;
        return res;
    }

    res.dstPort = uint16_t((l4[2] << 8) | l4[3]);
    if (isProtectedPort(res.dstPort)) res.verdict = Verdict::BlockedService;
    return res;
}

} // namespace kettle::filter

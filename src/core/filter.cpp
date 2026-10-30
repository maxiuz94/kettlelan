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

// Services that network members should not be able to reach.
constexpr Range kProtected[] = {
    {22, 22, "SSH"},
    {23, 23, "Telnet"},
    {25, 25, "mail (SMTP)"},
    {53, 53, "DNS"},
    {80, 80, "web server"},
    {111, 111, "RPC (NFS)"},
    {123, 123, "time (W32Time / NTP)"},
    {135, 135, "Windows RPC"},
    {137, 139, "file sharing (NetBIOS)"},
    {161, 162, "network management (SNMP)"},
    {443, 443, "web server (HTTPS)"},
    {445, 445, "file sharing (SMB)"},
    {500, 500, "VPN (IPsec/IKE)"},
    {548, 548, "file sharing (AFP)"},
    {631, 631, "printing (CUPS)"},
    {1433, 1434, "database (MS SQL)"},
    {1714, 1764, "KDE Connect"},
    {1900, 1900, "UPnP / SSDP"},
    {2049, 2049, "file sharing (NFS)"},
    {2869, 2869, "UPnP (Windows)"},
    {3000, 3000, "web app (development server)"},
    {3306, 3306, "database (MySQL/MariaDB)"},
    {3389, 3389, "remote desktop (RDP)"},
    {3702, 3702, "network discovery (WS-Discovery)"},
    {4500, 4500, "VPN (IPsec/IKE)"},
    {5000, 5001, "NAS panel / UPnP"},
    {5040, 5040, "connected devices (CDPSvc)"},
    {5050, 5050, "connected devices (CDPSvc)"},
    {5353, 5353, "mDNS (computer name)"},
    {5355, 5355, "LLMNR (computer name)"},
    {5357, 5358, "network discovery (WSD)"},
    {5432, 5432, "database (PostgreSQL)"},
    {5555, 5555, "Android debugging (ADB)"},
    {5900, 5910, "remote desktop (VNC)"},
    {5985, 5986, "remote management (WinRM)"},
    {6000, 6007, "X11 server"},
    {6379, 6379, "database (Redis)"},
    {7680, 7680, "Windows Update sharing (Delivery Optimization)"},
    {8000, 8000, "web server"},
    {8080, 8081, "web server"},
    {8096, 8096, "Jellyfin"},
    {8123, 8123, "Home Assistant"},
    {8443, 8443, "web server (HTTPS)"},
    {8888, 8888, "web panel (Jupyter and others)"},
    {9000, 9000, "admin panel"},
    {9090, 9091, "admin panel (Cockpit/Transmission)"},
    {9100, 9100, "printing (JetDirect) / metrics"},
    {9200, 9200, "database (Elasticsearch)"},
    {9443, 9443, "admin panel"},
    {10000, 10000, "Webmin"},
    {11211, 11211, "Memcached"},
    {11434, 11434, "AI model server (Ollama)"},
    {22000, 22000, "file sync (Syncthing)"},
    {27017, 27017, "database (MongoDB)"},
    {27036, 27037, "Steam Remote Play"},
    {32400, 32400, "Plex"},
    {47001, 47001, "remote management (WinRM)"},
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
    if ((((p[6] << 8) | p[7]) & 0x1FFF) != 0) return false; // later fragment – no ports
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
    const uint16_t fragOffset = fragField & 0x1FFF; // in 8-byte units

    if (res.protocol == kIcmp) {
        // Ping (8/0), "unreachable" (3: closed port, packet size) and "time exceeded" (11: traceroute).
        // Others – redirects, timestamps, router messages – are not needed and are dropped.
        if (fragOffset != 0) return res; // a later fragment: the first one carried the type
        if (total < ihl + 1) return {Verdict::Malformed, kIcmp, 0};
        const uint8_t type = p[ihl];
        if (type != 0 && type != 3 && type != 8 && type != 11) res.verdict = Verdict::BlockedProtocol;
        return res;
    }

    if (res.protocol != kTcp && res.protocol != kUdp) {
        res.verdict = Verdict::BlockedProtocol;
        return res;
    }

    if (fragOffset != 0) {
        // Later fragments carry no port header. A fragment at offset 1 (8 bytes) is a known trick
        // to overwrite the port number (RFC 1858) – drop it. Others pass: without the first fragment
        // (checked below) the system cannot reassemble the packet anyway.
        if (fragOffset == 1) res.verdict = Verdict::BlockedFragment;
        return res;
    }

    // The first (or only) fragment must contain complete port numbers.
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

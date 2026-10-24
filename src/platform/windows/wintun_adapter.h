// KettleLAN – the Wintun adapter on Windows, used by the KettleLAN service (runs as SYSTEM).
#pragma once

#include <winsock2.h>
#include <windows.h>

#include <memory>
#include <span>
#include <string>

#include "ip.h"

namespace kettle::platform::win {

std::string toUtf8(const wchar_t* w);
std::string systemError(DWORD code);
std::wstring programFolder(); // folder of the running .exe, with a trailing backslash
// Whether ordinary programs (the user without elevation, Users, Everyone…) can change files in `dir`.
bool folderChangeableByNonAdmins(const std::wstring& dir);
// Removes KettleLAN's Windows Firewall rules (when the service is uninstalled).
void removeFirewallRules();

// The "KettleLAN" Wintun adapter. wintun.dll is loaded only from the program's folder and only after
// its WireGuard LLC signature has been checked on a locked file. The adapter exists as long as this
// object (the service) lives.
class WintunAdapter {
public:
    static std::unique_ptr<WintunAdapter> open(std::string& error);
    ~WintunAdapter();

    bool setAddress(ip::Address4 address, std::string& error); // our address only, and the MTU
    bool startSession(std::string& error);
    void endSession();
    HANDLE readEvent() const;
    // One packet from Windows (games) or nullptr; then GetLastError() tells why (ERROR_NO_MORE_ITEMS…).
    const uint8_t* receive(uint32_t& size);
    void release(const uint8_t* packet);
    void send(std::span<const uint8_t> packet); // a packet to Windows (dropped when the ring is full)

private:
    WintunAdapter() = default;
    void* adapter_ = nullptr;
    void* session_ = nullptr;
};

} // namespace kettle::platform::win

// KettleLAN – the Wintun adapter on Windows (https://www.wintun.net), used by the KettleLAN service.
//
// Wintun needs administrator rights both to create the adapter and to exchange packets, so on
// Windows the whole program runs elevated (the executables request it in their manifest).
// wintun.dll is loaded only from the program's own folder, never from the search path, and only
// after its digital signature (WireGuard LLC) has been checked on a file locked against changes.
#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <netfw.h>
#include <oleauto.h>
#include <aclapi.h>
#include <sddl.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>
#include <vector>

#include "log.h"
#include "tun.h" // kMtu
#include "wintun_adapter.h"
#include "wintun.h"

namespace kettle::platform::win {

namespace {

constexpr wchar_t kAdapterName[] = L"KettleLAN";
constexpr wchar_t kTunnelType[] = L"KettleLAN";
// Fixed adapter id, so Windows recognises the same adapter on every start.
constexpr GUID kAdapterGuid = {0x7e1a0c42, 0x5b6e, 0x4e0d, {0x9c, 0x8b, 0x4b, 0x3a, 0x2f, 0x1e, 0x6d, 0x90}};
constexpr DWORD kRingCapacity = 0x400000; // 4 MiB
constexpr wchar_t kFirewallRuleUdp[] = L"KettleLAN - incoming UDP from network members";
constexpr wchar_t kFirewallRulePing[] = L"KettleLAN - ping from network members";
constexpr wchar_t kLegacyFirewallRule[] = L"KettleLAN - incoming game connections"; // 0.2.0, all protocols
constexpr wchar_t kWintunSigner[] = L"WireGuard LLC";

} // namespace

std::string toUtf8(const wchar_t* w) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string systemError(DWORD code) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::string text = toUtf8(buf);
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.')) text.pop_back();
    return text + " (" + std::to_string(code) + ")";
}

std::wstring programFolder() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring dir(path, n);
    dir.erase(dir.find_last_of(L"\\/") + 1);
    return dir;
}

namespace {

// Checks the Authenticode signature of an open file and that it was signed by `signer`.
bool signedBy(HANDLE file, const std::wstring& path, const wchar_t* signer) {
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();
    fileInfo.hFile = file;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;            // no network access needed
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);

    bool ok = false;
    if (status == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* prov = WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR* sgnr = prov ? WTHelperGetProvSignerFromChain(prov, 0, FALSE, 0) : nullptr;
        CRYPT_PROVIDER_CERT* cert = sgnr ? WTHelperGetProvCertFromChain(sgnr, 0) : nullptr;
        if (cert && cert->pCert) {
            wchar_t name[256] = {};
            CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
            ok = std::wcscmp(name, signer) == 0;
            if (!ok) KLOG_WARN("wintun.dll is signed by \"" << toUtf8(name) << "\", not by WireGuard LLC");
        }
    } else {
        KLOG_WARN("wintun.dll has no valid digital signature (0x" << std::hex << static_cast<unsigned long>(status) << std::dec << ")");
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return ok;
}

struct WintunApi {
    HMODULE dll = nullptr;
    WINTUN_CREATE_ADAPTER_FUNC* CreateAdapter = nullptr;
    WINTUN_OPEN_ADAPTER_FUNC* OpenAdapter = nullptr;
    WINTUN_CLOSE_ADAPTER_FUNC* CloseAdapter = nullptr;
    WINTUN_GET_ADAPTER_LUID_FUNC* GetAdapterLUID = nullptr;
    WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC* GetRunningDriverVersion = nullptr;
    WINTUN_SET_LOGGER_FUNC* SetLogger = nullptr;
    WINTUN_START_SESSION_FUNC* StartSession = nullptr;
    WINTUN_END_SESSION_FUNC* EndSession = nullptr;
    WINTUN_GET_READ_WAIT_EVENT_FUNC* GetReadWaitEvent = nullptr;
    WINTUN_RECEIVE_PACKET_FUNC* ReceivePacket = nullptr;
    WINTUN_RELEASE_RECEIVE_PACKET_FUNC* ReleaseReceivePacket = nullptr;
    WINTUN_ALLOCATE_SEND_PACKET_FUNC* AllocateSendPacket = nullptr;
    WINTUN_SEND_PACKET_FUNC* SendPacket = nullptr;

    template <typename F> bool get(F*& fn, const char* name) {
        fn = reinterpret_cast<F*>(reinterpret_cast<void*>(GetProcAddress(dll, name)));
        return fn != nullptr;
    }

    // Returns false with `untrusted` set when the file exists but is not the signed original.
    bool load(bool& untrusted) {
        untrusted = false;
        // only from the program's folder (protection against DLL planting)
        const std::wstring path = programFolder() + L"wintun.dll";
        // Keep the file open without write/delete sharing, so it cannot be replaced between the
        // signature check and loading it.
        const HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE) return false;
        if (!signedBy(lock, path, kWintunSigner)) {
            CloseHandle(lock);
            untrusted = true;
            return false;
        }
        dll = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        CloseHandle(lock);
        if (!dll) return false;
        return get(CreateAdapter, "WintunCreateAdapter") && get(OpenAdapter, "WintunOpenAdapter") &&
               get(CloseAdapter, "WintunCloseAdapter") && get(GetAdapterLUID, "WintunGetAdapterLUID") &&
               get(GetRunningDriverVersion, "WintunGetRunningDriverVersion") && get(SetLogger, "WintunSetLogger") &&
               get(StartSession, "WintunStartSession") && get(EndSession, "WintunEndSession") &&
               get(GetReadWaitEvent, "WintunGetReadWaitEvent") && get(ReceivePacket, "WintunReceivePacket") &&
               get(ReleaseReceivePacket, "WintunReleaseReceivePacket") &&
               get(AllocateSendPacket, "WintunAllocateSendPacket") && get(SendPacket, "WintunSendPacket");
    }
};

WintunApi g_wintun;

// Set while looking for an existing adapter: not finding it is normal, although Wintun reports an error.
std::atomic<bool> g_lookingForAdapter{false};

void CALLBACK wintunLogger(WINTUN_LOGGER_LEVEL level, DWORD64 /*timestamp*/, LPCWSTR message) {
    const std::string text = toUtf8(message);
    if (level == WINTUN_LOG_ERR && !g_lookingForAdapter) KLOG_ERROR("Wintun: " << text);
    else if (level == WINTUN_LOG_WARN) KLOG_WARN("Wintun: " << text);
    else KLOG_DEBUG("Wintun: " << text);
}


// Sets our address (and only ours) and the MTU on the adapter.
bool configureAddress(const NET_LUID& luid, ip::Address4 address, std::string& error) {
    const ULONG wanted = htonl(address.value());

    PMIB_UNICASTIPADDRESS_TABLE table = nullptr;
    if (GetUnicastIpAddressTable(AF_INET, &table) == NO_ERROR) {
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            MIB_UNICASTIPADDRESS_ROW& row = table->Table[i];
            if (row.InterfaceLuid.Value == luid.Value && row.Address.Ipv4.sin_addr.s_addr != wanted)
                DeleteUnicastIpAddressEntry(&row); // e.g. an address from a different Steam account
        }
        FreeMibTable(table);
    }

    MIB_UNICASTIPADDRESS_ROW row;
    InitializeUnicastIpAddressEntry(&row);
    row.InterfaceLuid = luid;
    row.Address.Ipv4.sin_family = AF_INET;
    row.Address.Ipv4.sin_addr.s_addr = wanted;
    row.OnLinkPrefixLength = ip::kVirtualNet.prefix();
    row.DadState = IpDadStatePreferred;
    const DWORD r = CreateUnicastIpAddressEntry(&row);
    if (r != NO_ERROR && r != ERROR_OBJECT_ALREADY_EXISTS) {
        error = "Could not set the address: " + systemError(r);
        return false;
    }

    MIB_IPINTERFACE_ROW iface;
    InitializeIpInterfaceEntry(&iface);
    iface.Family = AF_INET;
    iface.InterfaceLuid = luid;
    if (GetIpInterfaceEntry(&iface) == NO_ERROR) {
        iface.NlMtu = kMtu;
        iface.SitePrefixLength = 0; // required by SetIpInterfaceEntry for IPv4
        const DWORD m = SetIpInterfaceEntry(&iface);
        if (m != NO_ERROR) KLOG_WARN("Could not set the MTU: " << systemError(m));
    }
    return true;
}

// Firewall rules for the KettleLAN adapter, only for KettleLAN addresses (100.64.0.0/10):
//  * UDP – most LAN games (Quake 3, Counter-Strike, Unreal Tournament…) work without any prompt,
//  * ping (ICMP echo request).
// TCP is not opened: a game server on TCP gets the usual Windows "Allow access?" prompt for that
// program. The network stays "Public" (no Windows file sharing), and KettleLAN's own filter blocks
// computer services as well.
bool addFirewallRule(INetFwRules* rules, const wchar_t* ruleName, const wchar_t* text, LONG protocol, const wchar_t* icmp) {
    BSTR name = SysAllocString(ruleName);
    INetFwRule* existing = nullptr;
    if (SUCCEEDED(rules->Item(name, &existing)) && existing) {
        existing->Release();
        SysFreeString(name);
        return true; // already there
    }
    INetFwRule* rule = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_NetFwRule, nullptr, CLSCTX_INPROC_SERVER, IID_INetFwRule,
                                   reinterpret_cast<void**>(&rule)))) {
        BSTR description = SysAllocString(text);
        BSTR remote = SysAllocString(L"100.64.0.0/255.192.0.0");
        rule->put_Name(name);
        rule->put_Description(description);
        rule->put_Direction(NET_FW_RULE_DIR_IN);
        rule->put_Action(NET_FW_ACTION_ALLOW);
        rule->put_Protocol(protocol);
        if (icmp) {
            BSTR types = SysAllocString(icmp);
            rule->put_IcmpTypesAndCodes(types);
            SysFreeString(types);
        }
        rule->put_RemoteAddresses(remote);
        rule->put_Profiles(NET_FW_PROFILE2_ALL);
        rule->put_Enabled(VARIANT_TRUE);

        SAFEARRAYBOUND bound{1, 0};
        SAFEARRAY* ifaces = SafeArrayCreate(VT_VARIANT, 1, &bound);
        VARIANT item;
        VariantInit(&item);
        item.vt = VT_BSTR;
        item.bstrVal = SysAllocString(kAdapterName);
        LONG index = 0;
        SafeArrayPutElement(ifaces, &index, &item);
        VariantClear(&item);
        VARIANT list;
        VariantInit(&list);
        list.vt = VT_ARRAY | VT_VARIANT;
        list.parray = ifaces;
        rule->put_Interfaces(list);
        VariantClear(&list);

        ok = SUCCEEDED(rules->Add(rule));
        SysFreeString(description);
        SysFreeString(remote);
        rule->Release();
    }
    SysFreeString(name);
    return ok;
}

void ensureFirewallRules() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INetFwPolicy2* policy = nullptr;
    INetFwRules* rules = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_NetFwPolicy2, nullptr, CLSCTX_INPROC_SERVER, IID_INetFwPolicy2,
                                   reinterpret_cast<void**>(&policy))) &&
        SUCCEEDED(policy->get_Rules(&rules))) {
        // KettleLAN 0.2.0 allowed all protocols with a single rule – remove it.
        BSTR legacy = SysAllocString(kLegacyFirewallRule);
        INetFwRule* old = nullptr;
        if (SUCCEEDED(rules->Item(legacy, &old)) && old) {
            old->Release();
            if (SUCCEEDED(rules->Remove(legacy))) KLOG_INFO("Removed the old KettleLAN firewall rule (all protocols)");
        }
        SysFreeString(legacy);

        const bool udp = addFirewallRule(rules, kFirewallRuleUdp,
            L"Lets games receive UDP traffic from KettleLAN network members (KettleLAN adapter, 100.64.0.0/10 only).",
            NET_FW_IP_PROTOCOL_UDP, nullptr);
        const bool ping = addFirewallRule(rules, kFirewallRulePing,
            L"Lets KettleLAN network members ping this computer (KettleLAN adapter, 100.64.0.0/10 only).",
            1 /* ICMPv4 */, L"8:*");
        if (!udp || !ping) KLOG_WARN("Could not add the Windows Firewall rules – Windows may ask to allow games");
    }
    if (rules) rules->Release();
    if (policy) policy->Release();
    if (SUCCEEDED(init)) CoUninitialize();
}

} // namespace

// KettleLAN runs elevated, so its folder must not be changeable by ordinary programs. Checks the folder's
// permissions: unsafe when it is owned by the current user, or when the current user (without
// elevation), Users, Authenticated Users, INTERACTIVE or Everyone may add, change or delete files in it.
// Works on any drive and with redirected folders (Desktop on another drive, OneDrive…).
bool folderChangeableByNonAdmins(const std::wstring& dir) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    if (GetNamedSecurityInfoW(dir.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                              &owner, nullptr, &dacl, nullptr, &sd) != ERROR_SUCCESS)
        return false; // cannot tell – no warning rather than a false one

    // the current user's own SID
    std::vector<BYTE> userBuf;
    PSID user = nullptr;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        userBuf.resize(size);
        if (size && GetTokenInformation(token, TokenUser, userBuf.data(), size, &size))
            user = reinterpret_cast<TOKEN_USER*>(userBuf.data())->User.Sid;
        CloseHandle(token);
    }
    // The service runs as SYSTEM – its own account is not an ordinary program. Only a person's account
    // (the "debug" mode, started by hand) counts.
    const bool asSystem = user && IsWellKnownSid(user, WinLocalSystemSid);
    if (asSystem) user = nullptr;

    // As SYSTEM we do not know which person might own the folder, so the owner must be an account only
    // administrators control: whoever owns a folder can always change its permissions.
    if (asSystem && owner) {
        BYTE trustedInstaller[SECURITY_MAX_SID_SIZE];
        PSID ti = nullptr;
        if (ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", &ti) && ti) {
            DWORD size = SECURITY_MAX_SID_SIZE;
            if (CopySid(size, trustedInstaller, ti)) {
                LocalFree(ti);
                ti = trustedInstaller;
            } else {
                LocalFree(ti);
                ti = nullptr;
            }
        }
        const bool trustedOwner = IsWellKnownSid(owner, WinLocalSystemSid) || IsWellKnownSid(owner, WinBuiltinAdministratorsSid) ||
                                  (ti && EqualSid(owner, ti));
        if (!trustedOwner) {
            LocalFree(sd);
            return true;
        }
    }

    // well-known groups every ordinary program runs with
    BYTE everyone[SECURITY_MAX_SID_SIZE], users[SECURITY_MAX_SID_SIZE], authUsers[SECURITY_MAX_SID_SIZE],
        interactive[SECURITY_MAX_SID_SIZE];
    PSID groups[4] = {everyone, users, authUsers, interactive};
    const WELL_KNOWN_SID_TYPE types[4] = {WinWorldSid, WinBuiltinUsersSid, WinAuthenticatedUserSid, WinInteractiveSid};
    for (int i = 0; i < 4; ++i) {
        DWORD size = SECURITY_MAX_SID_SIZE;
        if (!CreateWellKnownSid(types[i], nullptr, groups[i], &size)) groups[i] = nullptr;
    }
    auto isNonAdmin = [&](PSID sid) {
        if (user && EqualSid(sid, user)) return true;
        for (PSID g : groups)
            if (g && EqualSid(sid, g)) return true;
        return false;
    };

    constexpr ACCESS_MASK kChange = FILE_ADD_FILE | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_ADD_SUBDIRECTORY |
                                    FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
    bool unsafe = (user && owner && EqualSid(owner, user)) || dacl == nullptr; // owner can change permissions; no DACL = full access
    for (DWORD i = 0; !unsafe && dacl && i < dacl->AceCount; ++i) {
        void* ace = nullptr;
        if (!GetAce(dacl, i, &ace)) continue;
        const auto* header = static_cast<ACE_HEADER*>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || (header->AceFlags & INHERIT_ONLY_ACE)) continue;
        const auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
        if ((allowed->Mask & kChange) && isNonAdmin(const_cast<DWORD*>(&allowed->SidStart))) unsafe = true;
    }
    LocalFree(sd);
    return unsafe;
}


void removeFirewallRules() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INetFwPolicy2* policy = nullptr;
    INetFwRules* rules = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_NetFwPolicy2, nullptr, CLSCTX_INPROC_SERVER, IID_INetFwPolicy2,
                                   reinterpret_cast<void**>(&policy))) &&
        SUCCEEDED(policy->get_Rules(&rules))) {
        for (const wchar_t* name : {kFirewallRuleUdp, kFirewallRulePing, kLegacyFirewallRule}) {
            BSTR b = SysAllocString(name);
            rules->Remove(b);
            SysFreeString(b);
        }
    }
    if (rules) rules->Release();
    if (policy) policy->Release();
    if (SUCCEEDED(init)) CoUninitialize();
}

// ---------------------------------------------------------------- adapter

std::unique_ptr<WintunAdapter> WintunAdapter::open(std::string& error) {
    if (!g_wintun.dll) {
        bool untrusted = false;
        if (!g_wintun.load(untrusted)) {
            error = untrusted ? "wintun.dll is not the official file signed by WireGuard LLC"
                              : "wintun.dll was not found next to the KettleLAN service";
            return nullptr;
        }
    }
    g_wintun.SetLogger(wintunLogger);
    // The adapter is missing at the first start and after the service stopped – then it is created.
    g_lookingForAdapter = true;
    WINTUN_ADAPTER_HANDLE adapter = g_wintun.OpenAdapter(kAdapterName);
    g_lookingForAdapter = false;
    if (!adapter) {
        KLOG_INFO("Creating the KettleLAN adapter");
        adapter = g_wintun.CreateAdapter(kAdapterName, kTunnelType, &kAdapterGuid);
    }
    if (!adapter) {
        error = "Could not create the KettleLAN adapter (Wintun): " + systemError(GetLastError());
        return nullptr;
    }
    const DWORD version = g_wintun.GetRunningDriverVersion();
    KLOG_INFO("Wintun driver " << ((version >> 16) & 0xff) << "." << (version & 0xff));
    ensureFirewallRules();
    std::unique_ptr<WintunAdapter> a(new WintunAdapter());
    a->adapter_ = adapter;
    return a;
}

WintunAdapter::~WintunAdapter() {
    endSession();
    if (adapter_) g_wintun.CloseAdapter(static_cast<WINTUN_ADAPTER_HANDLE>(adapter_));
}

bool WintunAdapter::setAddress(ip::Address4 address, std::string& error) {
    NET_LUID luid;
    g_wintun.GetAdapterLUID(static_cast<WINTUN_ADAPTER_HANDLE>(adapter_), &luid);
    return configureAddress(luid, address, error);
}

bool WintunAdapter::startSession(std::string& error) {
    if (session_) return true;
    session_ = g_wintun.StartSession(static_cast<WINTUN_ADAPTER_HANDLE>(adapter_), kRingCapacity);
    if (!session_) {
        error = "Could not start the Wintun session: " + systemError(GetLastError());
        return false;
    }
    return true;
}

void WintunAdapter::endSession() {
    if (session_) g_wintun.EndSession(static_cast<WINTUN_SESSION_HANDLE>(session_));
    session_ = nullptr;
}

HANDLE WintunAdapter::readEvent() const { return g_wintun.GetReadWaitEvent(static_cast<WINTUN_SESSION_HANDLE>(session_)); }

const uint8_t* WintunAdapter::receive(uint32_t& size) {
    DWORD n = 0;
    const BYTE* p = g_wintun.ReceivePacket(static_cast<WINTUN_SESSION_HANDLE>(session_), &n);
    size = n;
    return p;
}

void WintunAdapter::release(const uint8_t* packet) {
    g_wintun.ReleaseReceivePacket(static_cast<WINTUN_SESSION_HANDLE>(session_), packet);
}

void WintunAdapter::send(std::span<const uint8_t> packet) {
    if (packet.empty() || packet.size() > WINTUN_MAX_IP_PACKET_SIZE) return;
    BYTE* out = g_wintun.AllocateSendPacket(static_cast<WINTUN_SESSION_HANDLE>(session_), DWORD(packet.size()));
    if (!out) return; // ring full – drop, like a busy network would
    std::memcpy(out, packet.data(), packet.size());
    g_wintun.SendPacket(static_cast<WINTUN_SESSION_HANDLE>(session_), out);
}

} // namespace kettle::platform::win

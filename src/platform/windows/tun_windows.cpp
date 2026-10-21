// KettleLAN – TUN adapter on Windows (Wintun, https://www.wintun.net).
//
// Wintun needs administrator rights both to create the adapter and to exchange packets, so on
// Windows the whole program runs elevated (the executables request it in their manifest).
// wintun.dll is loaded only from the program's own folder, never from the search path.
#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <netfw.h>
#include <oleauto.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

#include "log.h"
#include "tun.h"
#include "wintun.h"

namespace kettle::platform {

namespace {

constexpr wchar_t kAdapterName[] = L"KettleLAN";
constexpr wchar_t kTunnelType[] = L"KettleLAN";
// Fixed adapter id, so Windows recognises the same adapter on every start.
constexpr GUID kAdapterGuid = {0x7e1a0c42, 0x5b6e, 0x4e0d, {0x9c, 0x8b, 0x4b, 0x3a, 0x2f, 0x1e, 0x6d, 0x90}};
constexpr DWORD kRingCapacity = 0x400000; // 4 MiB
constexpr wchar_t kFirewallRuleName[] = L"KettleLAN - incoming game connections";

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

bool isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
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

    bool load() {
        // only from the program's folder (protection against DLL planting)
        wchar_t path[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return false;
        std::wstring dir(path, n);
        dir.erase(dir.find_last_of(L"\\/") + 1);
        dll = LoadLibraryExW((dir + L"wintun.dll").c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
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

void CALLBACK wintunLogger(WINTUN_LOGGER_LEVEL level, DWORD64 /*timestamp*/, LPCWSTR message) {
    const std::string text = toUtf8(message);
    if (level == WINTUN_LOG_ERR) KLOG_ERROR("Wintun: " << text);
    else if (level == WINTUN_LOG_WARN) KLOG_WARN("Wintun: " << text);
    else KLOG_DEBUG("Wintun: " << text);
}

class WindowsTun final : public Tun {
public:
    WindowsTun(WINTUN_ADAPTER_HANDLE adapter, WINTUN_SESSION_HANDLE session) : adapter_(adapter), session_(session) {}
    ~WindowsTun() override {
        stop();
        if (session_) g_wintun.EndSession(session_);
        if (adapter_) g_wintun.CloseAdapter(adapter_);
    }

    void start(PacketHandler onPacket) override {
        running_ = true;
        reader_ = std::thread([this, onPacket = std::move(onPacket)] {
            const HANDLE readable = g_wintun.GetReadWaitEvent(session_);
            while (running_) {
                DWORD size = 0;
                BYTE* packet = g_wintun.ReceivePacket(session_, &size);
                if (packet) {
                    onPacket(std::span<const uint8_t>(packet, size));
                    g_wintun.ReleaseReceivePacket(session_, packet);
                    continue;
                }
                const DWORD err = GetLastError();
                if (err == ERROR_NO_MORE_ITEMS) {
                    WaitForSingleObject(readable, 200);
                } else {
                    KLOG_ERROR("Reading from the KettleLAN adapter failed: " << systemError(err));
                    break;
                }
            }
        });
    }

    void stop() override {
        running_ = false;
        if (reader_.joinable()) reader_.join();
    }

    void write(std::span<const uint8_t> packet) override {
        if (packet.empty() || packet.size() > WINTUN_MAX_IP_PACKET_SIZE) return;
        BYTE* out = g_wintun.AllocateSendPacket(session_, DWORD(packet.size()));
        if (!out) return; // ring full – drop, like a busy network would
        std::memcpy(out, packet.data(), packet.size());
        g_wintun.SendPacket(session_, out);
    }

    std::string name() const override { return "KettleLAN"; }

private:
    WINTUN_ADAPTER_HANDLE adapter_;
    WINTUN_SESSION_HANDLE session_;
    std::atomic<bool> running_{false};
    std::thread reader_;
};

// Sets our address (and only ours) and the MTU on the adapter.
bool configureAddress(const NET_LUID& luid, ip::Address4 address, Message& error) {
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
        error = {Msg::TunAddressFailed, {systemError(r)}};
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

// Firewall rule: allow incoming traffic only on the KettleLAN adapter and only from KettleLAN
// addresses. The network stays "Public" (no Windows file sharing); computer services are also
// blocked by KettleLAN's own filter.
void ensureFirewallRule() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INetFwPolicy2* policy = nullptr;
    INetFwRules* rules = nullptr;
    INetFwRule* rule = nullptr;
    BSTR name = SysAllocString(kFirewallRuleName);
    do {
        if (FAILED(CoCreateInstance(CLSID_NetFwPolicy2, nullptr, CLSCTX_INPROC_SERVER, IID_INetFwPolicy2,
                                    reinterpret_cast<void**>(&policy))))
            break;
        if (FAILED(policy->get_Rules(&rules))) break;
        INetFwRule* existing = nullptr;
        if (SUCCEEDED(rules->Item(name, &existing)) && existing) {
            existing->Release();
            break; // already there
        }
        if (FAILED(CoCreateInstance(CLSID_NetFwRule, nullptr, CLSCTX_INPROC_SERVER, IID_INetFwRule,
                                    reinterpret_cast<void**>(&rule))))
            break;
        BSTR description = SysAllocString(
            L"Lets games receive connections from KettleLAN network members (KettleLAN adapter, 100.64.0.0/10 only).");
        BSTR remote = SysAllocString(L"100.64.0.0/255.192.0.0");
        rule->put_Name(name);
        rule->put_Description(description);
        rule->put_Direction(NET_FW_RULE_DIR_IN);
        rule->put_Action(NET_FW_ACTION_ALLOW);
        rule->put_Protocol(256); // NET_FW_IP_PROTOCOL_ANY (missing from some SDK headers)
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

        if (SUCCEEDED(rules->Add(rule))) KLOG_INFO("Added the Windows Firewall rule for the KettleLAN adapter");
        else KLOG_WARN("Could not add the Windows Firewall rule – Windows may ask to allow games");
        SysFreeString(description);
        SysFreeString(remote);
    } while (false);
    if (rule) rule->Release();
    if (rules) rules->Release();
    if (policy) policy->Release();
    SysFreeString(name);
    if (SUCCEEDED(init)) CoUninitialize();
}

} // namespace

std::unique_ptr<Tun> openTun(ip::Address4 address, Message& error) {
    if (!isElevated()) {
        error = {Msg::TunNeedsAdmin, {}};
        return nullptr;
    }
    if (!g_wintun.dll && !g_wintun.load()) {
        error = {Msg::TunWintunMissing, {}};
        return nullptr;
    }
    g_wintun.SetLogger(wintunLogger);

    WINTUN_ADAPTER_HANDLE adapter = g_wintun.OpenAdapter(kAdapterName);
    if (!adapter) adapter = g_wintun.CreateAdapter(kAdapterName, kTunnelType, &kAdapterGuid);
    if (!adapter) {
        error = {Msg::TunWintunFailed, {systemError(GetLastError())}};
        return nullptr;
    }
    const DWORD version = g_wintun.GetRunningDriverVersion();
    KLOG_INFO("Wintun driver " << ((version >> 16) & 0xff) << "." << (version & 0xff));

    NET_LUID luid;
    g_wintun.GetAdapterLUID(adapter, &luid);
    if (!configureAddress(luid, address, error)) {
        g_wintun.CloseAdapter(adapter);
        return nullptr;
    }
    ensureFirewallRule();

    WINTUN_SESSION_HANDLE session = g_wintun.StartSession(adapter, kRingCapacity);
    if (!session) {
        const DWORD err = GetLastError();
        g_wintun.CloseAdapter(adapter);
        if (err == ERROR_ALREADY_EXISTS || err == ERROR_SHARING_VIOLATION) error = {Msg::TunBusy, {}};
        else error = {Msg::TunWintunFailed, {systemError(err)}};
        return nullptr;
    }
    KLOG_INFO("Adapter KettleLAN ready, address " << address.toString());
    return std::make_unique<WindowsTun>(adapter, session);
}

} // namespace kettle::platform

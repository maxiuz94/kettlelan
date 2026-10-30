// KettleLAN – the Windows service. Runs as SYSTEM, owns the Wintun adapter and passes packets to and
// from the KettleLAN program (which runs as the user) over a local named pipe.
//
//   kettlelan-service install [path\to\KettleLAN.exe]   install and start the service (administrator)
//   kettlelan-service uninstall   stop and remove the service and its firewall rules (administrator)
//   kettlelan-service debug       run in this console instead of as a service (administrator)
//
// The service never talks to Steam or to other players: everything from the network reaches it only
// through the program, and every packet from the program is checked before it goes to Windows.
#include <winsock2.h>
#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cstdio>
#include <cwctype>
#include <iostream>
#include <memory>
#include <string>

#include "log.h"
#include "svcproto.h"
#include "wintun_adapter.h"

using namespace kettle;
using namespace kettle::platform::win;

namespace {

constexpr wchar_t kServiceName[] = L"KettleLAN";
// Where the program is installed – written by "install". HKLM: only administrators can change it.
constexpr wchar_t kRegKey[] = L"SOFTWARE\\KettleLAN";
constexpr wchar_t kRegProgramFolder[] = L"ProgramFolder";
constexpr wchar_t kDisplayName[] = L"KettleLAN Service";
constexpr wchar_t kDescription[] =
    L"Virtual network adapter for KettleLAN (LAN games over the Steam network). Started automatically.";
constexpr wchar_t kPipeNameW[] = LR"(\\.\pipe\KettleLAN)";
constexpr DWORD kHelloTimeoutMs = 5000;
// A program that does not take data for this long is frozen (the pipe buffer is full): disconnect it.
constexpr DWORD kWriteTimeoutMs = 5000;

SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status{};
HANDLE g_stopEvent = nullptr;
bool g_console = false; // "debug": started by hand in a console

void reportStatus(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0) {
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = exitCode;
    g_status.dwWaitHint = waitHint;
    g_status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    if (g_statusHandle) SetServiceStatus(g_statusHandle, &g_status);
}

// The log goes to service.log next to the service (Program Files\KettleLAN Service), a folder only
// administrators can change. Not to ProgramData: ordinary users may create folders there, and a folder
// someone else prepared (for example a link to another place) would let them choose where SYSTEM writes.
void openLogFile() {
    const std::wstring dir = programFolder();
    // the service does not start from such a folder anyway (runServer) – then there is nothing to log
    if (dir.empty() || folderChangeableByNonAdmins(dir)) return;
    _wfreopen((dir + L"service.log").c_str(), L"a", stderr);
}

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = wchar_t(std::towlower(c));
    return s;
}

// The program's folder, with a trailing backslash. The service lives in Program Files\KettleLAN Service,
// the program wherever the user installed it. Without the registry entry (a build run by hand) both
// share one folder.
std::wstring clientFolder() {
    wchar_t buf[MAX_PATH];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kRegKey, kRegProgramFolder, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return programFolder();
    std::wstring dir(buf);
    if (dir.empty()) return programFolder();
    if (dir.back() != L'\\') dir += L'\\';
    return dir;
}

// Only the Steam process of the KettleLAN window (kettlelan-steam.exe) or kettlelan-cli.exe from the
// program's folder may use the adapter. The window itself never connects to the service.
bool clientAllowed(HANDLE pipe, std::string& who) {
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid)) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok) return false;
    const std::wstring full(path, size);
    who = toUtf8(full.c_str());
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    const std::wstring folder = lower(full.substr(0, slash + 1));
    const std::wstring file = lower(full.substr(slash + 1));
    return folder == lower(clientFolder()) && (file == L"kettlelan-steam.exe" || file == L"kettlelan-cli.exe");
}

class Pipe {
public:
    explicit Pipe(HANDLE h) : h_(h) {
        readEv_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        writeEv_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    ~Pipe() {
        CancelIoEx(h_, nullptr);
        CloseHandle(readEv_);
        CloseHandle(writeEv_);
    }
    HANDLE readEvent() const { return readEv_; }

    // Starts an overlapped read; the result comes with finishRead() once readEvent() is set.
    bool startRead() {
        ZeroMemory(&readOv_, sizeof(readOv_));
        readOv_.hEvent = readEv_;
        ResetEvent(readEv_);
        if (ReadFile(h_, buf_, sizeof(buf_), nullptr, &readOv_)) return true; // done, event set
        return GetLastError() == ERROR_IO_PENDING;
    }
    // Returns the bytes read, or nothing when the pipe is closed or broken.
    std::optional<std::span<const uint8_t>> finishRead() {
        DWORD n = 0;
        if (!GetOverlappedResult(h_, &readOv_, &n, FALSE) || n == 0) return std::nullopt;
        return std::span<const uint8_t>(buf_, n);
    }
    // Waits at most kWriteTimeoutMs (or until the service stops): a frozen program cannot block the service.
    bool write(const std::vector<uint8_t>& data) {
        OVERLAPPED ov{};
        ov.hEvent = writeEv_;
        ResetEvent(writeEv_);
        DWORD n = 0;
        if (!WriteFile(h_, data.data(), DWORD(data.size()), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return false;
        HANDLE waits[2] = {writeEv_, g_stopEvent};
        const DWORD r = WaitForMultipleObjects(g_stopEvent ? 2 : 1, waits, FALSE, kWriteTimeoutMs);
        if (r != WAIT_OBJECT_0) {
            CancelIoEx(h_, &ov);
            GetOverlappedResult(h_, &ov, &n, TRUE);
            if (r == WAIT_TIMEOUT) KLOG_WARN("KettleLAN is not taking any data (not responding?) – disconnected it.");
            return false;
        }
        return GetOverlappedResult(h_, &ov, &n, FALSE) && n == data.size();
    }

private:
    HANDLE h_;
    HANDLE readEv_, writeEv_;
    OVERLAPPED readOv_{};
    uint8_t buf_[65536 + 3];
};

// One connected program: verify it, set its address, then pass packets both ways.
void serveClient(HANDLE handle, WintunAdapter& adapter) {
    Pipe pipe(handle);
    std::string who;
    if (!clientAllowed(handle, who)) {
        KLOG_WARN("Refused a connection from " << (who.empty() ? std::string("an unknown program") : who));
        pipe.write(svc::encodeError(svc::ErrorCode::NotAllowed, "Only KettleLAN from its installation folder may use it."));
        return;
    }
    // The program's folder may be on any drive. If ordinary programs can put files there, any of them
    // could pretend to be KettleLAN – the installer sets safe permissions, so this means a damaged install.
    if (folderChangeableByNonAdmins(clientFolder())) {
        KLOG_WARN("Refused " << who << ": its folder can be changed by ordinary programs");
        pipe.write(svc::encodeError(svc::ErrorCode::NotAllowed,
                                    "KettleLAN's folder can be changed by other programs. Reinstall KettleLAN to fix its permissions."));
        return;
    }

    // --- HELLO (within a few seconds)
    svc::FrameReader reader;
    std::optional<svc::Hello> hello;
    if (!pipe.startRead()) return;
    while (!hello) {
        HANDLE waits[2] = {pipe.readEvent(), g_stopEvent};
        if (WaitForMultipleObjects(2, waits, FALSE, kHelloTimeoutMs) != WAIT_OBJECT_0) return;
        const auto data = pipe.finishRead();
        if (!data || !reader.feed(*data)) return;
        if (auto frame = reader.next()) {
            if (frame->type != svc::Type::Hello || !(hello = svc::decodeHello(frame->payload))) {
                pipe.write(svc::encodeError(svc::ErrorCode::BadHello, "Unexpected message."));
                return;
            }
            break;
        }
        if (!pipe.startRead()) return;
    }
    if (hello->version != svc::kVersion) {
        pipe.write(svc::encodeError(svc::ErrorCode::WrongVersion, "The KettleLAN service has another version."));
        return;
    }
    if (!ip::kVirtualNet.contains(hello->address)) {
        pipe.write(svc::encodeError(svc::ErrorCode::BadHello, "The address is not a KettleLAN address."));
        return;
    }
    std::string error;
    if (!adapter.setAddress(hello->address, error) || !adapter.startSession(error)) {
        KLOG_ERROR(error);
        pipe.write(svc::encodeError(svc::ErrorCode::AdapterFailed, error));
        return;
    }
    const ip::Address4 self = hello->address;
    KLOG_INFO("KettleLAN connected (" << who << "), address " << self.toString());
    if (!pipe.write(svc::encode(svc::Type::Ready, {}))) {
        adapter.endSession();
        return;
    }

    // --- packets
    uint64_t dropped = 0;
    bool alive = pipe.startRead();
    while (alive) {
        HANDLE waits[3] = {pipe.readEvent(), adapter.readEvent(), g_stopEvent};
        const DWORD r = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
        if (r == WAIT_OBJECT_0) {
            const auto data = pipe.finishRead();
            if (!data || !reader.feed(*data)) break;
            while (auto frame = reader.next()) {
                if (frame->type != svc::Type::Packet) { alive = false; break; } // protocol violation
                if (svc::acceptFromProgram(frame->payload, self)) adapter.send(frame->payload);
                else ++dropped;
            }
            if (alive) alive = pipe.startRead();
        } else if (r == WAIT_OBJECT_0 + 1) {
            uint32_t size = 0;
            while (const uint8_t* packet = adapter.receive(size)) {
                const bool ok = pipe.write(svc::encode(svc::Type::Packet, std::span<const uint8_t>(packet, size)));
                adapter.release(packet);
                if (!ok) { alive = false; break; }
            }
        } else {
            break; // stopping
        }
    }
    adapter.endSession();
    KLOG_INFO("KettleLAN disconnected" << (dropped ? " (" + std::to_string(dropped) + " invalid packets dropped)" : std::string()));
}

void runServer() {
    // The service runs as SYSTEM: from a folder ordinary programs can change, anybody could replace it.
    if (folderChangeableByNonAdmins(programFolder())) {
        if (!g_console) {
            KLOG_ERROR("The service's folder can be changed by ordinary programs – not starting. Reinstall KettleLAN.");
            return;
        }
        KLOG_WARN("The service's folder can be changed by ordinary programs (allowed in debug mode only).");
    }
    std::string error;
    auto adapter = WintunAdapter::open(error);
    if (!adapter) {
        KLOG_ERROR(error);
        return;
    }

    // SYSTEM and administrators: full access; the logged-on (interactive) user: read/write.
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, FALSE};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)", SDDL_REVISION_1,
                                                              &sa.lpSecurityDescriptor, nullptr)) {
        KLOG_ERROR("Could not create the pipe permissions: " << systemError(GetLastError()));
        return;
    }
    HANDLE connectEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    KLOG_INFO("KettleLAN service ready");
    while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateNamedPipeW(kPipeNameW, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                       1 << 20, 1 << 20, 0, &sa);
        if (pipe == INVALID_HANDLE_VALUE) {
            KLOG_ERROR("Could not create the pipe (is another program using its name?): " << systemError(GetLastError()));
            if (WaitForSingleObject(g_stopEvent, 5000) == WAIT_OBJECT_0) break;
            continue;
        }
        OVERLAPPED ov{};
        ov.hEvent = connectEv;
        ResetEvent(connectEv);
        bool connected = ConnectNamedPipe(pipe, &ov) != 0 || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected && GetLastError() == ERROR_IO_PENDING) {
            HANDLE waits[2] = {connectEv, g_stopEvent};
            DWORD n = 0;
            connected = WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 && GetOverlappedResult(pipe, &ov, &n, FALSE);
        }
        if (connected) serveClient(pipe, *adapter);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    CloseHandle(connectEv);
    LocalFree(sa.lpSecurityDescriptor);
    KLOG_INFO("KettleLAN service stopped");
}

DWORD WINAPI controlHandler(DWORD control, DWORD, void*, void*) {
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        reportStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        SetEvent(g_stopEvent);
    }
    return NO_ERROR;
}

void WINAPI serviceMain(DWORD, LPWSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, controlHandler, nullptr);
    if (!g_statusHandle) return;
    reportStatus(SERVICE_START_PENDING, NO_ERROR, 10000);
    reportStatus(SERVICE_RUNNING);
    runServer();
    reportStatus(SERVICE_STOPPED);
}

// ---------------------------------------------------------------- install / uninstall

void runQuiet(const std::wstring& commandLine) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = commandLine;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

int install(const wchar_t* programExe) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        std::cerr << "Run this as administrator: " << systemError(GetLastError()) << "\n";
        return 1;
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring command = L"\"" + std::wstring(exe) + L"\"";
    // Where the program is: the service accepts kettlelan-steam.exe / kettlelan-cli.exe only from there.
    if (programExe && *programExe) {
        wchar_t full[MAX_PATH];
        const DWORD n = GetFullPathNameW(programExe, MAX_PATH, full, nullptr);
        std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(full, n) : std::wstring();
        const size_t slash = dir.find_last_of(L"\\/");
        dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
        HKEY key = nullptr;
        LSTATUS st = dir.empty() ? ERROR_INVALID_PARAMETER
                                 : RegCreateKeyExW(HKEY_LOCAL_MACHINE, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (st == ERROR_SUCCESS) {
            st = RegSetValueExW(key, kRegProgramFolder, 0, REG_SZ, reinterpret_cast<const BYTE*>(dir.c_str()),
                                DWORD((dir.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(key);
        }
        if (st != ERROR_SUCCESS) {
            std::cerr << "Could not save the program's folder: " << systemError(DWORD(st)) << "\n";
            CloseServiceHandle(scm);
            return 1;
        }
    }
    SC_HANDLE svc = CreateServiceW(scm, kServiceName, kDisplayName, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                   SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command.c_str(), nullptr, nullptr, nullptr,
                                   nullptr, nullptr);
    if (!svc && GetLastError() == ERROR_SERVICE_EXISTS) {
        svc = OpenServiceW(scm, kServiceName, SERVICE_ALL_ACCESS);
        if (svc)
            ChangeServiceConfigW(svc, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command.c_str(),
                                 nullptr, nullptr, nullptr, nullptr, nullptr, kDisplayName);
    }
    if (!svc) {
        std::cerr << "Could not install the service: " << systemError(GetLastError()) << "\n";
        CloseServiceHandle(scm);
        return 1;
    }
    SERVICE_DESCRIPTIONW desc{const_cast<wchar_t*>(kDescription)};
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);
    SC_ACTION actions[3] = {{SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 30000}};
    SERVICE_FAILURE_ACTIONSW failure{86400, nullptr, nullptr, 3, actions};
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);
    // KettleLAN 0.2 started itself through Task Scheduler as administrator – not needed any more.
    runQuiet(L"schtasks.exe /Delete /F /TN KettleLAN");
    const bool started = StartServiceW(svc, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    std::cout << (started ? "KettleLAN Service installed and started.\n" : "KettleLAN Service installed, but it did not start.\n");
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return started ? 0 : 1;
}

int uninstall() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        std::cerr << "Run this as administrator: " << systemError(GetLastError()) << "\n";
        return 1;
    }
    SC_HANDLE svc = OpenServiceW(scm, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (svc) {
        SERVICE_STATUS status{};
        if (ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
            for (int i = 0; i < 50 && QueryServiceStatus(svc, &status) && status.dwCurrentState != SERVICE_STOPPED; ++i) Sleep(200);
        }
        DeleteService(svc);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    removeFirewallRules();
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, kRegKey);
    std::cout << "KettleLAN Service removed.\n";
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring command = argc > 1 ? lower(argv[1]) : L"";
    if (command == L"install") return install(argc > 2 ? argv[2] : nullptr);
    if (command == L"uninstall") return uninstall();
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (command == L"debug") {
        g_console = true;
        std::cout << "KettleLAN service in this console – Ctrl+C to stop.\n";
        SetConsoleCtrlHandler([](DWORD) -> BOOL { SetEvent(g_stopEvent); return TRUE; }, TRUE);
        runServer();
        return 0;
    }
    openLogFile();
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<wchar_t*>(kServiceName), serviceMain}, {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table)) {
        std::cerr << "kettlelan-service: start it as a Windows service, or use \"install\", \"uninstall\" or \"debug\".\n";
        return 1;
    }
    return 0;
}

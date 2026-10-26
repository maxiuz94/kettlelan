// KettleLAN – network adapter on Windows: a client of the KettleLAN service.
//
// The service (kettlelan-service.exe, SYSTEM) owns the Wintun adapter; this program runs as the user
// and exchanges packets with it over the local named pipe \\.\pipe\KettleLAN.
#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "log.h"
#include "svcproto.h"
#include "tun.h"

namespace kettle::platform {

namespace {

constexpr wchar_t kPipeNameW[] = LR"(\\.\pipe\KettleLAN)";
constexpr DWORD kReplyTimeoutMs = 15000;

std::string systemError(DWORD code) {
    char* buf = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<char*>(&buf), 0, nullptr);
    std::string text = buf ? buf : "";
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.')) text.pop_back();
    return text + " (" + std::to_string(code) + ")";
}

// Overlapped I/O on the pipe: a read on one thread and writes on another must not block each other.
class PipeIo {
public:
    explicit PipeIo(HANDLE h) : h_(h), readEv_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
                                writeEv_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~PipeIo() {
        CancelIoEx(h_, nullptr);
        CloseHandle(h_);
        CloseHandle(readEv_);
        CloseHandle(writeEv_);
    }
    HANDLE handle() const { return h_; }

    // Waits at most 5 seconds: a service that takes no data is frozen. Then the connection is given up
    // (the pending read ends too), so KettleLAN does not hang on it.
    bool write(const std::vector<uint8_t>& data) {
        std::lock_guard lk(writeMutex_);
        if (broken_) return false;
        OVERLAPPED ov{};
        ov.hEvent = writeEv_;
        ResetEvent(writeEv_);
        DWORD n = 0;
        if (!WriteFile(h_, data.data(), DWORD(data.size()), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return false;
        if (WaitForSingleObject(writeEv_, kWriteTimeoutMs) != WAIT_OBJECT_0) {
            broken_ = true;
            KLOG_ERROR("The KettleLAN service is not taking any data (not responding?)");
            CancelIoEx(h_, nullptr);
            GetOverlappedResult(h_, &ov, &n, TRUE);
            return false;
        }
        return GetOverlappedResult(h_, &ov, &n, FALSE) && n == data.size();
    }

    // Reads some bytes; nothing on error, timeout or when `stop` is set.
    std::optional<std::span<const uint8_t>> read(DWORD timeoutMs, HANDLE stop = nullptr) {
        OVERLAPPED ov{};
        ov.hEvent = readEv_;
        ResetEvent(readEv_);
        if (!ReadFile(h_, buf_, sizeof(buf_), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return std::nullopt;
        HANDLE waits[2] = {readEv_, stop};
        const DWORD r = WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, timeoutMs);
        DWORD n = 0;
        if (r != WAIT_OBJECT_0) {
            CancelIoEx(h_, &ov);
            GetOverlappedResult(h_, &ov, &n, TRUE);
            return std::nullopt;
        }
        if (!GetOverlappedResult(h_, &ov, &n, FALSE) || n == 0) return std::nullopt;
        return std::span<const uint8_t>(buf_, n);
    }

private:
    HANDLE h_;
    HANDLE readEv_, writeEv_;
    std::mutex writeMutex_;
    static constexpr DWORD kWriteTimeoutMs = 5000;
    std::atomic<bool> broken_{false};
    uint8_t buf_[65536 + 3];
};

class ServiceTun final : public Tun {
public:
    ServiceTun(std::unique_ptr<PipeIo> io, svc::FrameReader reader)
        : io_(std::move(io)), reader_(std::move(reader)), stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ServiceTun() override {
        stop();
        CloseHandle(stop_);
    }

    void start(PacketHandler onPacket) override {
        reader_thread_ = std::thread([this, onPacket = std::move(onPacket)] {
            // packets that arrived together with the service's reply
            while (auto f = reader_.next())
                if (f->type == svc::Type::Packet) onPacket(f->payload);
            while (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
                const auto data = io_->read(INFINITE, stop_);
                if (!data) {
                    if (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0)
                        KLOG_ERROR("The connection to the KettleLAN service was lost");
                    break;
                }
                if (!reader_.feed(*data)) {
                    KLOG_ERROR("The KettleLAN service sent broken data");
                    break;
                }
                while (auto f = reader_.next())
                    if (f->type == svc::Type::Packet) onPacket(f->payload);
            }
        });
    }

    void stop() override {
        SetEvent(stop_);
        if (reader_thread_.joinable()) reader_thread_.join();
    }

    void write(std::span<const uint8_t> packet) override {
        if (packet.empty() || packet.size() > svc::kMaxPayload) return;
        io_->write(svc::encode(svc::Type::Packet, packet));
    }

    std::string name() const override { return "KettleLAN"; }

private:
    std::unique_ptr<PipeIo> io_;
    svc::FrameReader reader_;
    HANDLE stop_;
    std::thread reader_thread_;
};

} // namespace

std::unique_ptr<Tun> openTun(ip::Address4 address, Message& error) {
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 3 && h == INVALID_HANDLE_VALUE; ++attempt) {
        // SECURITY_IDENTIFICATION: the service may check who we are, but cannot act as us
        h = CreateFileW(kPipeNameW, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeW(kPipeNameW, 2000);
        else if (h == INVALID_HANDLE_VALUE) break;
    }
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) error = {Msg::TunBusy, {}};
        else error = {Msg::TunServiceUnavailable, {}};
        KLOG_WARN("Could not connect to the KettleLAN service: " << systemError(err));
        return nullptr;
    }
    // Windows services run in session 0, which ordinary programs cannot use: a program that created
    // a pipe with the same name to impersonate the service is caught here.
    ULONG session = 1;
    if (!GetNamedPipeServerSessionId(h, &session) || session != 0) {
        CloseHandle(h);
        error = {Msg::TunServiceUntrusted, {}};
        return nullptr;
    }
    auto io = std::make_unique<PipeIo>(h);
    if (!io->write(svc::encodeHello(address))) {
        error = {Msg::TunServiceUnavailable, {}};
        return nullptr;
    }
    svc::FrameReader reader;
    for (;;) {
        const auto data = io->read(kReplyTimeoutMs);
        if (!data || !reader.feed(*data)) {
            error = {Msg::TunServiceUnavailable, {}};
            return nullptr;
        }
        const auto frame = reader.next();
        if (!frame) continue;
        if (frame->type == svc::Type::Ready) break;
        const auto info = frame->type == svc::Type::Error ? svc::decodeError(frame->payload) : std::nullopt;
        if (!info) error = {Msg::TunServiceUnavailable, {}};
        else if (info->code == svc::ErrorCode::Busy) error = {Msg::TunBusy, {}};
        else if (info->code == svc::ErrorCode::WrongVersion) error = {Msg::TunServiceVersion, {}};
        else if (info->code == svc::ErrorCode::AdapterFailed) error = {Msg::TunWintunFailed, {info->text}};
        else error = {Msg::TunServiceRejected, {info->text}};
        return nullptr;
    }
    KLOG_INFO("Adapter KettleLAN ready (KettleLAN service), address " << address.toString());
    return std::make_unique<ServiceTun>(std::move(io), std::move(reader));
}

// The program runs as the user now; the service checks its own folder and logs it.
std::optional<Message> startupWarning() { return std::nullopt; }

} // namespace kettle::platform

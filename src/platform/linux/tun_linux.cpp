// KettleLAN – TUN adapter on Linux.
//
// The program runs as a normal user (otherwise it cannot talk to Steam). The `kettle0` adapter is
// created once by the `kettlelan-helper` program started through pkexec (system password prompt).
// The adapter is persistent and owned by the user, so later it is opened without any privileges.
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "log.h"
#include "tun.h"

namespace kettle::platform {

namespace fs = std::filesystem;

static constexpr const char* kIfName = "kettle0";

namespace {

class LinuxTun final : public Tun {
public:
    explicit LinuxTun(int fd) : fd_(fd) {}
    ~LinuxTun() override {
        stop();
        if (fd_ >= 0) ::close(fd_);
    }

    void start(PacketHandler onPacket) override {
        running_ = true;
        reader_ = std::thread([this, onPacket = std::move(onPacket)] {
            std::vector<uint8_t> buf(65536);
            pollfd pfd{fd_, POLLIN, 0};
            while (running_) {
                const int r = ::poll(&pfd, 1, 200);
                if (r <= 0) continue;
                const ssize_t n = ::read(fd_, buf.data(), buf.size());
                if (n > 0) onPacket(std::span<const uint8_t>(buf.data(), size_t(n)));
                else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    KLOG_ERROR("Reading from " << kIfName << " failed: " << std::strerror(errno));
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
        const ssize_t n = ::write(fd_, packet.data(), packet.size());
        if (n < 0) KLOG_DEBUG("Writing to " << kIfName << " failed: " << std::strerror(errno));
    }

    std::string name() const override { return kIfName; }

private:
    int fd_;
    std::atomic<bool> running_{false};
    std::thread reader_;
};

// Whether the adapter exists, belongs to us and has the right address – then no password is needed.
bool interfaceReady(ip::Address4 address) {
    const fs::path sys = fs::path("/sys/class/net") / kIfName;
    if (!fs::exists(sys)) return false;

    std::ifstream ownerFile(sys / "owner");
    long owner = -1;
    if (!(ownerFile >> owner) || owner != long(::getuid())) return false;

    const int s = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (s < 0) return false;
    ifreq ifr{};
    std::strncpy(ifr.ifr_name, kIfName, IFNAMSIZ - 1);
    bool ok = false;
    if (::ioctl(s, SIOCGIFADDR, &ifr) == 0) {
        const auto* sin = reinterpret_cast<sockaddr_in*>(&ifr.ifr_addr);
        ok = ntohl(sin->sin_addr.s_addr) == address.value();
    }
    if (ok && ::ioctl(s, SIOCGIFMTU, &ifr) == 0) ok = ifr.ifr_mtu == kMtu;
    if (ok && ::ioctl(s, SIOCGIFFLAGS, &ifr) == 0) ok = (ifr.ifr_flags & IFF_UP) != 0;
    ::close(s);
    return ok;
}

fs::path findHelper() {
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    std::vector<fs::path> candidates;
    if (!ec) candidates.push_back(self.parent_path() / "kettlelan-helper");
    candidates.push_back("/usr/lib/kettlelan/kettlelan-helper");
    candidates.push_back("/usr/libexec/kettlelan/kettlelan-helper");
    for (const auto& c : candidates)
        if (fs::exists(c)) return c;
    return {};
}

// An AppImage runs from a FUSE mount that root cannot access by default.
// In that case the helper is copied to a fresh private folder (0700) just for the time it runs.
struct HelperCopy {
    fs::path dir;
    ~HelperCopy() {
        std::error_code ec;
        if (!dir.empty()) fs::remove_all(dir, ec);
    }
};

fs::path prepareHelper(const fs::path& helper, HelperCopy& copy, Message& error) {
    if (std::getenv("APPIMAGE") == nullptr) return helper;
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    std::string tmpl = std::string(runtime && *runtime ? runtime : "/tmp") + "/kettlelan-helper-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (!::mkdtemp(buf.data())) {
        error = {Msg::TunTempDirFailed, {std::strerror(errno)}};
        return {};
    }
    copy.dir = buf.data();
    const fs::path target = copy.dir / "kettlelan-helper";
    std::error_code ec;
    fs::copy_file(helper, target, ec);
    if (!ec) fs::permissions(target, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) {
        error = {Msg::TunHelperCopyFailed, {ec.message()}};
        return {};
    }
    return target;
}

bool runHelper(ip::Address4 address, Message& error) {
    const fs::path found = findHelper();
    if (found.empty()) {
        error = {Msg::TunHelperMissing, {}};
        return false;
    }
    HelperCopy copy;
    const fs::path helper = prepareHelper(found, copy, error);
    if (helper.empty()) return false;
    const std::string uid = std::to_string(::getuid());
    const std::string addr = address.toString();
    KLOG_INFO("Creating adapter " << kIfName << " (" << addr << "/10) – the system will ask for the administrator password.");

    // Runs "<tool> <helper> up <uid> <address>" and returns its exit code (kExecFailed = tool missing).
    constexpr int kExecFailed = 120;
    auto run = [&](const char* tool) {
        const pid_t pid = ::fork();
        if (pid == 0) {
            const char* argv[] = {tool, helper.c_str(), "up", uid.c_str(), addr.c_str(), nullptr};
            ::execvp(tool, const_cast<char* const*>(argv));
            ::_exit(kExecFailed);
        }
        if (pid < 0) return -1;
        int status = 0;
        ::waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    };

    int code = run("pkexec");
    // pkexec: 126 = password prompt cancelled, 127 = not authorized / authentication failed
    // (e.g. over SSH, where polkit sees no desktop session). In a terminal we then try sudo.
    if ((code == 127 || code == kExecFailed) && ::isatty(STDIN_FILENO)) {
        KLOG_INFO("The system password prompt is not available – trying sudo.");
        const int sudoCode = run("sudo");
        if (sudoCode != kExecFailed) code = sudoCode;
    }
    if (code == 0) return true;

    // In an AppImage the helper copy disappears after this function, so a manual command makes no sense there.
    const bool appImage = std::getenv("APPIMAGE") != nullptr;
    const std::string manual = "sudo " + helper.string() + " up " + uid + " " + addr;
    if (code == kExecFailed)
        error = {Msg::TunPkexecMissing, {manual}};
    else if (code == 126)
        error = {Msg::TunCancelled, {}};
    else if (code == 127)
        error = appImage ? Message{Msg::TunAuthFailedAppImage, {}} : Message{Msg::TunAuthFailed, {manual}};
    else
        error = {Msg::TunHelperFailed, {std::to_string(code)}};
    return false;
}

} // namespace

std::unique_ptr<Tun> openTun(ip::Address4 address, Message& error) {
    if (!interfaceReady(address)) {
        if (!runHelper(address, error)) return nullptr;
        if (!interfaceReady(address)) {
            error = {Msg::TunNotReady, {}};
            return nullptr;
        }
    }

    const int fd = ::open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error = {Msg::TunOpenFailed, {std::strerror(errno)}};
        return nullptr;
    }
    ifreq ifr{};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, kIfName, IFNAMSIZ - 1);
    if (::ioctl(fd, TUNSETIFF, &ifr) < 0) {
        if (errno == EBUSY)
            error = {Msg::TunBusy, {}};
        else
            error = {Msg::TunAttachFailed, {std::strerror(errno)}};
        ::close(fd);
        return nullptr;
    }
    KLOG_INFO("Adapter " << kIfName << " ready, address " << address.toString());
    return std::make_unique<LinuxTun>(fd);
}

std::optional<Message> startupWarning() { return std::nullopt; }

} // namespace kettle::platform

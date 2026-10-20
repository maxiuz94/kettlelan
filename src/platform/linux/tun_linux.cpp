// KettleLAN – karta TUN na Linuksie.
//
// Program działa jako zwykły użytkownik (inaczej nie połączy się ze Steamem). Kartę `kettle0`
// tworzy raz pomocnik `kettlelan-helper` uruchamiany przez pkexec (okno hasła systemu).
// Karta jest „trwała” i należy do użytkownika, więc później otwieramy ją bez uprawnień.
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
                    KLOG_ERROR("Odczyt z " << kIfName << " nie powiódł się: " << std::strerror(errno));
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
        if (n < 0) KLOG_DEBUG("Zapis do " << kIfName << ": " << std::strerror(errno));
    }

    std::string name() const override { return kIfName; }

private:
    int fd_;
    std::atomic<bool> running_{false};
    std::thread reader_;
};

// Czy karta istnieje, należy do nas i ma właściwy adres – wtedy nie trzeba prosić o hasło.
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

bool runHelper(ip::Address4 address, std::string& error) {
    const fs::path helper = findHelper();
    if (helper.empty()) {
        error = "Nie znaleziono pomocnika kettlelan-helper (powinien leżeć obok programu).";
        return false;
    }
    const std::string uid = std::to_string(::getuid());
    const std::string addr = address.toString();
    KLOG_INFO("Tworzę kartę " << kIfName << " (" << addr << "/10) – system poprosi o hasło administratora.");

    const pid_t pid = ::fork();
    if (pid == 0) {
        const char* argv[] = {"pkexec", helper.c_str(), "up", uid.c_str(), addr.c_str(), nullptr};
        ::execvp("pkexec", const_cast<char* const*>(argv));
        ::_exit(127);
    }
    if (pid < 0) {
        error = std::string("fork(): ") + std::strerror(errno);
        return false;
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (code == 127) error = "Brak programu pkexec (pakiet polkit). Zainstaluj go albo uruchom pomocnika ręcznie przez sudo.";
        else if (code == 126) error = "Anulowano okno hasła – bez tego nie da się utworzyć karty sieciowej.";
        else error = "Pomocnik kettlelan-helper zakończył się błędem (kod " + std::to_string(code) + ").";
        return false;
    }
    return true;
}

} // namespace

std::unique_ptr<Tun> openTun(ip::Address4 address, std::string& error) {
    if (!interfaceReady(address)) {
        if (!runHelper(address, error)) return nullptr;
        if (!interfaceReady(address)) {
            error = "Karta kettle0 nadal nie jest gotowa po uruchomieniu pomocnika.";
            return nullptr;
        }
    }

    const int fd = ::open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error = std::string("Nie można otworzyć /dev/net/tun: ") + std::strerror(errno);
        return nullptr;
    }
    ifreq ifr{};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, kIfName, IFNAMSIZ - 1);
    if (::ioctl(fd, TUNSETIFF, &ifr) < 0) {
        error = std::string("Nie można podłączyć się do karty kettle0: ") + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }
    KLOG_INFO("Karta " << kIfName << " gotowa, adres " << address.toString());
    return std::make_unique<LinuxTun>(fd);
}

} // namespace kettle::platform

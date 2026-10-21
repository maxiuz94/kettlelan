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

// AppImage działa z katalogu zamontowanego przez FUSE, do którego root domyślnie nie ma dostępu.
// Kopiujemy wtedy pomocnika do świeżego, prywatnego katalogu (0700) tylko na czas uruchomienia.
struct HelperCopy {
    fs::path dir;
    ~HelperCopy() {
        std::error_code ec;
        if (!dir.empty()) fs::remove_all(dir, ec);
    }
};

fs::path prepareHelper(const fs::path& helper, HelperCopy& copy, std::string& error) {
    if (std::getenv("APPIMAGE") == nullptr) return helper;
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    std::string tmpl = std::string(runtime && *runtime ? runtime : "/tmp") + "/kettlelan-helper-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (!::mkdtemp(buf.data())) {
        error = std::string("Nie można utworzyć katalogu tymczasowego: ") + std::strerror(errno);
        return {};
    }
    copy.dir = buf.data();
    const fs::path target = copy.dir / "kettlelan-helper";
    std::error_code ec;
    fs::copy_file(helper, target, ec);
    if (!ec) fs::permissions(target, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) {
        error = "Nie można przygotować pomocnika: " + ec.message();
        return {};
    }
    return target;
}

bool runHelper(ip::Address4 address, std::string& error) {
    const fs::path found = findHelper();
    if (found.empty()) {
        error = "Nie znaleziono pomocnika kettlelan-helper (powinien leżeć obok programu).";
        return false;
    }
    HelperCopy copy;
    const fs::path helper = prepareHelper(found, copy, error);
    if (helper.empty()) return false;
    const std::string uid = std::to_string(::getuid());
    const std::string addr = address.toString();
    KLOG_INFO("Tworzę kartę " << kIfName << " (" << addr << "/10) – system poprosi o hasło administratora.");

    // Uruchamia „<narzędzie> <pomocnik> up <uid> <adres>” i zwraca kod wyjścia (kExecFailed = brak narzędzia).
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
    // pkexec: 126 = anulowano okno hasła, 127 = brak zgody / nie udało się uwierzytelnić
    // (np. przez SSH, gdzie polkit nie widzi sesji pulpitu). W terminalu próbujemy wtedy sudo.
    if ((code == 127 || code == kExecFailed) && ::isatty(STDIN_FILENO)) {
        KLOG_INFO("Okno hasła systemu nie jest dostępne – próbuję sudo.");
        const int sudoCode = run("sudo");
        if (sudoCode != kExecFailed) code = sudoCode;
    }
    if (code == 0) return true;

    // W AppImage kopia pomocnika znika po wyjściu z funkcji – tam ręczne polecenie nie ma sensu.
    const bool appImage = std::getenv("APPIMAGE") != nullptr;
    const std::string manual = appImage ? std::string("(uruchom KettleLAN z menu pulpitu)")
                                        : "sudo " + helper.string() + " up " + uid + " " + addr;
    if (code == kExecFailed)
        error = "Brak programu pkexec (pakiet polkit). Zainstaluj go albo utwórz kartę ręcznie:\n  " + manual;
    else if (code == 126)
        error = "Anulowano okno hasła – bez tego nie da się utworzyć karty sieciowej.";
    else if (code == 127)
        error = "Nie udało się potwierdzić uprawnień administratora (np. w sesji SSH bez pulpitu). "
                "Uruchom KettleLAN z pulpitu albo utwórz kartę ręcznie:\n  " + manual;
    else
        error = "Pomocnik kettlelan-helper zakończył się błędem (kod " + std::to_string(code) + ").";
    return false;
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
        if (errno == EBUSY)
            error = "KettleLAN jest już uruchomiony w innym oknie lub terminalu – zamknij go i spróbuj ponownie.";
        else
            error = std::string("Nie można podłączyć się do karty kettle0: ") + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }
    KLOG_INFO("Karta " << kIfName << " gotowa, adres " << address.toString());
    return std::make_unique<LinuxTun>(fd);
}

} // namespace kettle::platform

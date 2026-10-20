// KettleLAN – wirtualna karta sieciowa (TUN), wspólny interfejs dla Linuksa i Windowsa.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

#include "ip.h"

namespace kettle::platform {

inline constexpr int kMtu = 1200;

class Tun {
public:
    using PacketHandler = std::function<void(std::span<const uint8_t>)>;

    virtual ~Tun() = default;
    // Uruchamia wątek czytający pakiety wysyłane przez system/gry do sieci KettleLAN.
    virtual void start(PacketHandler onPacket) = 0;
    virtual void stop() = 0;
    // Oddaje systemowi pakiet odebrany od innego członka sieci.
    virtual void write(std::span<const uint8_t> packet) = 0;
    virtual std::string name() const = 0;
};

// Przygotowuje kartę (w razie potrzeby prosi o uprawnienia administratora) i ją otwiera.
// Przy błędzie zwraca nullptr i opis w `error`.
std::unique_ptr<Tun> openTun(ip::Address4 address, std::string& error);

} // namespace kettle::platform

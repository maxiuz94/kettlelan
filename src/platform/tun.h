// KettleLAN – virtual network adapter (TUN), common interface for Linux and Windows.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "ip.h"
#include "messages.h"

namespace kettle::platform {

inline constexpr int kMtu = 1200;

class Tun {
public:
    using PacketHandler = std::function<void(std::span<const uint8_t>)>;

    virtual ~Tun() = default;
    // Starts a thread that reads packets sent by the system/games to the KettleLAN network.
    virtual void start(PacketHandler onPacket) = 0;
    virtual void stop() = 0;
    // Hands a packet received from another member over to the system.
    virtual void write(std::span<const uint8_t> packet) = 0;
    virtual std::string name() const = 0;
};

// Prepares the adapter (asking for administrator rights if needed) and opens it.
// On failure returns nullptr and fills `error`.
std::unique_ptr<Tun> openTun(ip::Address4 address, Message& error);

// A warning to show after start (e.g. an unsafe install folder), or nothing.
std::optional<Message> startupWarning();

} // namespace kettle::platform

// KettleLAN – connection to the Steam client (Steamworks API).
#pragma once

#include <cstdint>
#include <string>

#include "messages.h"

namespace kettle::steam {

// Valve's test App ID (Spacewar) – free and available to every Steam account.
// To use your own App ID, change it here.
inline constexpr uint32_t kAppId = 480;

class Session {
public:
    // Connects to the running Steam client. On failure returns false and fills `error`.
    bool init(Message& error);
    void shutdown();
    // Must be called regularly on the Steam thread (handles callbacks).
    void runCallbacks();

    uint64_t steamId() const { return steamId_; }
    std::string personaName() const;
    bool initialized() const { return initialized_; }

private:
    bool initialized_ = false;
    uint64_t steamId_ = 0;
};

} // namespace kettle::steam

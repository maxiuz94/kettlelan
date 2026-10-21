#include "session.h"

#include <cstdlib>
#include <string>

#include <steam_api.h>

#include "log.h"
#include "text.h"

namespace kettle::steam {

static void setAppIdEnv() {
    // Instead of a steam_appid.txt file – works no matter which folder the program was started from.
    const std::string id = std::to_string(kAppId);
#ifdef _WIN32
    _putenv_s("SteamAppId", id.c_str());
    _putenv_s("SteamGameId", id.c_str());
#else
    setenv("SteamAppId", id.c_str(), 1);
    setenv("SteamGameId", id.c_str(), 1);
#endif
}

bool Session::init(Message& error) {
    setAppIdEnv();
    SteamErrMsg msg{};
    const ESteamAPIInitResult r = SteamAPI_InitEx(&msg);
    if (r != k_ESteamAPIInitResult_OK) {
        switch (r) {
        case k_ESteamAPIInitResult_NoSteamClient:
            error = {Msg::SteamNotRunning, {}};
            break;
        case k_ESteamAPIInitResult_VersionMismatch:
            error = {Msg::SteamOutdated, {}};
            break;
        default:
            error = {Msg::SteamInitFailed, {std::string(msg)}};
            break;
        }
        return false;
    }
    initialized_ = true;
    steamId_ = SteamUser()->GetSteamID().ConvertToUint64();
    // Prepare Valve's relays in advance – faster first connection.
    SteamNetworkingUtils()->InitRelayNetworkAccess();
    KLOG_INFO("Connected to Steam as " << personaName());
    return true;
}

void Session::shutdown() {
    if (!initialized_) return;
    SteamAPI_Shutdown();
    initialized_ = false;
}

void Session::runCallbacks() { SteamAPI_RunCallbacks(); }

std::string Session::personaName() const {
    return initialized_ ? text::sanitize(SteamFriends()->GetPersonaName(), 32) : std::string();
}

} // namespace kettle::steam

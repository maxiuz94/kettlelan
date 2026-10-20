#include "session.h"

#include <cstdlib>
#include <string>

#include <steam_api.h>

#include "log.h"
#include "text.h"

namespace kettle::steam {

static void setAppIdEnv() {
    // Zamiast pliku steam_appid.txt – działa niezależnie od katalogu, z którego uruchomiono program.
    const std::string id = std::to_string(kAppId);
#ifdef _WIN32
    _putenv_s("SteamAppId", id.c_str());
    _putenv_s("SteamGameId", id.c_str());
#else
    setenv("SteamAppId", id.c_str(), 1);
    setenv("SteamGameId", id.c_str(), 1);
#endif
}

bool Session::init(std::string& error) {
    setAppIdEnv();
    SteamErrMsg msg{};
    const ESteamAPIInitResult r = SteamAPI_InitEx(&msg);
    if (r != k_ESteamAPIInitResult_OK) {
        switch (r) {
        case k_ESteamAPIInitResult_NoSteamClient:
            error = "Steam nie jest uruchomiony. Włącz Steama, zaloguj się i spróbuj ponownie.";
            break;
        case k_ESteamAPIInitResult_VersionMismatch:
            error = "Klient Steam jest za stary – zaktualizuj Steama.";
            break;
        default:
            error = std::string("Nie udało się połączyć ze Steamem: ") + msg;
            break;
        }
        return false;
    }
    initialized_ = true;
    steamId_ = SteamUser()->GetSteamID().ConvertToUint64();
    // Przygotowanie relayów Valve z wyprzedzeniem – szybsze pierwsze połączenie.
    SteamNetworkingUtils()->InitRelayNetworkAccess();
    KLOG_INFO("Połączono ze Steam jako " << personaName());
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

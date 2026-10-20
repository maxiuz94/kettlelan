// KettleLAN – połączenie z klientem Steam (Steamworks API).
#pragma once

#include <cstdint>
#include <string>

namespace kettle::steam {

// Testowe App ID Valve (Spacewar) – darmowe, dostępne dla każdego konta Steam.
// Własne App ID wystarczy wpisać tutaj (oraz w CMakeLists.txt).
inline constexpr uint32_t kAppId = 480;

class Session {
public:
    // Łączy się z działającym klientem Steam. Przy błędzie zwraca false i opis w `error`.
    bool init(std::string& error);
    void shutdown();
    // Musi być wywoływane regularnie w wątku Steama (obsługa zdarzeń).
    void runCallbacks();

    uint64_t steamId() const { return steamId_; }
    std::string personaName() const;
    bool initialized() const { return initialized_; }

private:
    bool initialized_ = false;
    uint64_t steamId_ = 0;
};

} // namespace kettle::steam

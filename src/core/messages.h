// KettleLAN – user-facing messages.
//
// Every message shown to the user has an id and arguments. The CLI and the log use the English
// templates below; the GUI translates the same templates with Qt (they are marked with
// QT_TRANSLATE_NOOP, so lupdate extracts them into translations/*.ts).
#pragma once

#include <string>
#include <vector>

namespace kettle {

enum class Msg {
    // network
    AlreadyInNetwork,
    Connecting,        // %1 = network code
    Merging,
    OpenFailed,        // %1 = Steam error code
    JoinFailed,        // %1 = Steam response code
    WrongLobby,
    Connected,         // %1 = network name, %2 = our address, %3 = network code
    Left,
    PeerJoined,        // %1 = name, %2 = address
    PeerLeft,          // %1 = name
    PeerConflict,      // %1 = name
    PeerBadKey,        // %1 = name
    PeerBadVersion,    // %1 = name
    InvalidCode,
    // Steam
    SteamNotRunning,
    SteamOutdated,
    SteamInitFailed,   // %1 = details from Steam
    // virtual network adapter
    TunTempDirFailed,  // %1 = system error
    TunHelperCopyFailed, // %1 = system error
    TunHelperMissing,
    TunPkexecMissing,  // %1 = manual command
    TunCancelled,
    TunAuthFailed,     // %1 = manual command
    TunAuthFailedAppImage,
    TunHelperFailed,   // %1 = exit code
    TunNotReady,
    TunOpenFailed,     // %1 = system error
    TunBusy,
    TunAttachFailed,   // %1 = system error
};

struct Message {
    Msg id = Msg::AlreadyInNetwork;
    std::vector<std::string> args;
};

// English template with %1, %2… placeholders (the source text for translations).
const char* messageTemplate(Msg id);

// Message in English with the arguments filled in.
std::string english(const Message& message);

// Whether the message is about another person joining or leaving (shown as a notification).
bool isPeerEvent(Msg id);

} // namespace kettle

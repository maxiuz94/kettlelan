// KettleLAN – user-facing messages.
//
// Every message shown to the user has an id and arguments, so the core does not build user-facing
// sentences itself. The templates are marked with QT_TRANSLATE_NOOP, so they could be translated
// with Qt's tools if that is ever needed.
#pragma once

#include <string>
#include <vector>

namespace kettle {

enum class Msg {
    // network
    AlreadyInThisNetwork, // %1 = network code
    NetworkLimit,      // %1 = maximum number of networks
    Connecting,        // %1 = network code
    Merging,
    OpenFailed,        // %1 = Steam error code
    JoinFailed,        // %1 = Steam response code
    WrongLobby,
    Connected,         // %1 = network name, %2 = our address, %3 = network code
    Left,
    PeerJoined,        // %1 = name, %2 = address, %3 = network name
    PeerLeft,          // %1 = name, %2 = network name
    PeerConflict,      // %1 = name
    PeerBadKey,        // %1 = name
    PeerBadVersion,    // %1 = name
    InvalidCode,
    OldVersionInNetwork, // %1 = network name
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
    // Windows
    TunNeedsAdmin,
    TunWintunMissing,
    TunWintunFailed,   // %1 = system error
    TunAddressFailed,  // %1 = system error
    TunWintunUntrusted,
    UnsafeFolder,      // %1 = the program's folder
    TunServiceUnavailable,
    TunServiceUntrusted,
    TunServiceRejected, // %1 = reason from the service
    TunServiceVersion,
};

struct Message {
    Msg id = Msg::Left;
    std::vector<std::string> args;
};

// English template with %1, %2… placeholders.
const char* messageTemplate(Msg id);

// Message in English with the arguments filled in.
std::string english(const Message& message);

// Whether the message is about another person joining or leaving (shown as a notification).
bool isPeerEvent(Msg id);
// Worth a system notification when the window is hidden: people joining and leaving, and warnings.
bool isNotifyEvent(Msg id);
// The state of a network, already shown on the list (no message needed in the window).
bool isNetworkStateEvent(Msg id);

} // namespace kettle

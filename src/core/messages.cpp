#include "messages.h"

// Marks a string for translation without depending on Qt (lupdate recognises this macro).
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(context, text) text
#endif

namespace kettle {

const char* messageTemplate(Msg id) {
    switch (id) {
    case Msg::AlreadyInThisNetwork: return QT_TRANSLATE_NOOP("Messages", "You are already in the network %1.");
    case Msg::NetworkLimit: return QT_TRANSLATE_NOOP("Messages", "You can be in at most %1 networks at the same time – leave one first.");
    case Msg::Connecting: return QT_TRANSLATE_NOOP("Messages", "Connecting to network %1…");
    case Msg::Merging: return QT_TRANSLATE_NOOP("Messages", "Joining the other people in this network…");
    case Msg::OpenFailed: return QT_TRANSLATE_NOOP("Messages", "Could not open the network (Steam error %1).");
    case Msg::JoinFailed: return QT_TRANSLATE_NOOP("Messages", "Could not connect to the network (Steam code %1). Please try again.");
    case Msg::WrongLobby: return QT_TRANSLATE_NOOP("Messages", "This lobby does not belong to this KettleLAN network.");
    case Msg::Connected: return QT_TRANSLATE_NOOP("Messages", "Connected to “%1”. Your address: %2, network code: %3");
    case Msg::Left: return QT_TRANSLATE_NOOP("Messages", "You left the network.");
    case Msg::PeerJoined: return QT_TRANSLATE_NOOP("Messages", "%1 joined “%3” – address %2");
    case Msg::PeerLeft: return QT_TRANSLATE_NOOP("Messages", "%1 left “%2”.");
    case Msg::PeerConflict: return QT_TRANSLATE_NOOP("Messages", "%1 has the same address as someone else – traffic to them will not work.");
    case Msg::PeerBadKey: return QT_TRANSLATE_NOOP("Messages", "%1 is in the lobby but does not know the network key – ignoring.");
    case Msg::PeerBadVersion: return QT_TRANSLATE_NOOP("Messages", "%1 uses a different version of KettleLAN – please update.");
    case Msg::OldVersionInNetwork: return QT_TRANSLATE_NOOP("Messages", "Someone in “%1” uses an older KettleLAN (0.2) – older versions cannot see this network, ask them to update.");
    case Msg::InvalidCode: return QT_TRANSLATE_NOOP("Messages", "That does not look like a network code. Codes look like KTL-XXXX-XXXX-XXXX.");
    case Msg::SteamNotRunning: return QT_TRANSLATE_NOOP("Messages", "Steam is not running. Start Steam and sign in.");
    case Msg::SteamOutdated: return QT_TRANSLATE_NOOP("Messages", "Your Steam client is too old – please update Steam.");
    case Msg::SteamInitFailed: return QT_TRANSLATE_NOOP("Messages", "Could not connect to Steam: %1");
    case Msg::TunTempDirFailed: return QT_TRANSLATE_NOOP("Messages", "Could not create a temporary folder: %1");
    case Msg::TunHelperCopyFailed: return QT_TRANSLATE_NOOP("Messages", "Could not prepare the helper: %1");
    case Msg::TunHelperMissing: return QT_TRANSLATE_NOOP("Messages", "The kettlelan-helper program was not found (it should be next to KettleLAN).");
    case Msg::TunPkexecMissing: return QT_TRANSLATE_NOOP("Messages", "pkexec (polkit) is not installed. Install it or create the adapter manually:\n  %1");
    case Msg::TunCancelled: return QT_TRANSLATE_NOOP("Messages", "The password prompt was cancelled – the virtual network adapter cannot be created without it.");
    case Msg::TunAuthFailed: return QT_TRANSLATE_NOOP("Messages", "Administrator authentication failed (for example in an SSH session without a desktop). Start KettleLAN from your desktop or create the adapter manually:\n  %1");
    case Msg::TunAuthFailedAppImage: return QT_TRANSLATE_NOOP("Messages", "Administrator authentication failed (for example in an SSH session without a desktop). Start KettleLAN from your desktop menu.");
    case Msg::TunHelperFailed: return QT_TRANSLATE_NOOP("Messages", "kettlelan-helper failed (exit code %1).");
    case Msg::TunNotReady: return QT_TRANSLATE_NOOP("Messages", "The kettle0 adapter is still not ready after running the helper.");
    case Msg::TunOpenFailed: return QT_TRANSLATE_NOOP("Messages", "Could not open /dev/net/tun: %1");
    case Msg::TunBusy: return QT_TRANSLATE_NOOP("Messages", "KettleLAN is already running in another window or terminal – close it and try again.");
    case Msg::TunAttachFailed: return QT_TRANSLATE_NOOP("Messages", "Could not attach to the kettle0 adapter: %1");
    case Msg::TunNeedsAdmin: return QT_TRANSLATE_NOOP("Messages", "On Windows KettleLAN needs administrator rights to create the virtual network adapter. Start it with “Run as administrator”.");
    case Msg::TunWintunMissing: return QT_TRANSLATE_NOOP("Messages", "wintun.dll was not found next to KettleLAN. Extract the whole KettleLAN folder from the zip file again.");
    case Msg::TunWintunFailed: return QT_TRANSLATE_NOOP("Messages", "Could not create the KettleLAN network adapter (Wintun): %1");
    case Msg::TunAddressFailed: return QT_TRANSLATE_NOOP("Messages", "Could not set the address of the KettleLAN network adapter: %1");
    case Msg::TunWintunUntrusted: return QT_TRANSLATE_NOOP("Messages", "wintun.dll in the KettleLAN folder is not the official file signed by WireGuard LLC – it may have been replaced. Download KettleLAN again from the official Releases page.");
    case Msg::TunServiceUnavailable: return QT_TRANSLATE_NOOP("Messages", "The KettleLAN service is not running. Reinstall KettleLAN, or start “KettleLAN Service” in Windows Services.");
    case Msg::TunServiceUntrusted: return QT_TRANSLATE_NOOP("Messages", "Another program pretends to be the KettleLAN service – KettleLAN does not connect to it.");
    case Msg::TunServiceRejected: return QT_TRANSLATE_NOOP("Messages", "The KettleLAN service refused the connection: %1");
    case Msg::TunServiceVersion: return QT_TRANSLATE_NOOP("Messages", "The KettleLAN service has a different version – reinstall KettleLAN.");
    case Msg::UnsafeFolder: return QT_TRANSLATE_NOOP("Messages", "KettleLAN runs as administrator from a folder that other programs can change: %1");
    }
    return "";
}

std::string english(const Message& message) {
    std::string out = messageTemplate(message.id);
    for (size_t i = 0; i < message.args.size() && i < 9; ++i) {
        const std::string placeholder = "%" + std::to_string(i + 1);
        for (size_t pos = out.find(placeholder); pos != std::string::npos; pos = out.find(placeholder, pos + message.args[i].size()))
            out.replace(pos, placeholder.size(), message.args[i]);
    }
    return out;
}

bool isPeerEvent(Msg id) { return id == Msg::PeerJoined || id == Msg::PeerLeft; }

bool isNetworkStateEvent(Msg id) {
    return id == Msg::Connecting || id == Msg::Connected || id == Msg::Left || id == Msg::Merging;
}

} // namespace kettle

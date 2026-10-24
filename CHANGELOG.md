# Changelog

All notable changes to KettleLAN. The entry for a version becomes the description of its GitHub release.

Format: one flat list per version, one line per change – `Area: what changed`, imperative, no intro text.
Areas: Network, Security, GUI, CLI, Linux, Windows, AppImage, Packaging, Docs, CI.
Before tagging a release add a `## [x.y.z] – date` section (see [docs/RELEASING.md](docs/RELEASING.md)).

## [Unreleased]

- Windows: KettleLAN runs as the user – the new KettleLAN Service (SYSTEM) runs the Wintun adapter
- Windows: no UAC prompt when KettleLAN starts; start with the system through the normal "Run" registry key
- Windows: the adapter stays ready while the service runs – KettleLAN starts without the "identifying network" delay
- Security: the service accepts only KettleLAN from its own folder and checks every packet from the program
- Security: the program checks that it talks to the real service (a Windows service in session 0)
- Windows: installer (Inno Setup) – installs to Program Files, installs the service, Start menu shortcut, optional desktop shortcut and autostart
- Windows: updating closes KettleLAN and the service first; uninstalling removes the service and its firewall rules and keeps your saved networks
- Windows: the installer replaces the zip package
- Windows: smaller package without the unused Qt styles
- Network: new protocol version 3 – not compatible with KettleLAN 0.2, everyone in a network needs 0.3
- Security: derive the lobby tag, the HELLO proof key and the name key from the network code with Argon2id, so the code cannot be guessed from lobby data or received proofs
- Security: encrypt the network name in Steam lobby data (XChaCha20-Poly1305)
- Security: generate network codes and nonces with the system's cryptographic random generator (getrandom, BCryptGenRandom)
- Network: be in up to five networks at the same time, with one adapter and one address
- Network: say when someone in the same network still uses KettleLAN 0.2
- Security: verify members separately in each network, never forward traffic between networks
- CLI: join several networks, "leave n" / "leave all", list all networks with their members
- GUI: new home screen – your address once at the top, all your networks with on/off switches, "x of 5 connected"
- GUI: open a connected network to see its members, go back to the list with the arrow
- GUI: join a network with a code in a dialog
- GUI: rejoin all networks that were connected when KettleLAN was closed
- Network: the same network name for everyone with the same code, kept by every member and restored when an unnamed lobby is reopened
- Network: only whoever opened the network (★) can rename it for everyone
- GUI: add your own note to a network, shown under its name only on your computer
- GUI: network details (name, note, code, forget) from the list or with the pencil on the network screen
- GUI: at most 20 saved networks
- Network: keep Steam sessions with members open (ping always shown, faster first game packet)
- GUI: list members with you first, then by ping, then the ones still connecting
- CLI: "join <code> [name]", "rename <n> <name>"
- GUI: short system tray menu (Show window, Quit KettleLAN) – everything else is in the window

## [0.2.1] – 2026-10-22

- Windows: allow only UDP and ping from KettleLAN network members in Windows Firewall (was: all protocols), remove the old rule
- Windows: load wintun.dll only after checking its WireGuard LLC signature on a file locked against changes
- GUI: fix "member(s)" in the member count
- CI: pin all GitHub Actions to commit SHAs, keep them updated with Dependabot
- CI: set read-only permissions for the test workflow
- Docs: warn that the Windows version is not code-signed yet, download only from the official Releases page
- Docs: add instructions for verifying a download (PowerShell, sha256sum, build attestation)
- Docs: add an FAQ entry about antivirus and SmartScreen warnings
- Windows: point to the official download page in README.txt inside the zip

## [0.2.0] – 2026-10-22

- Windows: add a Windows 10/11 (64-bit) version – portable zip with the official signed Wintun driver
- Windows: run as administrator (needed by Wintun), start with the system through Task Scheduler
- Windows: allow incoming game traffic with a firewall rule limited to the KettleLAN adapter and 100.64.0.0/10
- CI: build and test the Windows version with MSVC, add the zip to releases
- GUI: switch the interface to English
- CLI: rename commands to English (`create`, `join`, `list`, `code`, `leave`, `help`, `quit`)
- CLI: ask for the password through `sudo` when the polkit prompt is not available (e.g. over SSH)
- Linux: show a clear message when administrator authentication fails instead of "pkexec is not installed"
- AppImage: run natively on Wayland
- Docs: translate README, SECURITY and CHANGELOG to English
- CI: use only the CHANGELOG entry as the release description
- Docs: state that KettleLAN is developed with the help of Claude (Anthropic)
- Docs: add THIRD-PARTY-NOTICES.md with the licenses of all third-party components
- AppImage: include LGPL/GPL texts, a list of bundled libraries and their license files
- Packaging: include THIRD-PARTY-NOTICES.md in the .deb packages
- GUI: add a Licenses link and an AI-assistance note to Settings

## [0.1.0] – 2026-10-21

- Network: create a virtual LAN over Steam networking (direct or through Valve's relays)
- Network: add networks with a permanent code (`KTL-XXXX-XXXX-XXXX`) that work without the creator
- Network: give every player a stable `100.x.x.x` address derived from the Steam account
- Network: merge duplicate lobbies of the same network automatically
- Security: hide the IP address by default (Steam relay only)
- Security: block access to the computer's services (SSH, file sharing, remote desktop, databases, admin panels)
- Security: stop service announcements (mDNS, NetBIOS, SSDP…) from reaching other players
- Security: accept traffic only from members who prove knowledge of the network key, drop spoofed packets
- Security: show names and network names from other players as plain text only
- Security: run without root, create the network adapter with a strictly validated helper
- Security: add attack scenario tests, fuzzing and helper tests, run on every change
- GUI: add saved networks, automatic reconnect and start with the system
- GUI: show members with ping and connection type, copy an address with one click
- GUI: add system tray icon with menu and notifications, dark and light theme
- CLI: add `kettlelan-cli` for diagnostics and SSH sessions
- Packaging: build AppImage and .deb packages (Debian 12, Ubuntu 24.04) with GitHub Actions and build attestation
- Known issue: a game server may not appear in the game's LAN list – connect by typing its KettleLAN address
- Known issue: Steam installed as Flatpak or Snap may not work

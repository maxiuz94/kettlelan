# Changelog

All notable changes to KettleLAN. The entry for a version becomes the description of its GitHub release.

Format: one flat list per version, one line per change – `Area: what changed`, imperative, no intro text.
Areas: Network, Security, Steam, GUI, CLI, Linux, Windows, AppImage, Packaging, Docs, CI.
Before tagging a release add a `## [x.y.z] – date` section (see [docs/RELEASING.md](docs/RELEASING.md)).

## [Unreleased]

- Windows: install KettleLAN Service in its own folder in Program Files; the program can go to any folder or drive
- Windows: do not start the service from a folder ordinary programs can change
- Windows: disconnect when KettleLAN or the service takes no data for 5 seconds, so neither can freeze the other
- Windows: set autostart only in KettleLAN's settings, so it always applies to the account that uses KettleLAN
- Windows: do not log a missing adapter at the first start as an error

## [0.3.0] – 2026-10-25

- Network: new protocol version 3 – not compatible with KettleLAN 0.2; network codes stay the same, everyone in a network needs 0.3
- Network: be in up to five networks at the same time, with one adapter and one address
- Network: the same network name for everyone with the same code; only whoever opened the network (★) can rename it
- Network: your own name in networks instead of the Steam name (Settings → Your name)
- Network: keep Steam sessions with members open – ping always shown, faster first game packet
- Network: say when someone in the same network still uses KettleLAN 0.2
- Security: derive the lobby tag, the HELLO proof key and the name key from the network code with Argon2id – the code cannot be guessed from lobby data or received proofs
- Security: encrypt the network name and nicknames in Steam lobby data (XChaCha20-Poly1305, Monocypher)
- Security: verify members separately in each network, never forward traffic between networks
- Security: generate network codes and nonces with the system's cryptographic random generator
- Steam: ▶ / ⏹ button connects to and disconnects from Steam – with ⏹ Steam no longer shows "Spacewar"; the state is remembered
- Steam: everything that talks to Steam runs in a separate background process (kettlelan-steam)
- Steam: wait for Steam after login and connect automatically – no "Try again" needed
- GUI: new home screen – your address, all your networks with on/off switches, "Connected to Steam" under the title
- GUI: open a connected network to see its members, back to the list with the arrow; members sorted by ping
- GUI: network details with the pencil: shared name, your own note (only on your computer), code, forget
- GUI: "Steam friend" badge for your Steam friends (checked on your computer only), ⚠ when two members use the same name
- GUI: join a network with a code in a dialog; reconnect the networks that were on when KettleLAN was closed
- GUI: system notifications instead of pop-ups in the window; "Copied" shown where you clicked
- GUI: short system tray menu (Show window, Quit KettleLAN); at most 20 saved networks
- Windows: KettleLAN runs as the user – the new KettleLAN Service runs the Wintun adapter; no UAC prompt when KettleLAN starts
- Windows: the service accepts only KettleLAN from its own folder and checks every packet from it; KettleLAN checks that it talks to the real service
- Windows: installer (Inno Setup) instead of the zip – Program Files, the service, Start menu shortcut, optional desktop shortcut and autostart
- Windows: updating closes KettleLAN and the service first; uninstalling removes the service and its firewall rules and keeps your networks
- Windows: the adapter stays ready while the service runs – no "identifying network" delay; smaller package without unused Qt styles
- CLI: several networks (`leave n`, `leave all`), `join <code> [name]`, `rename <n> <name>`, `name [nickname]`
- Docs: FAQ about "Spacewar" in Steam and other games, updated screenshots

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

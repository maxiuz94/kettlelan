# Changelog

All notable changes to KettleLAN. The entry for a version becomes the description of its GitHub release.

Format: one flat list per version, one line per change – `Area: what changed`, imperative, no intro text.
Areas: Network, Security, GUI, CLI, Linux, AppImage, Packaging, Translations, Docs, CI.
Before tagging a release add a `## [x.y.z] – date` section (see [docs/RELEASING.md](docs/RELEASING.md)).

## [Unreleased]

- GUI: use English by default, add Polish translation
- GUI: offer Polish on first start on a Polish system
- GUI: add language selection to Settings, switch without restart
- GUI: use correct Polish plural forms for the member count
- CLI: rename commands to English (`create`, `join`, `list`, `code`, `leave`, `help`, `quit`)
- CLI: ask for the password through `sudo` when the polkit prompt is not available (e.g. over SSH)
- Linux: show a clear message when administrator authentication fails instead of "pkexec is not installed"
- AppImage: run natively on Wayland
- Docs: translate README, SECURITY and CHANGELOG to English, add Polish README and SECURITY
- CI: use only the CHANGELOG entry as the release description

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

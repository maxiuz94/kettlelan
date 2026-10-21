# Changelog

All notable changes to KettleLAN are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

The entry for a version is used as the description of its GitHub release – add a
`## [x.y.z] – date` section before tagging a release (see [docs/RELEASING.md](docs/RELEASING.md)).

## [Unreleased]

### Added
- **English interface** (now the default) with a **Polish translation** – on a Polish system KettleLAN
  offers Polish on first start, and the language can be changed at any time in the settings.
- The Polish plural forms of the member count ("1 osoba / 2 osoby / 5 osób") are handled by Qt.
- When the system password prompt is not available (e.g. in an SSH session), the command-line version
  asks for the password through `sudo`.
- The AppImage runs natively on Wayland.

### Changed
- `kettlelan-cli` commands are now in English: `create`, `join`, `list`, `code`, `leave`, `help`, `quit`.
- The repository, documentation and release notes are in English, with a Polish README.

### Fixed
- A clear message when administrator authentication fails, instead of "pkexec is not installed".

## [0.1.0] – 2026-10-21

First release. **KettleLAN is an open-source virtual LAN over the Steam network** – a Hamachi alternative
without a player limit. Play LAN games (Quake 3, Unreal Tournament, Counter-Strike 1.6, Warcraft III…)
with your friends as if you were on the same router – no port forwarding and no server of your own.

### Features
- **Networks with a permanent code** (`KTL-XXXX-XXXX-XXXX`) – create a network once, share the code, and it
  works no matter who created it. The first person to connect opens the network, everyone else joins.
- **Your networks** list, automatic reconnect to the last network and start with the system.
- **A stable `100.x.x.x` address** for every player – click a person to copy their address for the game.
- **Ping and connection type** (direct / Steam relay) for every member.
- **System tray icon** with a menu (copy addresses, network code, quick connect), notifications,
  dark and light theme following the system.
- Command-line version `kettlelan-cli` for diagnostics and SSH sessions.

### Security and privacy
- **Your IP address is hidden by default** – traffic goes through Steam's relays, other players never learn it.
- **Your computer's services are protected** – other players cannot reach SSH, file sharing, remote desktop
  or databases, and your system does not announce its name and services to them (mDNS, NetBIOS, SSDP…).
- Traffic is accepted only from members who know the network key, with protection against spoofing.
- The program runs without administrator rights; a small, strictly validated helper creates the adapter.
- Security tests (attack scenarios, fuzzing, helper tests) run on every change.
  Details in [SECURITY.md](https://github.com/maxiuz94/kettlelan/blob/main/SECURITY.md).

### Tested on
- Arch Linux (KDE Plasma, Wayland) and MX Linux 23 (Xfce), including between these two computers over
  KettleLAN – with a game of OpenArena (Quake 3).

### Known limitations
- Linux only for now – the Windows version is in progress.
- A game server may not appear in the game's "LAN" list – connect by typing the address from KettleLAN.
- KettleLAN uses Steam's test App ID, so your friends see you as "playing Spacewar".
- Steam installed as a Flatpak or Snap may not work – the regular Steam package is recommended.

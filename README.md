<div align="center">

<img src="packaging/linux/icons/kettlelan-128.png" width="96" alt="KettleLAN logo">

# KettleLAN

**An open-source virtual LAN over the Steam network.**<br>
Play classic LAN games with your friends as if you were on the same router –<br>
no player limit, no port forwarding, no server of your own.

[![Build](https://github.com/maxiuz94/kettlelan/actions/workflows/build.yml/badge.svg)](https://github.com/maxiuz94/kettlelan/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/maxiuz94/kettlelan)](https://github.com/maxiuz94/kettlelan/releases/latest)
[![License: BSD-3-Clause](https://img.shields.io/github/license/maxiuz94/kettlelan)](LICENSE)
![Platform: Linux | Windows](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-informational)

<img src="docs/screenshots/start.png" width="250" alt="Start screen">&nbsp;
<img src="docs/screenshots/network.png" width="250" alt="Network screen">&nbsp;
<img src="docs/screenshots/settings.png" width="250" alt="Settings">

**For players:** [What it is](#what-is-kettlelan) · [Download](#download) · [Quick start](#quick-start) ·
[Playing games](#playing-games) · [Privacy](#privacy-and-safety) · [FAQ](#faq)<br>
**For advanced users:** [How it works](#how-it-works) · [Command line](#command-line-version) ·
[Logs](#logs-and-troubleshooting) · [Building from source](#building-from-source)

</div>

## What is KettleLAN?

Old multiplayer games – Quake 3, Unreal Tournament, Counter-Strike 1.6, Warcraft III, Age of Empires II and
many more – were made for LAN parties, with all computers on one local network. KettleLAN builds such a
network over the internet, like Hamachi does, but:

- **no player limit** and no paid plans,
- **no port forwarding** and no router settings – it works behind any router, even behind carrier-grade NAT,
- **no server and no extra account** – it uses the network you and your friends already have: **Steam**.

You create a network, send its code to your friends, and from then on your computers see each other like on
one LAN. Each player gets an address like `100.80.46.79` that the game uses.

**What you need:**

- **Steam**, installed and signed in, on every computer (any account – you do not have to be Steam friends),
- **a game with LAN or "direct IP" multiplayer** – games that only have online matchmaking do not benefit,
- **KettleLAN** on every computer that plays.

## Download

Get the latest version from the [**Releases**](https://github.com/maxiuz94/kettlelan/releases/latest) page
([what's new](CHANGELOG.md)):

| System | File | How to install |
|--------|------|----------------|
| **Windows** 10/11<br>(64-bit) | [`KettleLAN-0.3.3-windows-x64-setup.exe`](https://github.com/maxiuz94/kettlelan/releases/latest/download/KettleLAN-0.3.3-windows-x64-setup.exe) | run the installer |
| **Ubuntu** 24.04+,<br>Mint 22+, Pop!_OS,<br>Debian 13 | [`kettlelan_0.3.3_ubuntu24.04_amd64.deb`](https://github.com/maxiuz94/kettlelan/releases/latest/download/kettlelan_0.3.3_ubuntu24.04_amd64.deb) | `sudo apt install ./kettlelan_*.deb` |
| **Debian 12**,<br>MX Linux 23 | [`kettlelan_0.3.3_debian12_amd64.deb`](https://github.com/maxiuz94/kettlelan/releases/latest/download/kettlelan_0.3.3_debian12_amd64.deb) | `sudo apt install ./kettlelan_*.deb` |
| **Any other Linux** | [`KettleLAN-0.3.3-x86_64.AppImage`](https://github.com/maxiuz94/kettlelan/releases/latest/download/KettleLAN-0.3.3-x86_64.AppImage) | `chmod +x KettleLAN-*.AppImage`,<br>then run it |

Click a file name to download it. The Windows installer asks for administrator rights once, and you can choose
any folder or drive. On Linux run the commands in the folder with the downloaded file.

On Linux use the regular Steam from your distribution or from Valve – the Flatpak and Snap versions of Steam
may not work.

> [!WARNING]
> **Download KettleLAN only from the [Releases page](https://github.com/maxiuz94/kettlelan/releases) of this
> repository.** Copies from other websites may be modified. Every file is built by GitHub Actions from this
> repository and comes with a SHA256 checksum – see [Verifying a download](SECURITY.md#verifying-a-download).
>
> **The Windows version is not code-signed yet.** Windows SmartScreen shows "Windows protected your PC"
> (click *More info* → *Run anyway*), and some antivirus programs may show a generic warning
> (see the [FAQ](#faq)).

## Quick start

1. **Start Steam** and sign in.
2. **Start KettleLAN.** On Linux your system asks for the administrator password the first time (and after
   every reboot) – this creates KettleLAN's virtual network adapter. On Windows the installer has already set
   it up. KettleLAN itself always runs as a normal user.
3. **One person creates the network:** **Create network**, give it a name, then **Invite friends** – the code
   (`KTL-XXXX-XXXX-XXXX`) is copied. Send it to your friends.
4. **Everyone else joins:** **Join with a code**, paste the code, **Join**.
5. **Play:** the host starts the game server as on a LAN. The others connect to the host's address – click a
   person on the list in KettleLAN to copy their address, then paste it into the game.

**Next time** the network is under **Your networks** – just switch it on. The code never changes, and it does
not matter who switches the network on first. You can be in up to five networks at the same time.

- **▶ / ⏹** in the header connects KettleLAN to Steam and disconnects it. Your networks stay switched on for
  next time.
- **Settings** has your name in networks (instead of your Steam name), start with the system, the theme and
  the privacy options.

## Playing games

### Which ports are open?

KettleLAN needs **no ports on your router** – everything goes through Steam. Inside a KettleLAN network it
behaves like a home LAN: **every port a game uses works**, TCP and UDP. The ports some popular LAN games use by
default:

| Game | UDP | TCP | Good to know |
|------|-----|-----|--------------|
| **Quake 3** | 27960 | – | more servers on one computer use 27961, 27962… |
| **Counter-Strike 1.6 / Source** | 27015<br>27005<br>27020 | 27015 | UDP 27015 is the server, 27005 the player, 27020 SourceTV; TCP only for remote console |
| **Unreal Tournament** | 7777⁠–⁠7781 | – | 7777 is the game, 7778 server info; the web admin (TCP 8080) is blocked by KettleLAN – not needed to play |
| **Warcraft III** | 6112 | 6112 | UDP finds LAN games, TCP is the game itself |
| **Age of Empires II** (classic) | 2300⁠–⁠2400 | 2300⁠–⁠2400<br>47624 | the original version from 1999 (DirectPlay) |
| **Minecraft** (Java) | – | 25565 | *Open to LAN* uses a random port shown in the chat – connect to `address:port` |
| **Terraria** | – | 7777 | you can choose another port when you start the server |
| **Factorio** | 34197 | – | the same port for the server and the players |

You do not have to open or set up any of them. Port-forwarding websites list more ports for many games – for
example ports of the Steam client itself, or TCP for games that only use UDP. They are meant for your router,
which KettleLAN does not need. Ping works too.

### What does KettleLAN block?

With **Settings → Protect this computer's services** (on by default) other players cannot reach services that
have nothing to do with games:

- remote access: SSH, Telnet, remote desktop (RDP, VNC), X11,
- file sharing: Windows sharing (SMB, NetBIOS), NFS, AFP, Syncthing, printers,
- web servers and admin panels: ports 80, 443, 3000, 5000–5001, 8000, 8080–8081, 8443, 8888, 9000, 9090–9091,
  9443, 10000, Jellyfin, Plex,
- databases: MySQL/MariaDB, PostgreSQL, MS SQL, MongoDB, Redis, Memcached,
- KDE Connect, Steam Remote Play, UPnP/SSDP, mDNS and LLMNR (your computer's name), DNS, mail.

Your computer also does not announce its name and shared folders to other players. The full list with port
numbers is in [`src/core/filter.cpp`](src/core/filter.cpp).

**Does this block any game?** Common games do not use these ports. If a game or a game server of yours does
(for example a server panel on port 8080), switch the protection off while you play, or move the server to
another port.

Also blocked, because games do not need them: network protocols other than TCP, UDP and ICMP, ICMP messages
other than ping, "unreachable" and "time exceeded", and IP fragments used to hide a port number.

**Anything else to know:**

- **Windows asks "Allow access?"** when a game starts a server. Allow it and **tick "Public networks"** –
  Windows treats KettleLAN as a public network. UDP and ping are already allowed by KettleLAN, so games that
  host over UDP work either way; games that host over TCP (Minecraft, Terraria, Warcraft III) need this. If you
  clicked it away: *Windows Security → Firewall & network protection → Allow an app through firewall*, then
  tick *Public* for the game.
- **On Linux** KettleLAN does not change your firewall. If a game cannot be reached and your system uses a
  firewall (ufw, firewalld…), allow the `kettle0` adapter: `sudo ufw allow in on kettle0` (ufw) or
  `sudo firewall-cmd --permanent --zone=trusted --add-interface=kettle0 && sudo firewall-cmd --reload`
  (firewalld). KettleLAN's own protection keeps working.
- **The game's LAN server list may stay empty.** Many games look for servers only on your home network. Type
  the host's KettleLAN address (`100.x.x.x`) instead.
- **IPv4 only.** Very old games that need IPX (for example DOS games and early StarCraft) or games that use only
  IPv6 do not work over KettleLAN.

## Privacy and safety

- **Your IP address is hidden** – traffic goes through Steam's servers, so other players never learn your
  public address (*Settings → Hide my IP address*). Switching it off can lower the ping a little, but then
  people in your networks can learn your address.
- **Your computer's services are protected** – see [What does KettleLAN block?](#what-does-kettlelan-block)
- **Only people with the code get in**, and every packet is checked: it must come from a verified member and
  carry that member's own address.
- **What other players do see:** your game servers and your game's LAN broadcasts – like on a real LAN. If you
  are in several networks, members of each of them see them, so switch off the networks you are not playing in.
- **Share the code only with people you trust.** If someone you do not want is in your network, **block them**:
  hover over the person in the list and click ⊘. Nothing from them reaches your computer and they do not see you
  or your game servers – in every network, until you unblock them (*Settings → Blocked people*). They are not
  told. To remove someone from the network for everybody, create a new network and share the new code.
- **No flooding.** If one person sends far more than any game does, KettleLAN drops the excess, so they cannot
  slow down your computer or your game.
- People in a network can see your Steam account in Steam, even if you use your own name in KettleLAN.

More details and how to report a vulnerability: [SECURITY.md](SECURITY.md).

## FAQ

**The ping is higher than expected.** By default all traffic goes through Steam's servers to hide your IP
address, which adds a few milliseconds. The first seconds after joining can be slower while Steam sets up the
connection. Switching *Hide my IP address* off allows direct connections.

**Can I play with someone who is not my Steam friend?** Yes – anyone with the network code can join. Steam
friends get a "Steam friend" badge on the list.

**Two of my computers use the same Steam account.** Your KettleLAN address is derived from your Steam account,
so both computers would get the same address – KettleLAN shows an "Address conflict". Use a different Steam
account on each computer that plays at the same time.

**Is there a player limit?** Steam lobbies allow up to 250 members.

**Why do my friends see me "playing Spacewar"?** While KettleLAN is connected to Steam, Steam treats it as a
running game – Valve's free test app "Spacewar". Steamworks programs cannot hide this. Press **⏹** in
KettleLAN's header to disconnect from Steam and the status disappears; **▶** connects again. If you start
another Steam game, your friends see that game.

**Why "Spacewar"?** It is Valve's free test App ID that every Steam account can use. A dedicated App ID would
require publishing KettleLAN on Steam.

**Does KettleLAN get in the way of other games?** No. Steam lets several games run at the same time, and every
game has its own lobbies and networking. If you prefer, press **⏹** before you start another game.

**My antivirus or Windows SmartScreen warns about KettleLAN.** The Windows version is new and not yet
code-signed, and it does things that heuristic scanners treat as suspicious: its installer asks for
administrator rights, installs a network driver (Wintun) and a Windows service, and adds firewall rules. A
single generic detection (for example `Trojan.Malware.300983.susgen` from MaxSecure) is a known false-positive
pattern for new, unsigned programs. Make sure the file comes from the
[Releases page](https://github.com/maxiuz94/kettlelan/releases) and [verify it](SECURITY.md#verifying-a-download).
If a well-known antivirus flags an official release, please
[open an issue](https://github.com/maxiuz94/kettlelan/issues) with the scan link – we report false positives to
the vendors.

---

## How it works

For those who want to know what happens under the hood. The security design is described in detail in
[SECURITY.md](SECURITY.md).

- **Addresses.** Every player gets a virtual network adapter with an address in `100.64.0.0/10`, derived from
  their Steam account (the same on every computer and every network). Games see an ordinary local network.
  The adapter uses an MTU of 1200 bytes so that packets fit into Steam's messages.
- **Networks.** A network is a Steam lobby. From the network code KettleLAN derives keys with Argon2id: a tag
  to find the lobby, a key to prove membership, and a key that encrypts the network name and nicknames
  (XChaCha20-Poly1305) in the lobby data. Before any traffic flows, members prove to each other that they know
  the key (HELLO). Several people can open the same network at the same time – the lobbies are merged.
- **Traffic.** Packets travel through Steam's networking (Steam Datagram Relay): encrypted, authenticated with
  Steam certificates, through Valve's relays (or directly, if *Hide my IP address* is off). Packets to one
  address go only to that member; broadcasts go to all verified members.
- **Filter.** Before a packet reaches your system, KettleLAN checks that it comes from a verified member, carries
  that member's own address, is addressed to you (or is a broadcast), and is not aimed at a protected service.
  KettleLAN never forwards traffic between members or into your home network.
- **Linux.** The adapter `kettle0` is created by `kettlelan-helper`, a small script that only creates and
  removes this adapter and validates its arguments strictly. It runs as root through polkit (`pkexec`), or
  `sudo` in a terminal without a desktop. KettleLAN itself never runs as root.
- **Windows.** The adapter (Wintun) belongs to *KettleLAN Service*, which runs as SYSTEM from
  `C:\Program Files\KettleLAN Service`, wherever you installed KettleLAN. The program talks to it through a
  local pipe, and the service checks every packet again. Firewall rules allow UDP and ping only on this adapter
  and only from `100.64.0.0/10`.
- **Steam.** Everything that talks to Steam runs in a separate process (`kettlelan-steam`), started by the
  KettleLAN window only while it is connected to Steam (**▶**). KettleLAN uses Valve's test App ID `480`.

## Command-line version

`kettlelan-cli` does everything the window does, in a terminal – handy over SSH or on a computer without a
desktop. Run only one KettleLAN (window or terminal) at a time.

| Command | What it does |
|---------|--------------|
| `create [name]` | create a new network and show its code |
| `join <code>` | connect to a network |
| `list` | your networks, their members, addresses and ping |
| `code` | show the codes of your networks |
| `name [nickname]` | your name in networks (without a nickname: your Steam name) |
| `rename <n> <name>` | rename network *n* for everyone (only the host, marked `*`) |
| `leave [n\|all]` | leave one network or all of them |
| `block <address>`, `unblock <address>`, `blocked` | block a person in every network (until you quit), unblock them, list who is blocked |
| `quit` | exit |

## Logs and troubleshooting

- **Program log:** *Settings → Open log*. In a terminal, `kettlelan-cli` prints the same messages.
- **Windows service log:** `C:\ProgramData\KettleLAN\service.log`. In PowerShell:
  `Get-Service KettleLAN` (should be *Running*) and
  `Get-Content C:\ProgramData\KettleLAN\service.log -Tail 20 -Encoding UTF8`.
- **Steam must be running and signed in** before KettleLAN connects. If KettleLAN shows "Not connected to
  Steam", press **▶**.
- **Problems or ideas:** [open an issue](https://github.com/maxiuz94/kettlelan/issues) and attach the log.

## Building from source

### Linux

1. Install the packages:

   | Distribution | Command |
   |--------------|---------|
   | Arch / Manjaro | `sudo pacman -S --needed base-devel cmake git polkit iproute2 qt6-base qt6-declarative` |
   | Ubuntu 24.04+ / Debian 12+ | `sudo apt install build-essential cmake git pkexec iproute2 qt6-base-dev qt6-declarative-dev qml6-module-qtquick qml6-module-qtqml qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-shapes qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtqml-workerscript` |
   | Fedora | `sudo dnf install gcc-c++ cmake git polkit iproute qt6-qtbase-devel qt6-qtdeclarative-devel` |

2. Download the **Steamworks SDK** (free, needs a Steam account). Valve's license does not allow it in this
   repository, so `build.sh` opens the download page for you and then finds `steamworks_sdk_XXX.zip` in your
   Downloads folder.
3. Build and install:

   ```sh
   git clone https://github.com/maxiuz94/kettlelan.git
   cd kettlelan
   ./scripts/build.sh               # builds and runs the tests
   ./scripts/install-local.sh       # optional: adds KettleLAN to your application menu
   ```

   The program is then in `build/` (`kettlelan` – the window, `kettlelan-cli` – the terminal version).
   On a computer with little memory use `JOBS=2 ./scripts/build.sh`.

Options for `build.sh` (passed on to CMake):

| Option | Effect |
|--------|--------|
| `-DKETTLELAN_BUILD_GUI=OFF` | only the terminal version, without Qt |
| `-DKETTLELAN_SECURITY_TESTS=ON` | adds attack-simulating commands to `kettlelan-cli` – for testing only, see [SECURITY.md](SECURITY.md#tests-on-two-computers) |
| `-DKETTLELAN_SANITIZE=ON` | builds the tests with AddressSanitizer and UndefinedBehaviorSanitizer |

Packages: `packaging/linux/build-appimage.sh` (AppImage) and `cpack` with `-DKETTLELAN_PACKAGE=deb` (.deb).

### Windows

You need Visual Studio 2022 (C++ workload), CMake, Qt 6.8 for MSVC 2022 64-bit and Inno Setup 6. In Git Bash:

```sh
packaging/windows/build-installer.sh build-windows /path/to/steamworks_sdk_XXX.zip
```

It builds KettleLAN, runs the tests and creates the installer. Wintun is downloaded automatically from
wintun.net and verified with SHA256.

### Tests and releases

- `ctest --test-dir build --output-on-failure` runs all tests: the core, attack scenarios, fuzzing and the
  Linux helper. They need no Steamworks SDK and run on every change in GitHub Actions.
- Releases are built by GitHub Actions from the official SDK file listed in
  [`cmake/steamworks-sdk.sha256`](cmake/steamworks-sdk.sha256) – see [docs/RELEASING.md](docs/RELEASING.md).

## Built with AI assistance

KettleLAN is developed with the help of **Claude**, an AI assistant made by
[Anthropic](https://www.anthropic.com). Most of the code, tests and documentation were written by Claude
following the maintainer's requirements and decisions. Every change was built, tested on real hardware
(Arch Linux with KDE Plasma, MX Linux with Xfce, Windows, computers playing together over KettleLAN) and
checked by the automated tests before being published.

If you find a bug or a security problem, please report it – see [SECURITY.md](SECURITY.md).

## License

KettleLAN is licensed under the **BSD 3-Clause License** – see [LICENSE](LICENSE).
It started as a fork of [PartyLAN](https://github.com/gyf304/partylan) by Yifan Gu (also BSD 3-Clause).

Components by other authors keep their own licenses – details in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md):

| Component | License |
|-----------|---------|
| Steam library (`libsteam_api.so`, `steam_api64.dll`) | Valve proprietary, shipped under the Steamworks SDK Access Agreement |
| Qt 6 | LGPL-3.0 – system library in the .deb packages, bundled in the AppImage and the Windows installer |
| Wintun (Windows) | prebuilt binaries by WireGuard LLC, distributed under their license |
| Microsoft Visual C++ runtime (Windows) | Microsoft redistributable |
| Libraries bundled in the AppImage | their own open-source licenses, listed inside the AppImage |

Steam and the Steam logo are trademarks of Valve Corporation. KettleLAN is not affiliated with or
endorsed by Valve. Game names are trademarks of their respective owners.

# KettleLAN security

This document describes what KettleLAN protects, what it does not, and how to report a problem.

## Reporting a vulnerability

Please **do not open a public issue**. Use GitHub's private reporting instead:
**Security → Report a vulnerability** in this repository.

## Who sees what

| Who | What they see |
|-----|---------------|
| A third party (your ISP, someone on the same Wi-Fi) | Only encrypted Steam UDP traffic (AES-GCM, identities verified with Steam certificates). No game data and no `100.x.x.x` addresses. |
| A member of your network, **"Hide my IP address" on** (default) | Your Steam name and your `100.x.x.x` address. Traffic goes through Valve's relays, so they **never learn your public IP**. |
| A member of your network, hiding off | As above, plus your public IP address when the connection is direct. |
| Valve | That you use Steam networking in the Spacewar app (App ID 480) and whom you connect to. |

## Protecting your computer

- **The program does not run as root.** Only the `kettlelan-helper` script gets administrator rights; it can
  only create or remove the `kettle0` adapter and validates its arguments strictly. The password is collected
  by the system's polkit prompt; in a terminal without a desktop (e.g. over SSH) KettleLAN uses `sudo`.
- **"Protect this computer's services"** (on by default): packets from members to SSH, file sharing
  (SMB, NFS, AFP), remote desktop (RDP, VNC), databases, admin panels, KDE Connect, Steam Remote Play and
  similar services are dropped before they reach the system. Protocols other than TCP/UDP/ICMP and suspicious
  IP fragments (RFC 1858) are dropped too. Full list: [`src/core/filter.cpp`](src/core/filter.cpp).
- With protection on, your system **does not announce** its services in the KettleLAN network (mDNS/Avahi,
  NetBIOS, SSDP, KDE Connect, Spotify…), which would reveal the computer's name and shared resources.
- **No access to your home network.** Only packets addressed to your `100.x.x.x` address (or broadcasts) are
  accepted. KettleLAN does not forward traffic and does not change your internet route – only the
  `100.64.0.0/10` range goes through `kettle0`.
- **Verified members only.** Steam connections are accepted only from people in the network's lobby, and
  packets only from those who proved that they know the key from the code – and only with their own address.
- **Text from other people** (Steam names, network names) is cleaned of control characters and shown as plain
  text – no HTML, so it cannot be used, for example, to load an image from someone's server.
- The file with saved networks and the log are readable by your account only (permissions `600`).

## Windows

- Wintun needs administrator rights to create the adapter **and** to exchange packets, so on Windows the whole
  program runs elevated (UAC prompt on start). This is a temporary design: a separate system service for the
  adapter, with the interface running as a normal user, is planned.
- `wintun.dll` is loaded only from the program's own folder (protection against DLL planting), and only after
  KettleLAN has checked its digital signature (WireGuard LLC) on a file locked against changes. The shipped file is
  the official build, verified with SHA256 at build time.
- Because the program runs elevated, its folder must not be writable by ordinary programs: the Qt libraries and
  `steam_api64.dll` are loaded by Windows before KettleLAN starts and cannot be checked by it. Extract KettleLAN to
  `C:\Program Files\KettleLAN`; KettleLAN warns when it runs from inside your user profile (Downloads, Desktop…).
  The planned system service will be installed there automatically.
- A Windows Firewall rule allows incoming traffic **only on the KettleLAN adapter and only from `100.64.0.0/10`**.
  The KettleLAN network stays a "Public" network, so Windows does not share files or printers on it, and
  KettleLAN's own filter blocks computer services as on Linux.

## The network code

- The code (`KTL-XXXX-XXXX-XXXX`, 60 bits) works like a password. Without it nobody can find the network's
  lobby (the lobby is looked up by a hash of the whole code) or join it.
- There is no "owner" who could kick someone. If the code reaches the wrong person, create a new network
  and share the new code only with people you trust.

## What KettleLAN does not protect against

- Members of a network are like computers on one LAN: they see the game traffic sent to them and can
  connect to game ports on your computer. Only let people you trust into your network.
- Service protection is based on a list of ports. A service on an unusual port (e.g. your own server on 8123)
  is reachable by members – stop it or block it in your firewall for the `kettle0` adapter.
- `scripts/install-local.sh` puts the helper into your home folder. Malware running under your account could
  replace it. **The .deb package does not have this problem**: the helper lives in `/usr/lib/kettlelan/`
  (owned by root) and the password prompt has its own description (polkit action
  `io.github.maxiuz94.kettlelan.helper`).
- An **AppImage** is a single file owned by you – it has the same level of trust as any other program you run
  from your account. Before creating the adapter the helper is copied into a fresh private folder (`0700`),
  because root cannot access the inside of a mounted AppImage.

## Unsigned Windows builds

The Windows version is **not code-signed yet**. This means:

- Windows SmartScreen shows "Windows protected your PC" and the UAC prompt shows "Unknown publisher".
- Some antivirus programs may report a generic heuristic detection.
- You cannot rely on a publisher name to know that a file is genuine – **download KettleLAN only from the
  [Releases page](https://github.com/maxiuz94/kettlelan/releases) of this repository** and verify it as described
  below. Any other source (mirrors, download sites, files sent by other people) may be modified.

Code signing for open-source projects is planned.

## Verifying a download

Every file on the Releases page shows its SHA256 next to it, and `SHA256SUMS.txt` lists all of them.

- **Windows** (PowerShell):
  ```powershell
  Get-FileHash .\KettleLAN-0.2.0-windows-x64.zip -Algorithm SHA256
  ```
- **Linux**:
  ```sh
  sha256sum -c SHA256SUMS.txt --ignore-missing
  ```
- **Build origin** (any system with the [GitHub CLI](https://cli.github.com)) – proves that the file was built by
  GitHub Actions in this repository from a tagged release:
  ```sh
  gh attestation verify KettleLAN-0.2.0-windows-x64.zip -R maxiuz94/kettlelan
  ```

The bundled `wintun.dll` and `steam_api64.dll` keep their original digital signatures (WireGuard LLC and Valve);
you can see them in the file's *Properties → Digital Signatures*.

## Where the release files come from

- Packages are built by GitHub Actions from the public code
  ([`.github/workflows/release.yml`](.github/workflows/release.yml)), and the build log is public.
- Every release has `SHA256SUMS.txt` and a build attestation – `gh attestation verify FILE -R maxiuz94/kettlelan`
  confirms that a file was built in this repository from a specific commit.
- All GitHub Actions are pinned to full commit SHAs (updated through Dependabot pull requests), so a moved tag
  cannot change what runs in the build.
- The AppImage tools (linuxdeploy) are downloaded in pinned versions and verified with SHA256.
- The only non-public input is Valve's official Steamworks SDK (its license does not allow publishing it).
  The workflow checks its SHA256 against [`cmake/steamworks-sdk.sha256`](cmake/steamworks-sdk.sha256) and stops
  if the file is different.

## How it is tested

`ctest --test-dir build` runs all of the tests below, and GitHub Actions repeats them on every change –
also in a build with AddressSanitizer and UndefinedBehaviorSanitizer, which stop on any memory error or
undefined behaviour.

- **`tests/test_security.cpp`** – attack scenarios against the network logic (`src/core/router.cpp`, the same
  code the program uses): someone without the key, replaying someone else's proof, a stranger outside the
  lobby, taking over someone's address, impersonating other members, the home router and localhost, attempts
  to reach your home network and the internet through you, access to your computer's services, leaks through
  broadcasts, address collisions, broken and oversized packets, HELLO flooding.
- **`tests/fuzz_core.cpp`** – millions of random and deliberately broken packets, messages, network codes and
  names fed to every function that reads data from other people.
- **`tests/test_helper.sh`** – malicious arguments for the helper that runs as root: command injection
  (`;`, `$(...)`, newline), addresses outside the network, octal notation, root and system account uids.
- **`tests/test_core.cpp`** – the service filter (including IP fragmentation tricks), text cleaning, network
  codes, proofs and tags.
- **Hardening** – built with stack protection, PIE, full RELRO and a non-executable stack.

### Tests on two computers

Attacks over the real Steam network cannot be tested on one computer. Use the command-line version built
in test mode:

```sh
cmake -B build-test -DKETTLELAN_SECURITY_TESTS=ON -DKETTLELAN_BUILD_GUI=OFF
cmake --build build-test --parallel
./build-test/kettlelan-cli
```

- `test-wrong-key <code>` – joins the network's lobby with a forged key proof. The other side should log
  "does not know the network key – ignoring", and pinging it must not work.
- `test-spoof <address>` – sends members a ping with a forged source address. The other side should log
  "with a forged source address (spoofing)" and increase its blocked-attempts counter.

These commands are never included in the regular packages.

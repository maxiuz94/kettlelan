# Third-party notices

KettleLAN itself is licensed under the BSD 3-Clause License (see [LICENSE](LICENSE)).
It uses or ships the components listed below, each under its own license.

| Component | Used for | License | Where |
|-----------|----------|---------|-------|
| PartyLAN | original idea and parts of the IPv4 code | BSD-3-Clause | source code |
| Steamworks SDK – `libsteam_api.so` | talking to the Steam client | Valve proprietary (Steamworks SDK Access Agreement) | all packages |
| Qt 6 | graphical interface | LGPL-3.0 | system library (.deb), bundled (AppImage, Windows) |
| Monocypher | Argon2id and authenticated encryption of network keys and names | BSD-2-Clause OR CC0-1.0 | source code (`third_party/monocypher`) |
| Wintun | virtual network adapter on Windows | prebuilt binaries license; API header GPL-2.0 OR MIT | Windows package, `third_party/wintun` |
| Microsoft Visual C++ runtime | needed by the Windows build | Microsoft redistributable | Windows package |
| System libraries | needed by Qt | various open-source licenses | bundled (AppImage) |

## PartyLAN

- Copyright (c) 2023, Yifan Gu – https://github.com/gyf304/partylan
- License: BSD 3-Clause. KettleLAN started as a fork of PartyLAN; `src/core/ip.h` and `src/core/ip.cpp` are
  based on its code. The copyright notice is kept in [LICENSE](LICENSE).

## Steamworks SDK (Valve Corporation)

- `libsteam_api.so` is a redistributable binary from the `redistributable_bin` folder of the Steamworks SDK.
  It is shipped with KettleLAN under the Steamworks SDK Access Agreement, which allows distributing the
  SDK redistributables together with an application in object code form.
- It is **not** covered by KettleLAN's BSD license and remains the property of Valve Corporation.
- The SDK itself (headers and other files) is not part of this repository – the license does not allow it.
  Releases are built with the official file listed in [`cmake/steamworks-sdk.sha256`](cmake/steamworks-sdk.sha256),
  available from https://partner.steamgames.com/downloads/list.
- Steam and the Steam logo are trademarks of Valve Corporation. KettleLAN is not affiliated with or
  endorsed by Valve.

## Qt 6

- Copyright (C) The Qt Company Ltd. and other contributors – https://www.qt.io
- License: GNU Lesser General Public License v3 ([`packaging/licenses/LGPL-3.0.txt`](packaging/licenses/LGPL-3.0.txt)),
  which builds on the GNU GPL v3 ([`packaging/licenses/GPL-3.0.txt`](packaging/licenses/GPL-3.0.txt)).
- KettleLAN uses the Qt modules Core, Gui, Widgets, Network, Qml, Quick, Quick Controls 2, Quick Layouts,
  Quick Shapes and DBus, linked **dynamically** and **unmodified**.
- **.deb packages** use the Qt libraries of your distribution – nothing from Qt is shipped.
- **AppImage** ships unmodified Qt shared libraries and plugins (Qt 6.8.3, official binaries).
  - Their source code is available from The Qt Company:
    https://download.qt.io/archive/qt/6.8/6.8.3/single/ (and https://code.qt.io).
  - You may replace them with your own build of a compatible Qt version: extract the AppImage
    (`./KettleLAN-*.AppImage --appimage-extract`) and swap the files in `squashfs-root/usr/lib` and
    `squashfs-root/usr/plugins`.
  - The license texts are included inside the AppImage in `usr/share/doc/kettlelan/licenses/`.

## Monocypher

- Copyright (c) 2017-2023 Loup Vaillant, Michael Savage, Fabio Scotoni – https://monocypher.org
- Version 4.0.3, unmodified, in [`third_party/monocypher`](third_party/monocypher) (from the official repository
  https://github.com/LoupVaillant/Monocypher, tag `4.0.3`). Dual-licensed under the 2-clause BSD license or CC0 1.0;
  KettleLAN uses it under the **2-clause BSD license** – see [`third_party/monocypher/LICENCE.md`](third_party/monocypher/LICENCE.md).
- The unit tests check it against the Argon2id test vector from RFC 9106.

## Wintun (WireGuard LLC)

- Copyright (C) 2018-2021 WireGuard LLC – https://www.wintun.net
- The Windows package ships the official, digitally signed `wintun.dll` (version 0.14.1) **unmodified**, under
  the Wintun prebuilt binaries license. The license text is included in the Windows package as
  `licenses/Wintun-LICENSE.txt`. The file is downloaded from wintun.net during the build and verified with SHA256
  ([`cmake/Wintun.cmake`](cmake/Wintun.cmake)).
- The API header [`third_party/wintun/wintun.h`](third_party/wintun/wintun.h) is dual-licensed
  "GPL-2.0 OR MIT"; KettleLAN uses it under the **MIT** license.

## Windows package

- Qt 6 libraries, QML modules and plugins (unmodified official binaries, LGPL-3.0 – source code:
  https://download.qt.io/archive/qt/6.8/6.8.3/single/). They are separate DLL files and can be replaced.
- The Microsoft Visual C++ runtime DLLs, redistributed as permitted by Microsoft for applications built with
  Visual Studio, and other Microsoft redistributable files added by Qt's `windeployqt` tool.
- The full list of bundled files is in `licenses/bundled-files.txt` inside the package.

## Other libraries bundled in the AppImage

To run on many distributions, the AppImage includes some system libraries needed by Qt (for example
XCB, xkbcommon, Wayland, ICU, double-conversion, PCRE2, zstd). The exact list depends on the build and is
generated automatically while building the AppImage:

- `usr/share/doc/kettlelan/licenses/bundled-libraries.txt` – every bundled library and where it comes from,
- `usr/share/doc/kettlelan/licenses/*.copyright` – the copyright and license file of each source package.

View them with `./KettleLAN-*.AppImage --appimage-extract` and open `squashfs-root/usr/share/doc/kettlelan/`.

## Algorithms and tools

- **SipHash-2-4** (Jean-Philippe Aumasson, Daniel J. Bernstein) – KettleLAN contains its own implementation
  of this public algorithm (`src/core/siphash.cpp`).
- **Build tools** (CMake, GCC, linuxdeploy, GitHub Actions) are used only to build KettleLAN and are not
  shipped with it.

## Trademarks

Steam is a trademark of Valve Corporation. Quake is a trademark of id Software / ZeniMax Media.
All other game and product names are trademarks of their respective owners and are used only to describe
compatibility.

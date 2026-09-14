# Third-party components and references

The original application code is provided under GPL-3.0-or-later. Static linking does not remove third-party license obligations. Accompany binary redistribution with the provided corresponding source archive or another license-compliant source distribution.

| Component | Version / reference | License | Integration |
|---|---|---|---|
| OpenConnect | 9.21 | LGPL-2.1; see upstream COPYING.LGPL and source notices | Static library; upstream protocol implementation |
| OpenSSL | 3.5.8 | Apache-2.0 | Static TLS/crypto libraries |
| libxml2 | 2.15.3 | MIT and notices in Copyright | Static XML parsing |
| zlib | 1.3.2 | zlib license | Static compression |
| MinGW-w64 runtime | w64devkit 2.10.0 locally; Ubuntu MSVCRT MinGW in Linux builds | Multiple permissive notices | Static Windows runtime |
| GCC runtime | Version supplied by the selected build toolchain | GPL with GCC Runtime Library Exception | Static compiler/C++ runtime; see packaged toolchain notices |
| Wintun prebuilt binaries | 0.14.1 | Wintun Prebuilt Binaries License | Official, unmodified architecture-specific wintun.dll, used through its permitted API |
| OpenWrt ocserv recipe | c7a47d583127961590dfd832c424441c6810952c | GPL-2.0-or-later | SDK recipe and procd/UCI integration under server/openwrt |
| ocserv | 1.5.0 | GPL-2.0-or-later | Server; upstream source included in server build artifacts |
| LuCI ocserv application / translations | SDK-selected feed revision in BUILDINFO.json | See the upstream LuCI source notices | Existing management page and translations; corresponding feed source bundled |

OpenConnect GUI (https://gitlab.com/openconnect/openconnect-gui) was consulted for its callback-based connection lifecycle. The UI, credential store, import format, native network configuration, tray handling and icon here were implemented separately; Qt GUI code is not included or linked.

Wintun DLLs are obtained from https://www.wintun.net/builds/wintun-0.14.1.zip; the ZIP SHA-256 is `07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51`. The provided DLLs must stay unmodified. Including the driver's prebuilt license and its name does not imply endorsement by WireGuard or OpenConnect.

Public Windows SDK WFP identifier values omitted by MinGW headers were checked against Microsoft's win32metadata `RecompiledIdlHeaders/um/fwpmu.h` and defined in `src/wfp_constants.h`.

Copies of component licenses are supplied in the release `licenses` directory. Build tools such as MSYS2, CMake, Python/Pillow and 7-Zip are not runtime dependencies and are not included in the application package.

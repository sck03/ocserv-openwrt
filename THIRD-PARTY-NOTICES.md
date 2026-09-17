# Third-party components and references

The application is GPL-3.0-or-later. Static linking does not remove third-party license obligations. Binary redistribution must include license-compliant corresponding source; the matching archive includes the client, build recipes, patches and pinned upstream archives.

| Component | Version / reference | License | Integration |
|---|---|---|---|
| OpenConnect | 9.21 | LGPL-2.1; see source notices | Static official protocol library with documented Windows script error fixes |
| GnuTLS | 3.8.13 | LGPL-2.1-or-later; accompanying source notices | Static TLS, certificate and Windows system-key support |
| GMP | 6.3.0 | LGPL-3.0-or-later / GPL-2.0-or-later | Static arithmetic |
| Nettle / Hogweed | 3.10.2 | LGPL-3.0-or-later / GPL-2.0-or-later | Static cryptography |
| stoken | 0.92 | LGPL-2.1-or-later | Static RSA software-token support |
| libxml2 | 2.15.3 | MIT and notices in Copyright | Static XML parser |
| zlib | 1.3.2 | zlib license | Static compression |
| nlohmann/json | 3.12.0 | MIT | Vendored single-header JSON parser |
| OpenConnect GUI | v1.6.2, c89e5dc00a2b24e5b1c84855b91bcdda3aee4d86 | GPL-2.0-or-later | Profile, dialog and connection lifecycle adaptations |
| vpnc-scripts | ce9e961bd0f6b867e1c7c35f78f6fb973f6ff101 | GPL-2.0; see upstream COPYING and notices | Windows routing script with logging and error-handling changes |
| MinGW-w64 runtime | w64devkit 2.10.0 locally; Ubuntu MSVCRT MinGW in CI | Multiple permissive notices | Static Windows runtime |
| GCC runtime | Selected toolchain version | GPL with GCC Runtime Library Exception | Static C/C++ runtime |
| Wintun prebuilt binaries | 0.14.1 | Wintun Prebuilt Binaries License | Official, unmodified architecture-specific DLL |
| OpenWrt ocserv recipe | c7a47d583127961590dfd832c424441c6810952c | GPL-2.0-or-later | SDK recipe and procd/UCI integration |
| ocserv | 1.5.0 | GPL-2.0-or-later | Server; source included in server artifacts |
| LuCI reference source | SDK-selected feed revision in BUILDINFO.json | Upstream LuCI notices | Feed source bundled; the shipped page is luci-app-ocserv-easy |

The UI uses the Windows API in C++17. Qt is not linked or distributed. The OpenConnect library owns VPN protocol and tunnel behavior; the upstream Windows vpnc script configures addresses, routes and DNS. See [client/UPSTREAM.md](client/UPSTREAM.md) for references and local adaptations.

Wintun comes from https://www.wintun.net/builds/wintun-0.14.1.zip, SHA-256 07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51. Supplied DLLs stay unmodified. Attribution does not imply endorsement by WireGuard or OpenConnect.

Complete licenses accompany the portable package in licenses/. MSYS2, CMake, Python, cryptography, Node.js, Pillow and 7-Zip are development/test tools, not application runtime dependencies.

# ocserv 1.5.0 package recipe

The `ocserv/` directory is the OpenWrt packages feed recipe, including its UCI/procd integration.

- Source: https://github.com/openwrt/packages/tree/c7a47d583127961590dfd832c424441c6810952c/net/ocserv
- Retrieved: 2026-09-14
- Upstream package: ocserv 1.5.0-r1; local package release: 1.5.0-r3.
- Source archive SHA-256: `42ced08958b9576ab134fcb7bdc7f8df5e13214fd147855f99021fedcf0eedbe`
- License: GPL-2.0-or-later (upstream copyright notices retained).
- Build system: Meson. The maintained target is OpenWrt 25.12 APK on armsr/armv8.

Local changes provide first-install certificate initialization, current VPN defaults, and explicit use of the firmware's existing TUN support. The APK does not depend on a stock `kmod-tun`; the installer checks `/dev/net/tun` before proceeding. Installation and configuration are documented in [OPENWRT-N1.md](../../docs/OPENWRT-N1.md).

The userspace package must match the firmware's libc and library ABI. SDK compilation does not validate installation or VPN traffic on the user's OPL v0.0.8 / `6.12.66-flippy-94+` N1.

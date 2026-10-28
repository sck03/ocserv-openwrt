# ocserv 1.6.0 package recipe

The `ocserv/` directory is the OpenWrt packages feed recipe, including its UCI/procd integration.

- Source: https://github.com/openwrt/packages/tree/c7a47d583127961590dfd832c424441c6810952c/net/ocserv
- Retrieved: 2026-09-14
- Original feed recipe: ocserv 1.5.0-r1; maintained source and minimum version: 1.6.0-r1 (verified 2026-10-28).
- Official source: https://www.infradead.org/ocserv/download/ocserv-1.6.0.tar.xz
- Source archive SHA-256: `2d3303a0d9e06eabfa08694b9dca3c558fef36418857bebf1b8de57ece5f9f32`
- License: GPL-2.0-or-later (upstream copyright notices retained).
- Build system: Meson. Maintained OpenWrt 25.12 APK targets are listed in [targets.json](targets.json), including armsr/armv8 for N1.

Local changes provide first-install certificate initialization, current VPN defaults, and explicit use of the firmware's existing TUN support. The APK does not depend on a stock `kmod-tun`; the installer checks `/dev/net/tun` before proceeding. Installation and configuration are documented in [OPENWRT-N1.md](../../docs/OPENWRT-N1.md).

The userspace package must match the firmware's libc and library ABI. SDK compilation does not validate installation or VPN traffic on the user's OPL v0.0.8 / `6.12.66-flippy-94+` N1.

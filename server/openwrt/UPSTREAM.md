# ocserv 1.5.0 package recipe

The `ocserv/` directory is the OpenWrt packages feed recipe, including its UCI/procd integration.

- Source: https://github.com/openwrt/packages/tree/c7a47d583127961590dfd832c424441c6810952c/net/ocserv
- Retrieved: 2026-09-14
- Package: ocserv 1.5.0-r1
- Source archive SHA-256: `42ced08958b9576ab134fcb7bdc7f8df5e13214fd147855f99021fedcf0eedbe`
- License: GPL-2.0-or-later (upstream copyright notices retained).
- Build system: Meson; this is not the older 24.10 Autotools recipe with a changed version number.

This recipe is provided for a matching OpenWrt SDK. It has **not** been compiled or installed on the user's OPL N1 firmware in this task. A userspace package must match that firmware's libc and library ABI. Its `kmod-tun` dependency must be satisfied by the firmware's own kernel/module packages. Do not install a stock OpenWrt kernel module into the `6.6.102-flippy-93+` kernel.

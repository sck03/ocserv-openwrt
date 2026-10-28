include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-ocserv-easy
PKG_VERSION:=0.6.4
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-or-later

LUCI_TITLE:=Simple ocserv administration for Lansyra
LUCI_DEPENDS:=+ocserv +luci-compat +luci-lib-nixio +luci-lib-jsonc +libuci-lua
LUCI_PKGARCH:=all
# SDK CSSTidy 1.3 drops modern grid, gap and inset declarations.
LUCI_MINIFY_CSS:=0
LUCI_MINIFY_LUA:=0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature

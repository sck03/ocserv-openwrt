#!/bin/sh
# Install the verified 25.12 APK bundle on a Phicomm N1; OpenClash is opt-in.
set -eu
umask 077
cd "$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
[ "$(id -u)" = 0 ] || { echo '请以 root 运行此脚本。'; exit 1; }
[ -f SHA256SUMS ] || { echo '请完整解压服务端套装后运行 install.sh。'; exit 1; }
sha256sum -c SHA256SUMS
sh ./preflight-n1.sh
case "$(apk --print-arch)" in
    aarch64|aarch64_generic) ;;
    *) echo '此套装仅适用于 N1 的 aarch64 / aarch64_generic 用户空间。'; exit 1 ;;
esac

fresh=0
pending=/etc/ocserv/easy-install-pending
if [ -f "$pending" ] || { ! apk info -e ocserv >/dev/null 2>&1 && [ ! -e /etc/config/ocserv ]; }; then fresh=1; fi
# An APK-only installation skipped this bundle's first-run network setup.
# Repair that known default configuration, while retaining customized networks.
if [ "$fresh" = 0 ] &&
    [ "$(uci -q get ocserv.config.zone)" = ocvpn ] &&
    [ "$(uci -q get ocserv.config.ipaddr)" = 10.77.0.0 ] &&
    [ "$(uci -q get ocserv.config.netmask)" = 255.255.255.0 ] &&
    [ "$(uci -q get ocserv.config.port)" = 4443 ] &&
    [ "$(uci -q get ocserv.config.proxy_arp)" != 1 ] &&
    ! uci -q show firewall | grep -Eq '(vpns|ocvpn|ocserv)'; then
    fresh=1
    echo '检测到此前只安装了 APK，正在补齐默认 VPN 的网络设置。'
fi
set -- ./*.apk
[ "$#" -eq 2 ] || { echo '修复版套装应只包含 ocserv 和 luci-app-ocserv-easy 两个 APK。请解压到新目录，不要混入旧包。'; exit 1; }
for package do [ -f "$package" ] || { echo "缺少安装包：$package"; exit 1; }; done
case "$1 $2" in ./luci-app-ocserv-easy-*.apk\ ./ocserv-*.apk) ;; *) echo '安装包名称与修复版套装不符。'; exit 1;; esac
if [ "$fresh" = 1 ]; then
    [ -z "$(uci -q changes firewall)" ] || { echo '其他页面有未应用的防火墙修改，请先处理后重跑安装。'; exit 1; }
    for id in ocserv_vpn ocserv_vpn_to_lan ocserv_vpn_nat ocserv_vpn_entry; do
        [ -z "$(uci -q get "firewall.$id")" ] || { echo "已存在 firewall.$id，请在 LuCI 检查 VPN 网络配置。"; exit 1; }
    done
fi

plan=$(mktemp /tmp/bulijie-apk-plan.XXXXXX)
trap 'rm -f "$plan"' EXIT HUP INT TERM
apk update
apk add --simulate --allow-untrusted "$@" >"$plan" 2>&1 || { cat "$plan"; exit 1; }
if grep -Ei '(installing|upgrading|downgrading|replacing).*(kernel|kmod-)' "$plan" >/dev/null; then
    cat "$plan"
    echo '安装计划涉及内核模块，已停止。请检查 OPL/Flippy 软件源。'
    exit 1
fi
backup="/root/bulijie-before-install-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -m 700 "$backup"
for name in ocserv firewall dhcp openclash; do
    [ ! -f "/etc/config/$name" ] || cp -p "/etc/config/$name" "$backup/$name.uci"
done
[ ! -d /etc/ocserv ] || cp -pR /etc/ocserv "$backup/ocserv"
if [ "$fresh" = 1 ]; then
    mkdir -p /etc/ocserv
    printf '%s\n' 'Fresh installation awaiting VPN network setup' > "$pending"
fi
apk add --allow-untrusted "$@"
/usr/libexec/ocserv-easy-repair-users
# The maintained page now depends on ocserv directly. Retire the upstream
# duplicate editors only after both replacement packages installed successfully.
legacy=''
for package in luci-i18n-ocserv-zh-cn luci-app-ocserv; do
    if apk info -e "$package" >/dev/null 2>&1; then legacy="$legacy $package"; fi
done
if [ -n "$legacy" ]; then
    # Deliberate word splitting of fixed package names, never user input.
    apk del $legacy
fi
/etc/init.d/ocserv initcerts
/etc/init.d/ocserv-easy-guard enable

if [ "$fresh" = 1 ]; then
    # Baseline VPN networking only: no OpenClash or DNS service settings change.
    [ -z "$(uci -q changes firewall)" ] || { echo '其他页面有未应用的防火墙修改，请先处理后重跑安装。'; exit 1; }
    rollback_network() {
        uci -q revert firewall || true
        cp -p "$backup/firewall.uci" /etc/config/firewall
        /etc/init.d/firewall reload || true
        echo "VPN 防火墙设置失败，已恢复原文件。备份：$backup"
    }
    if ! uci batch <<'UCI'
set firewall.ocserv_vpn=zone
set firewall.ocserv_vpn.name='ocvpn'
add_list firewall.ocserv_vpn.device='vpns+'
set firewall.ocserv_vpn.input='ACCEPT'
set firewall.ocserv_vpn.output='ACCEPT'
set firewall.ocserv_vpn.forward='REJECT'
set firewall.ocserv_vpn.mtu_fix='1'
set firewall.ocserv_vpn_to_lan=forwarding
set firewall.ocserv_vpn_to_lan.src='ocvpn'
set firewall.ocserv_vpn_to_lan.dest='lan'
set firewall.ocserv_vpn_nat=nat
set firewall.ocserv_vpn_nat.name='ocserv VPN source NAT'
set firewall.ocserv_vpn_nat.src='lan'
set firewall.ocserv_vpn_nat.src_ip='10.77.0.0/24'
set firewall.ocserv_vpn_nat.proto='all'
set firewall.ocserv_vpn_nat.family='ipv4'
set firewall.ocserv_vpn_nat.target='MASQUERADE'
set firewall.ocserv_vpn_entry=rule
set firewall.ocserv_vpn_entry.name='Allow ocserv from LAN'
set firewall.ocserv_vpn_entry.src='lan'
set firewall.ocserv_vpn_entry.proto='tcp udp'
set firewall.ocserv_vpn_entry.dest_port='4443'
set firewall.ocserv_vpn_entry.target='ACCEPT'
commit firewall
UCI
    then
        rollback_network
        exit 1
    fi
    if ! fw4 check || ! /etc/init.d/firewall reload; then
        rollback_network
        exit 1
    fi
    rm -f "$pending"
fi
/etc/init.d/rpcd restart
# Invalidate both older Lua and current ucode dispatcher caches.
rm -f /tmp/luci-indexcache* /tmp/luci-modulecache/*
echo "安装完成。安装前备份：$backup"
echo '请重新登录 LuCI → VPN → 布利杰VPN（管理页 0.4.1）。'
echo '全新安装：添加账号，检查服务设置，然后点击「启动服务」。'
echo '默认端口 4443，VPN 地址池 10.77.0.0/24；专用 OpenClash 开关默认关闭。'

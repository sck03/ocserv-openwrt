#!/bin/sh
# Install the verified OpenWrt 25.12 APK bundle; OpenClash is opt-in.
set -eu
umask 077
cd "$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
[ "$(id -u)" = 0 ] || { echo '请以 root 运行此脚本。'; exit 1; }
set -- ./*.apk
[ "$#" -eq 2 ] || { echo '修复版套装应只包含 ocserv 和 luci-app-ocserv-easy 两个 APK。请解压到新目录，不要混入旧包。'; exit 1; }
for package do [ -f "$package" ] || { echo "缺少安装包：$package"; exit 1; }; done
case "$1 $2" in ./luci-app-ocserv-easy-*.apk\ ./ocserv-*.apk) ;; *) echo '安装包名称与修复版套装不符。'; exit 1;; esac
if [ "${OCSERV_EASY_LOCK_OWNER:-}" != "$$" ]; then
    [ -f SHA256SUMS ] || { echo '请完整解压服务端套装后运行 install.sh。'; exit 1; }
    sha256sum -c SHA256SUMS
    sh ./preflight.sh
    [ -s APK-ARCHITECTURES ] || { echo '缺少套装架构清单，请重新下载完整 ZIP。'; exit 1; }
    device_arch=$(sh ./preflight.sh --architecture)
    grep -Fqx -- "$device_arch" APK-ARCHITECTURES || {
        recommended_arch=$device_arch
        [ "$recommended_arch" != aarch64 ] || recommended_arch=aarch64_generic
        echo "套装与固件软件包架构不匹配：$device_arch。请选择 openwrt-25.12-$recommended_arch 的 ZIP。"
        echo '架构以 /etc/apk/arch 为准，不能仅按 Cortex-A53 等 CPU 型号选择。'
        exit 1
    }
    [ -f with-lock.lua ] && [ -f lease.lua ] || { echo '缺少维护锁模块，请重新解压完整套装。' >&2; exit 1; }
    # Bootstrap only the lock runtime on a fresh LuCI installation. No VPN
    # configuration is read or changed until the common record lock is held.
    if ! lua -e 'require "nixio"; require "nixio.fs"' >/dev/null 2>&1; then
        echo '正在安装 LuCI 维护锁所需的 Lua/nixio 运行库。'
        (
            lock_plan=$(mktemp /tmp/lansyra-lock-plan.XXXXXX)
            trap 'rm -f "$lock_plan"' EXIT
            trap 'exit 129' HUP
            trap 'exit 130' INT
            trap 'exit 143' TERM
            apk update
            apk add --simulate luci-lib-nixio >"$lock_plan" 2>&1 || { cat "$lock_plan"; exit 1; }
            if grep -Ei '(installing|upgrading|downgrading|replacing).*(kernel|kmod-)' "$lock_plan" >/dev/null; then
                cat "$lock_plan"
                echo '维护锁运行库的安装计划涉及内核模块，已停止。请检查当前固件的软件源。' >&2
                exit 1
            fi
            apk add luci-lib-nixio
        )
        lua -e 'require "nixio"; require "nixio.fs"' || { echo 'Lua/nixio 运行库不可用，已停止安装。' >&2; exit 1; }
    fi
    # exec preserves the current PID and the POSIX lock descriptor. The lock
    # remains held throughout recovery, package hooks, service work and cleanup.
    exec lua ./with-lock.lua /bin/sh ./install.sh
fi
[ ! -f /etc/ocserv/easy-transaction/state.json ] || {
    echo '存在待恢复的管理页事务，请等待后台恢复完成，或先在 LuCI 处理恢复错误后再安装。' >&2
    exit 1
}
if [ -f /etc/ocserv/easy-guard/state.json ]; then
    # A confirmed guard can survive an upgrade. Never interrupt its activation,
    # confirmation deadline, rollback, or an invalid recovery record.
    if ! lua -e 'local f=assert(io.open("/etc/ocserv/easy-guard/state.json","rb")); local raw=f:read(2097153); f:close(); assert(raw and #raw<=2097152); local s=require("luci.jsonc").parse(raw); assert(type(s)=="table" and s.version==1 and s.phase=="enabled" and type(s.ops)=="table" and type(s.info)=="table")' >/dev/null 2>&1; then
        echo 'VPN 专用上网尚在应用、待确认或恢复阶段；请先在 LuCI 完成或恢复该操作后再安装。' >&2
        exit 1
    fi
fi

pending=/etc/ocserv/easy-install-pending
network_journal=/etc/ocserv/easy-install-network
firewall_new=/etc/config/.firewall-lansyra-$$
recover_network() {
    [ -d "$network_journal" ] || return 0
    if [ -f "$network_journal/complete" ]; then
        # The service and firewall checks completed before interruption.
        rm -f "$pending"
    elif [ -f "$network_journal/before" ]; then
        if cmp -s /etc/config/firewall "$network_journal/before"; then :
        elif [ -f "$network_journal/prepared" ] && cmp -s /etc/config/firewall "$network_journal/firewall"; then
            cp -p "$network_journal/before" "$firewall_new" && mv -f "$firewall_new" /etc/config/firewall || return 1
        else
            echo "防火墙在安装中断后有其他修改，未覆盖。请核对恢复记录：$network_journal" >&2
            return 1
        fi
        if [ -f "$network_journal/prepared" ]; then /etc/init.d/firewall reload || return 1; fi
    fi
    rm -f "$firewall_new"
    rm -rf -- "$network_journal"
}
# Recover a known interrupted first installation before deciding whether its
# managed section names conflict. Unknown later edits are preserved for review.
recover_network || exit 1
fresh=0
if [ -f "$pending" ] || { ! apk info -e ocserv >/dev/null 2>&1 && [ ! -e /etc/config/ocserv ]; }; then fresh=1; fi
service_was_running=0
service_stopped=0
service_restored=0
service_boot=disable
if [ "$fresh" = 0 ]; then
    echo '检测到现有 Lansyra 服务；将原地升级并保留账号、证书和网络配置。'
    if /etc/init.d/ocserv status >/dev/null 2>&1; then service_was_running=1; fi
    if /etc/init.d/ocserv enabled >/dev/null 2>&1; then service_boot=enable; fi
else
    echo '未检测到已完成的 Lansyra 安装；将执行首次网络初始化。'
fi
if [ "$fresh" = 1 ]; then
    [ -z "$(uci -q changes firewall)" ] || { echo '其他页面有未应用的防火墙修改，请先处理后重跑安装。'; exit 1; }
    for id in ocserv_vpn ocserv_vpn_to_lan ocserv_vpn_nat ocserv_vpn_entry; do
        [ -z "$(uci -q get "firewall.$id")" ] || { echo "已存在 firewall.$id，请在 LuCI 检查 VPN 网络配置。"; exit 1; }
    done
fi

plan=$(mktemp /tmp/lansyra-apk-plan.XXXXXX)
wait_service() {
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        actual=0
        if /etc/init.d/ocserv status >/dev/null 2>&1; then actual=1; fi
        [ "$actual" = "$1" ] && return 0
        sleep 1
    done
    return 1
}
cleanup() {
    status=$?
    if [ -d "$network_journal" ] && ! recover_network; then
        echo "首次网络设置尚需恢复。请处理 $network_journal 后重跑安装。" >&2
        status=1
    fi
    if [ "$service_stopped" = 1 ] && [ "$service_restored" = 0 ]; then
        if /etc/init.d/ocserv start >/dev/null 2>&1 && wait_service 1; then
            echo '升级中途退出，已尝试恢复当前安装的 ocserv 服务；APK 版本未回滚。'
        else
            echo '升级中途退出，恢复 ocserv 服务失败；请检查 logread -e ocserv。' >&2
        fi
    fi
    rm -f "$plan"
    trap - EXIT HUP INT TERM
    exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
apk update
apk add --simulate --upgrade --allow-untrusted "$@" >"$plan" 2>&1 || { cat "$plan"; exit 1; }
if grep -Ei '(installing|upgrading|downgrading|replacing).*(kernel|kmod-)' "$plan" >/dev/null; then
    cat "$plan"
    echo '安装计划涉及内核模块，已停止。请检查当前固件的软件源。'
    exit 1
fi
backup="/root/lansyra-before-install-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -m 700 "$backup"
for name in ocserv firewall dhcp openclash nikki momo; do
    [ ! -f "/etc/config/$name" ] || cp -p "/etc/config/$name" "$backup/$name.uci"
done
[ ! -d /etc/ocserv ] || cp -pR /etc/ocserv "$backup/ocserv"
if [ "$fresh" = 1 ]; then
    mkdir -p /etc/ocserv
    printf '%s\n' 'Fresh installation awaiting VPN network setup' > "$pending"
fi
if [ "$service_was_running" = 1 ]; then
    echo '正在暂时停止 ocserv 以替换运行文件。'
    service_stopped=1
    /etc/init.d/ocserv stop
    wait_service 0 || { echo 'ocserv 未停止，已中止升级。' >&2; exit 1; }
fi
apk add --upgrade --allow-untrusted "$@"
if [ "$fresh" = 0 ]; then /etc/init.d/ocserv "$service_boot"; fi
# Package post-install hooks may start an enabled service. Keep a previously
# stopped service stopped, including on the first installation.
if [ "$service_was_running" = 0 ] && /etc/init.d/ocserv status >/dev/null 2>&1; then
    /etc/init.d/ocserv stop
    wait_service 0 || { echo '无法恢复 ocserv 停止状态。' >&2; exit 1; }
fi
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
echo '正在初始化 ocserv 服务端证书；局域网部署会生成本地证书。'
if ! /etc/init.d/ocserv initcerts; then
    echo '证书初始化失败，安装尚未完成。修正后重新运行 install.sh；可用 logread -e ocserv 查看原因。'
    exit 1
fi
/etc/init.d/ocserv-easy-guard enable
/etc/init.d/ocserv-easy-guard restart

if [ "$fresh" = 1 ]; then
    # Baseline VPN networking only: no OpenClash or DNS service settings change.
    [ -z "$(uci -q changes firewall)" ] || { echo '其他页面有未应用的防火墙修改，请先处理后重跑安装。'; exit 1; }
    prefix_octet() {
        case "$1" in
            0) printf '0' ;; 128) printf '1' ;; 192) printf '2' ;; 224) printf '3' ;;
            240) printf '4' ;; 248) printf '5' ;; 252) printf '6' ;; 254) printf '7' ;;
            255) printf '8' ;; *) return 1 ;;
        esac
    }
    mask_to_prefix() {
        local mask="$1" old_ifs="$IFS" part bits total=0 count=0
        IFS=.
        for part in $mask; do
            bits=$(prefix_octet "$part") || { IFS="$old_ifs"; return 1; }
            total=$((total + bits)); count=$((count + 1))
        done
        IFS="$old_ifs"
        [ "$count" = 4 ] || return 1
        printf '%s' "$total"
    }
    vpn_ip=$(uci -q get ocserv.config.ipaddr 2>/dev/null || true)
    vpn_mask=$(uci -q get ocserv.config.netmask 2>/dev/null || true)
    vpn_port=$(uci -q get ocserv.config.port 2>/dev/null || true)
    vpn_udp=$(uci -q get ocserv.config.udp_port 2>/dev/null || true)
    case "$vpn_ip" in ''|*[!0-9.]*) vpn_ip=10.77.0.0 ;; esac
    case "$vpn_mask" in ''|*[!0-9.]*) vpn_mask=255.255.255.0 ;; esac
    vpn_prefix=$(mask_to_prefix "$vpn_mask" 2>/dev/null || printf '24')
    case "$vpn_port" in ''|*[!0-9]*) vpn_port=4443 ;; esac
    case "$vpn_udp" in ''|*[!0-9]*) vpn_udp="$vpn_port" ;; esac
    vpn_ports="$vpn_port"
    [ "$vpn_udp" = "$vpn_port" ] || vpn_ports="$vpn_ports $vpn_udp"
    rollback_network() {
        if recover_network; then echo "VPN 防火墙设置失败，已恢复原文件。备份：$backup"
        else echo "VPN 防火墙恢复尚未完成，记录：$network_journal" >&2; fi
    }
    mkdir -m 700 "$network_journal"
    mkdir -m 700 "$network_journal/delta"
    cp -p /etc/config/firewall "$network_journal/before"
    cp -p /etc/config/firewall "$network_journal/firewall"
    # Render the candidate in an isolated UCI directory. Journal both versions
    # before replacement so process termination/power loss is recoverable.
    if ! uci -c "$network_journal" -t "$network_journal/delta" batch <<UCI
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
set firewall.ocserv_vpn_nat.src_ip='$vpn_ip/$vpn_prefix'
set firewall.ocserv_vpn_nat.proto='all'
set firewall.ocserv_vpn_nat.family='ipv4'
set firewall.ocserv_vpn_nat.target='MASQUERADE'
set firewall.ocserv_vpn_entry=rule
set firewall.ocserv_vpn_entry.name='Allow ocserv from LAN'
set firewall.ocserv_vpn_entry.src='lan'
set firewall.ocserv_vpn_entry.proto='tcp udp'
set firewall.ocserv_vpn_entry.dest_port='$vpn_ports'
set firewall.ocserv_vpn_entry.target='ACCEPT'
commit firewall
UCI
    then
        rollback_network
        exit 1
    fi
    : > "$network_journal/prepared"
    sync
    cp -p "$network_journal/firewall" "$firewall_new"
    mv -f "$firewall_new" /etc/config/firewall
    if ! fw4 check || ! /etc/init.d/firewall reload; then
        rollback_network
        exit 1
    fi
    : > "$network_journal/complete"
    sync
    rm -f "$pending"
    recover_network
fi
if [ "$service_was_running" = 1 ]; then
    echo '正在恢复原有 ocserv 服务。'
    /etc/init.d/ocserv start
    wait_service 1 || { echo 'ocserv 未启动，请检查 logread -e ocserv。' >&2; exit 1; }
    service_restored=1
fi
/etc/init.d/rpcd restart
# Invalidate both older Lua and current ucode dispatcher caches.
rm -f /tmp/luci-indexcache* /tmp/luci-modulecache/*
echo "安装完成。安装前备份：$backup"
echo '请重新登录 LuCI → VPN → 岚序 Lansyra（管理页 0.6.0-r1）。'
echo '全新安装：添加账号，检查服务设置，然后点击「启动服务」。'
echo '默认端口 4443，VPN 地址池 10.77.0.0/24；VPN 专用代理开关默认关闭。'

# N1 上的 ocserv 与固件升级

核实日期：2026-09-14。

## 当前设备

| 项目 | 用户提供的值 |
|---|---|
| 设备 | Phicomm N1 / `phicomm,n1` |
| 固件 | OPL_FW4 For N1 v0.0.7 |
| OpenWrt | 24.10-SNAPSHOT，`r28816-a65ca44cb7` |
| 内核 | `6.6.102-flippy-93+` |
| target | `armsr/armv8` |
| 软件包架构 | `aarch64_generic` |
| LAN IP | `192.168.19.254` |
| 包管理器 | opkg |

这是一套 N1 定制固件，不能把所有标记为 ARMv8 的固件、软件包和内核模块都视为可互换。

## 能不能安装 1.5.0

可以做匹配固件的移植/重新打包。`1.3.0-r2` 表示上游 1.3.0、OpenWrt 打包修订 2；不是“1.30-r2”。`1.5.0` 也不是“1.50”。截至核实日期，源代码配方为：

| OpenWrt packages 分支 | ocserv 配方版本 | 构建方式 |
|---|---|---|
| openwrt-24.10 | 1.3.0-r2 | Autotools |
| openwrt-25.12 | 1.4.1-r2 | Autotools |
| master | 1.5.0-r1 | Meson |

软件源跟随发行分支维护，不会因为上游出新版就自动变成最新。ocserv 自 1.4.2 起转换为 Meson，因此需要移植完整配方、安装路径和补丁，不只是修改版本号。1.5.0 在 2026-06-07 发布，含未认证 cookie 缓冲区问题、DTLS MTU 检查和连接稳定性等修复；旧发行包是否回移特定修复要单独核对。

项目提供 `server/openwrt/ocserv` 的完整 1.5.0 配方及 `server/tools/build-ocserv.sh`。GitHub Actions 在 Linux 中分别使用官方 **24.10.8 / 25.12.5 的 armsr/armv8 SDK** 构建；产物的 `BUILDINFO.json` 记录 SDK、校验值和 feed 提交。**尚未在这套 OPL 固件上实际安装与转发验收**，官方 SDK 包需要先通过下面的设备检查。

优先使用固件作者提供的对应 SDK/软件源。若尝试官方 24.10 的 armsr/armv8 SDK，应先比对 libc、GnuTLS、libev 等 ABI、依赖和实际运行结果。官方 SDK 的架构匹配不等于已验证兼容这套定制固件。不要混用 master 的软件源或强制忽略依赖。

## 当前 24.10 升级与安装管理页

1. 在 GitHub Actions 手动构建 `all` 或 `server`，下载 `openwrt-24.10-aarch64_generic`，完整解压。
2. 在电脑校验 `SHA256SUMS`，把包和工具上传到 N1 的临时目录。
3. 在路由器运行 `sh preflight-n1.sh`。它逐项读取固件、库版本、TUN 和磁盘信息，不输出密码或私钥。
4. 从可信的、与当前固件匹配的软件源保留**当前已安装的 1.3.0-r2 原包**用于回滚，并准备其可信 SHA-256 记录。升级脚本要求新旧包各自目录均有包含对应文件名的 `SHA256SUMS`；若在同一目录，将旧包校验记录追加进去。
5. 将下面 `NEW.ipk` / `OLD.ipk` 替换为实际文件名：

```sh
sh upgrade-ocserv-24.10.sh --check NEW.ipk OLD.ipk
sh upgrade-ocserv-24.10.sh --apply NEW.ipk OLD.ipk
opkg install ./luci-app-ocserv_*.ipk ./luci-i18n-ocserv-zh-cn_*.ipk ./luci-app-ocserv-easy_*.ipk
```

`--check` 核对包名、架构、版本与校验值，先用新二进制在当前设备执行版本与现有配置检查，并拒绝安装或替换内核模块的计划。`--apply` 会短暂断开 VPN，备份现有账号、证书、配置和旧包，安装后验证服务；失败自动尝试恢复。备份目录是 `/root/ocserv-upgrade-日期时间`。

安装页面后重新登录 LuCI，打开 **VPN → OpenConnect VPN → 布利杰VPN**。具体功能见 [管理页说明](SERVER-UI.md)。页面要求 1.5.0，不保留旧版会话接口的兼容分支。

## 以后升级 OpenWrt 25.12

ocserv 是用户态程序，通过标准 TUN 接口与内核交互，通常不需要因 Linux 6.6 升到 6.12 而更换应用协议。因此 **1.5.0 可以继续作为 25.12 的移植版本，但要按新固件重新打包、安装和验证**。

官方 25.12 默认改用 APK，25.12.0 的 armsr/armv8 清单使用 Linux 6.12.71。24.10 的 `.ipk` 不能当作 25.12 的 `.apk` 直接安装；OPL 作者也可能定制包管理方式，以实际固件为准。

- 新内核的 TUN 支持或 `kmod-tun` 必须来自匹配的新固件。绝不能把官方模块塞入 `6.6.102-flippy-93+`，也不能把旧模块带到新内核。
- N1 要用明确支持 N1 的固件。官方 armsr/armv8 通用 EFI 镜像不是 N1 可直接刷写的设备镜像。
- 升级前备份 `/etc/config/ocserv`、`/etc/ocserv`、防火墙、DNS 和 OpenClash 配置；备份含账号哈希和私钥，应限制访问。
- 升级后安装匹配的 ocserv/依赖，恢复配置，确认生成配置、监听端口、TUN、DNS 和代理转发，再放行用户。
- 客户端不依赖服务端包管理器或 Linux 内核号。服务器地址/端口、CA 与认证方式保持一致时，客户端通常不需要改；更换 CA/固定公钥时重新分发连接文件。

升级 N1 固件并恢复配置后，改用 `openwrt-25.12-aarch64_generic` 中的 APK。先运行 `preflight-n1.sh`，确认 `aarch64_generic`、APK 与 TUN；校验 `SHA256SUMS` 后安装本次下载的四个包：

```sh
apk add --allow-untrusted ./ocserv-1.5.0-*.apk ./luci-app-ocserv-*.apk ./luci-i18n-ocserv-zh-cn-*.apk
```

`luci-app-ocserv-*.apk` 同时包含原页面和 `luci-app-ocserv-easy`。`--allow-untrusted` 用于这里自行构建并核验的本地包，因为它们没有固件官方软件源的签名。普通依赖继续从当前固件匹配的软件源安装。25.12 不使用 24.10 的自动回滚脚本；保留新固件及其匹配的旧包和配置备份，确认版本、服务状态与真实客户端通信后再开放用户。

## 本项目的服务配置示例

`server/examples/ocserv.uci` 使用 TCP/UDP 4443、VPN 地址池 10.77.0.0/24、`max_same=1`。请确认这个地址池不与现网冲突，LAN 掩码示例按 /24 写出，需按实际情况核对。

`max-same-clients=1` 由服务端按认证账号实施。同一账号的新连接超过限制会被拒绝，已有连接保留；断网后的席位释放需要等待服务端检测。管理员停用账号时还应使用 1.5.0 的 `occtl terminate user <用户名>` 终止并使会话失效。

包内 procd 脚本从 UCI 生成 `/var/etc/ocserv.conf` 和 `/var/etc/ocpasswd`。不要直接编辑这些临时文件。用 LuCI 管理账号，或者使用 `server/tools/add-user.sh` 交互输入密码后写入 UCI 的密码哈希，再安排服务重启。客户端记住密码不改变服务器在线数量限制。

`server/examples/ocserv.conf.local` 作为额外配置示例。DNS 示例为 VPN 服务端地址 `10.77.0.1`；需让 dnsmasq/OpenClash 在此地址正确应答 VPN 客户端。不要把 VPN 入口 `192.168.19.254` 同时用作隧道 DNS，因为入口需要保留物理路径，容易造成 DNS 绕行/路由冲突。客户端对此情况会拒绝连接并回滚。

## OpenClash 与防绕过

认证客户端的流量应从真实的 `vpns*` 接口进入 OpenClash。给 VPN 建独立 fw4 区域，按实际单臂旁路由出口设置转发、DNS 输入许可及必要的源 NAT。未经认证的 LAN 主机不得通过修改网关进入透明代理，也不得直接连 HTTP/SOCKS/Mihomo 控制端口。

`server/examples/fw4-guard.nft` 是待审核的独立规则表示例，默认 LAN 接口为 `br-lan`，在代理重定向前拒绝 LAN 转发，并对 VPN 地址池做出口 NAT。它不是已经适配并应用到你的机器上的配置：

1. 核对真实接口、所有代理监听端口及 OpenClash 实际 hook 优先级。
2. 核对管理 IP 的访问例外，尤其是控制接口 9090；示例默认禁止 LAN 直接访问。
3. 给真实 VPN 设备添加独立 fw4 区域、DNS 规则和到实际出口的转发；仅放行“VPN 源 IP”不够，必须检查输入接口。
4. 先用 `nft -c -f 文件` 验证，再在维护窗口加载。不要自动覆盖已有防火墙。
5. 用未授权电脑测试修改网关、直连代理、伪造 VPN 源 IP 和 IPv6，再验证授权电脑的内网/DNS/外网。

知道服务器 IP 本身不等于获权；客户端无法可靠隐藏实际连接 IP。认证和输入接口隔离才是防绕过的边界。

## 原始来源

- [24.10 配方](https://github.com/openwrt/packages/blob/openwrt-24.10/net/ocserv/Makefile)
- [25.12 配方](https://github.com/openwrt/packages/blob/openwrt-25.12/net/ocserv/Makefile)
- [本项目采用的 1.5.0 配方](https://github.com/openwrt/packages/tree/c7a47d583127961590dfd832c424441c6810952c/net/ocserv)
- [ocserv 1.5.0 NEWS](https://gitlab.com/openconnect/ocserv/-/blob/1.5.0/NEWS)
- [25.12 默认 USE_APK](https://github.com/openwrt/openwrt/blob/openwrt-25.12/config/Config-build.in)
- [25.12.0 armsr/armv8 软件清单](https://downloads.openwrt.org/releases/25.12.0/targets/armsr/armv8/openwrt-25.12.0-armsr-armv8.manifest)
- [25.12.0 armsr/armv8 镜像与 SDK](https://downloads.openwrt.org/releases/25.12.0/targets/armsr/armv8/)

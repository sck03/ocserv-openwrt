# OpenWrt 多架构服务端

服务端支持 OpenWrt 25.12.x、APK、firewall4 和 LuCI。GitHub Actions 为以下目标分别编译 ocserv 与中文管理页：

| OpenWrt target/subtarget | ZIP / APK 架构 | 常见设备类型 |
|---|---|---|
| armsr/armv8 | aarch64_generic | N1、通用 ARM64 |
| x86/64 | x86_64 | x86 软路由 |
| mediatek/filogic | aarch64_cortex-a53 | MediaTek Filogic 路由器 |
| ipq806x/generic | arm_cortex-a15_neon-vfpv4 | Qualcomm IPQ806x |
| ramips/mt7621 | mipsel_24kc | MediaTek MT7621 |
| ath79/generic | mips_24kc | Qualcomm Atheros ATH79 |

这是 SDK 编译目标支持，不代表所有型号均已实机验收。其他 target、OpenWrt 24.10/IPK 和其他 ABI 不在当前发行范围内。同为 ARM64 也应选择对应的软件包架构，不能只看 CPU 位数。

在设备运行 `ubus call system board`、`cat /etc/apk/arch` 和 `apk --print-arch`，核对固件版本、target 和软件包架构。安装器优先使用 `/etc/apk/arch`，不存在时才回退到 `apk --print-arch`；后者在部分 OPL 固件上只报告较笼统的 `aarch64` 别名。依赖必须来自当前固件的软件源，设备必须已有 `/dev/net/tun`；套装不携带内核模块。

N1 使用 Cortex-A53 CPU，但用户这套 OPL/Flippy 固件的软件包架构是 **`aarch64_generic`**，应下载 `openwrt-25.12-aarch64_generic`。这里的 `aarch64_cortex-a53` 套装由 **mediatek/filogic SDK** 构建，不是所有 A53 设备的通用安装包。通用 ARM64 编译的程序可以在 N1 的 A53 CPU 上运行；不要修改 `/etc/apk/arch` 或强制安装来绕过固件的包架构选择。

从 Releases 下载对应架构的 ZIP，完整解压到新目录并上传路由器，以 root 执行：

```sh
cd /tmp/lansyra
sh install.sh
```

脚本校验 SHA256SUMS 和 APK-ARCHITECTURES，再预演安装和备份配置；错架构时在安装前停止。统一的 `preflight.sh` 检查适用于上述所有目标。同名 Lansyra 包会原地升级并保留配置；旧项目或不同包标识仍需按全新安装清理后部署，不会自动卸载旧服务端。

完整套装还包含 `with-lock.lua` 和 `lease.lua`。安装器、命令行账号脚本、管理页与后台恢复共享 `/var/run/ocserv-easy/lock` 的 POSIX 文件锁；已有维护操作时，新操作提示稍后重试。首次安装缺少 Lua/nixio 时，安装器先从固件软件源预演并安装 `luci-lib-nixio`，再取得共享锁，之后才读取和修改 VPN 配置。运行库预演也拒绝涉及内核模块的计划。锁由进程退出自动释放，不应删除锁文件来强行解锁。

安装器会保留已经确认启用的专用上网设置；存在待恢复的普通配置事务，或专用上网仍处于应用、待确认、恢复阶段时，安装在替换软件包和停止服务之前退出。先等待后台恢复，或在 LuCI 核对并处理恢复错误，再重新运行安装器。

首次网络初始化使用独立 UCI 候选文件，替换前在 `/etc/ocserv/easy-install-network` 保存前后版本。执行中断后，重跑同一完整套装会恢复未完成的事务，或收尾已验证的安装。若中断后管理员修改过防火墙，则保留现场和恢复记录并停止，避免覆盖后续修改；核对记录和安装前备份后再处理。旧安装器没有这份记录的遗留中断仍需手动核对，不能通过删除配置强行重跑。

首次安装默认允许 LAN 访问 TCP/UDP 4443，VPN 地址池为 10.77.0.0/24，配置 VPN 到 LAN 的转发与源 NAT。公网接入需按实际拓扑设置 WAN 防火墙、上级路由端口转发和 DNS。确认地址池不与现有网络冲突。添加账号、检查设置后再启动服务。

命令行管理账号可在同一完整套装目录执行 `sh add-user.sh employee01`。密码由 `ocpasswd` 交互读取，不进入命令行参数；输入完成后才取得维护锁，检查重复账号、待恢复事务和未应用的 UCI 修改，再用独立 UCI 临时目录提交。该脚本仅保存账号，需按其提示重启 ocserv 才会载入；需要立即更新账号且保留其他在线用户连接时，使用 LuCI 账号页。

VPN 专用 OpenClash 开关仍只针对已确认的 N1 单网口旁路由拓扑，其他设备保持关闭；多架构编译不扩大该功能的实机验证范围。N1 专项说明见 [OPENWRT-N1.md](OPENWRT-N1.md)。

GitHub 的服务端工作流默认并行构建表中全部六个目标；仅全部编译与回归通过后发布同一 Release。每个 ZIP 包含两个 APK、源码、BUILDINFO、架构清单及校验值。BUILDINFO 保存 SDK target、版本、SHA-256 和提交号，打包检查 SDK 配置、ELF 位数、字节序和机器类型。

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

在设备运行 `ubus call system board` 和 `apk --print-arch`，核对固件版本、target 和软件包架构。N1/OPL 的 `aarch64` 别名仅允许使用 `aarch64_generic` 套装。依赖必须来自当前固件的软件源，设备必须已有 `/dev/net/tun`；套装不携带内核模块。

从 Releases 下载对应架构的 ZIP，完整解压到新目录并上传路由器，以 root 执行：

```sh
cd /tmp/linkora-vpn
sh install.sh
```

脚本校验 SHA256SUMS 和 APK-ARCHITECTURES，再预演安装和备份配置；错架构时在安装前停止。`preflight-n1.sh` 保留旧文件名以兼容既有用法，其检查适用于上述所有目标。

首次安装默认允许 LAN 访问 TCP/UDP 4443，VPN 地址池为 10.77.0.0/24，配置 VPN 到 LAN 的转发与源 NAT。公网接入需按实际拓扑设置 WAN 防火墙、上级路由端口转发和 DNS。确认地址池不与现有网络冲突。添加账号、检查设置后再启动服务。

VPN 专用 OpenClash 开关仍只针对已确认的 N1 单网口旁路由拓扑，其他设备保持关闭；多架构编译不扩大该功能的实机验证范围。N1 专项说明见 [OPENWRT-N1.md](OPENWRT-N1.md)。

GitHub 的服务端工作流默认并行构建表中全部六个目标；仅全部编译与回归通过后发布同一 Release。每个 ZIP 包含两个 APK、源码、BUILDINFO、架构清单及校验值。BUILDINFO 保存 SDK target、版本、SHA-256 和提交号，打包检查 SDK 配置、ELF 位数、字节序和机器类型。

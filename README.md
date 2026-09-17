# 布利杰VPN

原生 Win32 / C++17 的 OpenConnect 客户端 0.4.0，配套 ocserv 1.5.0-r3 服务端和中文管理页 0.4.1。Windows 提供 x86/x64 便携包；N1 服务端只维护 **OpenWrt 25.12 系列、armsr/armv8、aarch64_generic、APK**，兼容 OPL 的 `apk --print-arch` 输出 `aarch64`。

客户端静态合并 OpenConnect 9.21、OpenSSL、libxml2 和 zlib，不需要 Qt、.NET 或额外 VC 运行库；同目录的官方 Wintun 驱动组件必须保留。Windows 7 SP1 是兼容目标，仍需实机验收。

## 客户端连接

1. 完整解压对应架构的 ZIP，退出旧版，运行 `布利杰VPN.exe`。
2. 填写服务器，例如 `192.168.19.253:4443`（自动补上 `https://`），再填写账号密码。
3. 点击连接。首次遇到自签或名称不匹配的证书时，核对地址及完整 `pin-sha256`，点击 **信息准确，记住并连接**，对应 OpenConnect GUI 的 **Accurate information**。
4. 指纹按服务器主机和端口保存，后续同一公钥无需再次确认。公钥变化时显示新旧指纹，必须重新确认。

证书确认在 TLS 握手中完成，**早于账号密码提交**；取消确认不提交密码。未连接时地址始终可编辑，旧导入文件的 `LockServer=1` 不再锁住输入框。更换主机或端口不会沿用另一台服务器的证书或自动填入其密码。

`.bvpn` 配置、公共 CA 和管理员预先核实的完整指纹仍可选用。无需先把自签证书导入 Windows 根证书库。过期或尚未生效的证书仍需修正证书或系统时间。

客户端的 **导出配置** 可将当前地址、连接选项与已确认的指纹或公共 CA 保存为 `.bvpn`，供其他客户端导入。服务端管理页的 **客户端配置** 也可下载地址配置或附带 CA 的配置。导出不包含账号、密码或私钥。

## N1 全新安装

当前设备：Phicomm N1，ARMv8，OPL_FW4 For N1 v0.0.8，LuCI openwrt-25.12，内核 `6.12.66-flippy-94+`。套装基于官方 25.12.5 SDK 构建，依赖必须来自与实际 OPL 固件匹配的软件源；不携带或替换内核模块。

将服务端 ZIP 完整解压并上传到 N1，例如 `/tmp/bulijie-vpn`，通过 SSH 以 root 执行：

```sh
cd /tmp/bulijie-vpn
sh install.sh
```

请解压到新目录，避免混入旧 APK。安装脚本校验文件、检查 25.12/APK/TUN、预演依赖安装并备份配置，然后安装 **ocserv、luci-app-ocserv-easy 两个 APK**。它会修复旧页面留下的明文密码记录，安装成功后移除重复的原版界面和翻译包。全新安装生成证书并配置旁路由的基本 VPN 转发；之前只手动安装 APK、仍使用本套装默认网络的设备，也会补齐转发。重新登录 LuCI，进入 **VPN → 布利杰VPN**，添加账号、核对服务设置并启动服务。

默认端口 `4443`、VPN 网段 `10.77.0.0/24`、每个账号同时一台设备。没有预设账号或密码。初始 DNS 为 `1.1.1.1`，需按实际部署检查连通性。

## OpenClash 专用上网开关

管理页新增 **VPN 专用上网**。默认关闭；OpenClash 由管理员正常安装并配置。点击开启后，程序读取实际 LAN 地址和当前管理电脑地址，自动处理 VPN DNS、OpenClash 白名单、防火墙和标准流量卸载。普通 LAN 设备不能借 N1 的网关、DNS 或代理端口上网；通过主路由的普通上网保持原有行为。

开关适用于已确认的 **N1 单网口旁路由**。关闭后恢复本功能改动的设置；后来添加的账号、修改的订阅等保留。服务操作失败或应用后管理页面无法重新确认访问，会尝试自动恢复。订阅、节点及分流规则不由本开关修改。详见 [开关说明](docs/VPN-ONLY-OPENCLASH.md)。

## 构建与发行

- [Windows 客户端工作流](https://github.com/sck03/ocserv-openwrt/actions/workflows/build-client.yml)：独立构建 x86/x64、源码和校验文件。
- [N1 服务端工作流](https://github.com/sck03/ocserv-openwrt/actions/workflows/build-server.yml)：通过 `sdk_version` 指定具体 `25.12.x` 或 `auto`。默认 25.12.5，`auto` 只选择 25.12 系列稳定版。`build_jobs` 默认 `auto`，使用运行器全部可用 CPU 并行编译，也可手动填写任务数。
- 标准运行器为 `ubuntu-24.04`，Actions/管理页脚本检查使用 Node.js 24。运行器系统版本与 N1 固件版本是不同概念。
- 手动工作流全部检查成功后发布独立 Pre-release；[Releases](https://github.com/sck03/ocserv-openwrt/releases) 附件长期保留，Actions artifacts 保留 7 天。
- 服务端 artifact 为 `openwrt-25.12-aarch64_generic`，内含两个 APK、安装工具、源码、BUILDINFO 和 SHA256SUMS；ZIP 名称带管理页版本，便于区分修复版。

## 资料

- [N1 安装与首次配置](docs/OPENWRT-N1.md)
- [中文管理页](docs/SERVER-UI.md)
- [客户端证书与密码保存](docs/CLIENT.md)
- [构建说明](docs/BUILD.md)
- [上游版本更新](docs/UPSTREAM-UPDATES.md)
- [验证记录与实机边界](docs/VALIDATION.md)
- [第三方组件与许可证](THIRD-PARTY-NOTICES.md)

项目采用 GPL-3.0-or-later；第三方组件遵守各自许可证。编译、模拟事务和回环 TLS 测试不替代 N1/Win7 实机验收。

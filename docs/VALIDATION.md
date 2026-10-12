# 验证记录

本页保留当前支持平台的验证证据与实机边界。早期重构、旧命名及 Windows 7 的记录见 [历史版本](https://github.com/sck03/ocserv-openwrt/blob/af2394d/docs/VALIDATION.md)，不作为当前版本的验收要求。

## 2026-10-12 代码质量与体积优化

应用代码为 [`114018b`](https://github.com/sck03/ocserv-openwrt/commit/114018b)，以下工作流均针对该提交；后续收尾只更新文档。

服务端配置改为一次合并字符串，覆写检查只扫描一次附加配置，账号比较复用已有函数。Windows 核心日志在脱敏后才转换为宽字符；Android 路由拆分共用输出列表，去掉递归过程中反复创建和拼接的中间列表。Linux 认证输入改为可取消的异步管道写入，并拒绝非法用户名。Apple 共用指纹与凭据校验，限制 UTF-8 字节数及内嵌 NUL；收尾时在同一把锁内收集断开回调并标记完成，避免遗漏迟到的停止请求。

| 平台 | GitHub 验证 |
|---|---|
| Windows | [运行 38185269509](https://github.com/sck03/ocserv-openwrt/actions/runs/38185269509)：x86/x64 编译、PE 审计、便携包启动、原生回归全部通过；x64 七项真实 Wintun 隧道回归通过 |
| macOS / Linux | [运行 38185274006](https://github.com/sck03/ocserv-openwrt/actions/runs/38185274006)：macOS Universal 编译及源码打包、Linux 两种架构编译、IPC/协议回归、图形启动及 DEB 打包全部通过 |
| iOS | [运行 38185276972](https://github.com/sck03/ocserv-openwrt/actions/runs/38185276972)：共用地址、指纹、凭据和数据包校验、核心、应用与扩展的 unsigned 归档全部通过 |
| Android | [运行 38185279525](https://github.com/sck03/ocserv-openwrt/actions/runs/38185279525)：三个架构核心、JUnit、debug APK、签名和 16 KB 对齐检查全部通过 |
| OpenWrt | [运行 38185271897](https://github.com/sck03/ocserv-openwrt/actions/runs/38185271897)：服务端功能回归及六架构 APK 编译、打包全部通过 |

已下载并核对 Windows 和服务端回归附件的 SHA-256 及 ZIP CRC。Windows 每个架构均通过连接 60、配置/DPAPI 47、界面 29、WSH 6、资源 8 组及回环认证 35 项；x64 另有真实隧道 7 项。服务端报告确认逻辑 51、事务 36、证书 11、安装 24、密码文件 9、浏览器 15 组及轮询 5 组通过。Linux 新增写入阶段取消和非法用户名回归；Apple 新增完整指纹、非规范 Base64、UTF-8 长度及内嵌 NUL 边界；Android 新增 IPv4/IPv6 密集排除路由的逐地址检查。

本地配置、脚本、打包和浏览器回归通过；实际生成的 Windows 源码 ZIP 解压后，构建参数 11、客户端打包 10、发布元数据 6 项再次通过。Ruff F、Python/JavaScript 语法及 C 数据包检查通过。

128 条路由、117,336 字节配置的对照实验生成完全一致的内容。暂停垃圾回收时，单次渲染的 Lua 堆增长从 14,844.59 KiB 降到 337.60 KiB；这是本地 Lua 5.1 分配对照，不能换算成 N1 常驻内存或吞吐提升。Windows 源码包的项目文件从 161 个减至 90 个，去除 71 个无关平台文件，保留所有必要构建输入与第三方归档。过时验证说明转为历史链接。清理生成目录约 22 MiB，并卸载本次安装的测试依赖；源码、许可证、备份、发行档案和验证报告保留。

## 2026-10-11 桌面客户端与会话生命周期

该轮应用代码为 `670f650`；Windows 0.7.0、macOS/Linux 0.1.0、iOS 0.2.1。Windows 支持 10/11，macOS 支持 13+ Universal，Linux 支持 Debian 12 / Ubuntu 24.04 的 amd64 / arm64。

| 平台 | 已完成的 GitHub 验证 |
|---|---|
| Windows | [运行 38086011288](https://github.com/sck03/ocserv-openwrt/actions/runs/38086011288)，`670f650`：x86/x64 编译、PE 审计、便携包启动与原生回归；x64 另通过真实 Wintun/CSTP 隧道回归 |
| macOS / Linux | [运行 38084910520](https://github.com/sck03/ocserv-openwrt/actions/runs/38084910520)，`670f650`：macOS Universal 应用与系统扩展、Linux 两种架构编译、测试、图形启动、DEB 与源码打包 |
| iOS | [运行 38082843251](https://github.com/sck03/ocserv-openwrt/actions/runs/38082843251)，`9c313bd`：Apple 共用地址/数据包校验、核心、应用与隧道扩展编译及未签名归档 |

Windows 两种架构各通过配置/DPAPI 47 项、连接/脱敏/启动注册 60 项、原生界面 29 项、真实 WSH 6 项、资源 8 组及回环 TLS/认证 35 项。资源测试各处理 60 万事件、1,000 轮回收；x64 最终句柄保持 118 个。x64 七项真实隧道回归覆盖正常流量、首次/重连配置失败、网络变化、睡眠唤醒、暂停时取消和断开脚本失败，均确认网卡移除。

Linux 在私有 D-Bus 和真实 GIO 子进程管道中验证成功认证、重复使用、取消、超大输出与换行注入。仅将 VPN 服务所需的临时秘密交给 NetworkManager，丢弃其他认证字段。图形启动与截图覆盖两种架构；认证子进程为合成测试程序，没有连接真实 VPN。

macOS 与 iOS 共用 `client/apple`。该轮验证覆盖地址、数据包和证书有效期校验、会话代次、暂停合并及编译归档。macOS 实际加载系统扩展仍需要有效的 Apple 签名和描述文件。

该轮安装包复核结果：Linux amd64 / arm64 DEB 分别为 33,468 / 31,632 字节，macOS Universal 应用 ZIP 为 5,093,444 字节，本地 Windows x64 ZIP 为 3,040,857 字节。源码包保留对应上游源码和许可证。清理构建目录、展开的依赖及重复临时文件约 1.90 GiB；保留校验过的下载、发行档案、备份和验证报告。

## 2026-10-10 服务端与移动端基线

| 平台 | 该轮验证及提交 |
|---|---|
| 服务端 | [运行 38028346418](https://github.com/sck03/ocserv-openwrt/actions/runs/38028346418)，`7ec2c05`：服务端回归及六个 OpenWrt 25.12.5 目标的 APK 编译、打包 |
| Android | [运行 38028911546](https://github.com/sck03/ocserv-openwrt/actions/runs/38028911546)，`6d0867c`：三个架构核心、单元测试、debug APK、签名与 16 KB 对齐检查 |

服务端当轮通过 Lua 逻辑 50 项、启动配置一致性 4 项、事务 36 项、进程 9 项、安装 24 项、密码文件 9 项、证书 11 项、隔离网络 20 项、浏览器 15 组和轮询 5 组。六个目标为 `x86/64`、`armsr/armv8`、`mediatek/filogic`、`ipq806x/generic`、`ramips/mt7621`、`ath79/generic`。

更早的域名/IPv6、N1 运行库接口、账号认证和防火墙事务记录均保留在上述历史版本及其链接的 Actions 中。历史结果只证明当时注明的提交。

## 验证方法与范围

- Windows 原生回归运行实际 OpenConnect/GnuTLS、临时回环 HTTPS 服务、DPAPI、Win32 控件和 Windows Script Host。便携包检查清空开发工具 PATH。普通本地测试不创建网卡；仅隔离的 Windows 管理员运行器使用 `--network` 验证 Wintun、分流路由和 UDP 往返。
- 服务端 Lua 测试模拟 UCI/nixio 与服务边界；配置一致性测试执行实际启动脚本。证书测试使用真实 certtool/OpenSSL。防火墙测试使用独立 Linux 网络命名空间、veth、nftables、NAT 与 TPROXY，不操作用户 N1。
- `openwrt_runtime_tests.lua` 和 `ocserv_auth_tests.py` 仅允许在带 `/tmp/ocserv-easy-test-root` 标记的独立 OpenWrt 测试根目录运行。QEMU/rootfs 或模拟接口通过不等于真实设备验收。
- Android 的完整编译与 JUnit 结果以对应 GitHub 工作流为准。Apple 的地址、凭据和数据包单元检查及 unsigned 编译归档不证明系统扩展获准加载。
- 本地报告放在 `test-results/`，不进入 Git 或安装包。验证工作流使用 `publish_release=false`、Android debug 和 Apple unsigned；构建附件不等于正式发布。

## 仍需实机验收

- Windows 10/11 实际设备上的管理员授权、Wintun、DNS/路由，以及异常退出后的回收。
- macOS 签名、系统扩展授权、睡眠恢复和真实 VPN 流量；Linux 的系统 NetworkManager、认证助手及真实隧道。
- Android/iOS 真机的公网入口、网络切换、后台运行、MTU 与双栈流量。
- OPL v0.0.8 / 6.12.66-flippy-94+ 的 APK 依赖与安装、CSTP/DTLS 转发、同账号连接限制。
- OpenClash 各模式、DNS/IPv6 隔离、断网重连、重启持久化及长期性能。

这些设备与网络检查仍需对应环境，自动回归不能替代。

# 验证记录

本页记录岚序（Lansyra）全新安装版本的验证证据与实机边界。以前的打包和系统升级记录见[历史版本](https://github.com/sck03/ocserv-openwrt/blob/ba903fe/docs/VALIDATION.md)。

## 2026-10-14 名称统一、全新安装与代码优化

Windows、桌面与服务端代码提交为 [`9837c6c`](https://github.com/sck03/ocserv-openwrt/commit/9837c6c)。Android / iOS 在 [`7ec83b8`](https://github.com/sck03/ocserv-openwrt/commit/7ec83b8) 验证；已核对其应用、共享 Apple 核心、构建脚本和工作流在后续提交中没有变化。

| 平台 | GitHub 验证 |
|---|---|
| Windows | [运行 38514906562](https://github.com/sck03/ocserv-openwrt/actions/runs/38514906562)：x86/x64 编译、PE 审计、便携包启动与原生回归通过；x64 七项真实 Wintun 隧道回归通过 |
| macOS / Linux | [运行 38514908870](https://github.com/sck03/ocserv-openwrt/actions/runs/38514908870)：macOS Universal 编译通过；Linux 两种架构的编译、IPC/协议、DEB 审计及 Debian 13 / Ubuntu 26.04 四组安装与启动检查全部通过 |
| iOS | [运行 38513614174](https://github.com/sck03/ocserv-openwrt/actions/runs/38513614174)：地址、指纹、凭据、数据包校验及应用/扩展 unsigned 归档通过 |
| Android | [运行 38513611697](https://github.com/sck03/ocserv-openwrt/actions/runs/38513611697)：三个架构核心、JUnit、debug APK、签名与 16 KB 对齐检查通过 |
| OpenWrt | [运行 38514911188](https://github.com/sck03/ocserv-openwrt/actions/runs/38514911188)：功能回归与六架构 APK 编译、ELF 审计和打包全部通过 |

界面和系统应用名称统一为“岚序 Lansyra”，程序、安装包及应用标识使用 Lansyra。Windows 便携包的中英文启动窗口、EXE 产品信息，Android APK 的全部本地化启动器名称，以及 Apple 应用和扩展的 Info.plist 均已检查。macOS 实际版本为 `0.1.1 (2)`，iOS 为 `0.2.2 (5)`，修复了包内版本停留在默认 `1.0` 的问题。

全新安装使用新的密码加密上下文、NetworkManager 配置标记和防火墙标识；移除旧包替换声明、旧安装推断和相关兼容测试。服务端统一使用 `preflight.sh`，文档不再指导复用旧客户端数据、凭据或防火墙事务。

Android 对包含和排除路由排序、去重并消除被覆盖的子网，减少重复计算；服务端日志从尾部筛选最多 256 条有效记录，避免反复移动整个数组。macOS 限制配置读取大小并复用日期格式器；iOS 等待系统配置加载，避免重复创建或改写活跃连接。Linux 减少界面刷新时的重复配置查询，并启用支持的链接时优化。

实际应用包大小：Windows x64 / x86 ZIP 为 3,010,509 / 3,097,541 字节；Linux amd64 / arm64 DEB 为 37,448 / 35,936 字节；macOS ZIP 为 5,093,690 字节，Android debug APK 为 36,727,976 字节，iOS unsigned 归档为 3,410,398 字节。Linux 的 GTK、NetworkManager 和 OpenConnect 依赖由 APT 提供，DEB 大小不等于首次安装总量。源码与测试结果继续独立打包。

已核对七个客户端和六个服务端实际附件的 GitHub SHA-256、ZIP CRC 和配套校验清单。每个服务端 ZIP 的 17 个内部文件全部通过 SHA-256 校验，包含两个 APK 及当前 `install.sh`、`preflight.sh`，不包含验证目录。Windows 每种架构通过连接 60、配置/DPAPI 47、界面 29、WSH 6、资源 8 组及回环认证 35 项；x64 另通过真实隧道 7 项。服务端逻辑 51、事务与日志 37、证书 11、安装 24、密码文件 9、浏览器 15 组和轮询 5 组通过。

本地打包回归通过 Windows 13、移动端 19、服务端 7 项，清理脚本 4 项；Python / Shell / YAML 语法和 JScript 24 项检查通过。已删除约 188 MiB 旧安装包和中间验证文件、58 MiB 生成缓存、16 MiB 回归临时文件，并卸载本轮临时测试依赖。当前验证摘要保存在 `artifacts/validation/lansyra/`，已校验的当前安装包位于 `dist/`；历史数据不进入安装包。

## 验证方法与范围

- Windows 原生回归运行实际 OpenConnect/GnuTLS、临时回环 HTTPS 服务、DPAPI、Win32 控件和 Windows Script Host。普通本地测试不创建网卡；仅隔离的管理员运行器使用真实 Wintun、`198.18.0.0/24` 分流路由和 UDP 往返测试。
- 服务端 Lua 测试模拟 UCI/nixio 与服务边界；证书测试使用真实 certtool/OpenSSL，防火墙测试使用独立 Linux 网络命名空间。`openwrt_runtime_tests.lua` 和 `ocserv_auth_tests.py` 仅在带 `/tmp/ocserv-easy-test-root` 标记的独立 OpenWrt 测试根目录运行。
- Linux 使用私有 D-Bus 与合成认证助手验证 IPC、取消、输入限制和回调；干净容器验证 DEB 依赖、真实系统助手位置及图形启动，未连接真实 VPN。Android 的 JUnit 与 APK 检查、Apple 的单元校验及 unsigned 编译均以对应工作流为准。
- 本轮使用 `publish_release=false`、Android debug 和 Apple unsigned。源码及有效回归保留，测试数据不进入 Git 或安装包；构建附件不等于正式发布。

## 仍需实机验收

- Windows 实际设备上的管理员授权、Wintun、DNS/路由与异常退出回收。
- macOS 签名、系统扩展授权、睡眠恢复与真实 VPN 流量；Linux 的系统 NetworkManager、交互认证与真实隧道。
- Android/iOS 真机的公网入口、网络切换、后台运行、MTU 和双栈流量。
- OPL v0.0.8 / 6.12.66-flippy-94+ 的 APK 依赖与安装、CSTP/DTLS 转发、同账号连接限制；OpenClash 的 DNS/IPv6 隔离、重启持久化与长期性能。

自动回归不能替代这些设备和网络检查。

# 验证记录

本页记录当前支持平台的验证证据与实机边界。此前的构建、资源优化与设备验证记录保留在[历史版本](https://github.com/sck03/ocserv-openwrt/blob/d7de421/docs/VALIDATION.md)，不作为新系统基线的验收结果。

## 2026-10-13 打包精简与系统升级

应用与构建脚本提交为 [`82cf893`](https://github.com/sck03/ocserv-openwrt/commit/82cf893)。Linux 桌面基线为 Debian 13 / Ubuntu 26.04 LTS，均支持 amd64、arm64；Windows 交叉编译、Android 与 OpenWrt 构建宿主升级为 Ubuntu 26.04。

| 平台 | 当前提交的 GitHub 验证 |
|---|---|
| Windows | [运行 38365497877](https://github.com/sck03/ocserv-openwrt/actions/runs/38365497877)：x86/x64 编译、PE 审计、便携包启动与原生回归通过；x64 七项真实 Wintun 隧道回归通过 |
| macOS / Linux | [运行 38365509968](https://github.com/sck03/ocserv-openwrt/actions/runs/38365509968)：macOS Universal 编译通过；Linux 两种架构的编译、IPC/协议、DEB 审计及 Debian 13 / Ubuntu 26.04 四组安装与启动检查全部通过 |
| iOS | [运行 38365523521](https://github.com/sck03/ocserv-openwrt/actions/runs/38365523521)：地址、指纹、凭据、数据包校验及应用/扩展 unsigned 归档通过 |
| Android | [运行 38365516121](https://github.com/sck03/ocserv-openwrt/actions/runs/38365516121)：三个架构核心、JUnit、debug APK、签名与 16 KB 对齐检查通过 |
| OpenWrt | [运行 38365504421](https://github.com/sck03/ocserv-openwrt/actions/runs/38365504421)：功能回归与六架构 APK 编译、ELF 审计和打包全部通过 |

Windows 的 `windows-x64`、`windows-x86` Actions 附件由原来的 77,969,313 / 73,695,380 字节降至 3,006,734 / 3,096,229 字节。测试程序直接从构建目录传给独立 Windows 回归任务，不再复制到 `dist/validation-*` 或应用附件中。便携包增加运行文件和许可证清单检查，拒绝测试程序、源码 ZIP、旧配置与本机数据。

macOS、Android、iOS 应用附件分别为 5,094,201、36,729,919、3,411,976 字节；对应源码作为独立附件保留。Linux DEB 分别为 37,708 / 36,182 字节，实际 ELF 为 61,464 / 69,600 字节。Linux 使用系统 GTK、libnm、libsecret 与 OpenConnect 插件，这些组件由 APT 安装；小体积不代表缺少应用代码，也不等于首次安装的总下载量或运行内存。

Linux 改用 CPack / `dpkg-shlibdeps` 生成依赖和安装大小，从独立临时目录打包并裁剪符号。安装包检查确认只包含客户端、桌面入口、图标、说明和许可证。认证助手改为通过运行时 libnm 插件信息定位，修复将构建机路径用于其他发行版的问题；`--check-runtime` 在四组新系统安装测试中均找到实际助手。测试同时检查 ELF 架构、动态库加载、`--version` 和图形启动，不依赖预装的开发库。

服务端保留 ELF、启动脚本、管理模块及 CSS 审计，依赖报告和页面资源哈希单独上传。安装 ZIP 不再包含 `validation/ui` 或重复页面副本；源码配方只从 Git 跟踪文件收集，拒绝混入未跟踪的旧文件。APK 与上游压缩源码直接存入 ZIP，避免重复压缩。移动/Apple 对应源码仅收录所需构建与回归脚本。

已核对客户端及六架构服务端共 13 个实际附件的 SHA-256、ZIP CRC、配套校验清单和内容；Android APK 包含预期的三个 ABI，客户端应用附件不包含测试目录或源码包。Windows 每种架构通过连接 60、配置/DPAPI 47、界面 29、WSH 6、资源 8 组及回环认证 35 项；x64 另通过真实隧道 7 项。服务端报告确认逻辑 51、事务 36、证书 11、安装 24、密码文件 9、浏览器 15 组和轮询 5 组通过。

本地打包回归通过 Windows 13、移动端 18、服务端 7 项，清理脚本 4 项；实际生成的 Windows 源码 ZIP 解压后，构建参数 11、客户端打包 13、发布元数据 6 项通过。Python Ruff F、语法、工作流 YAML 与 JScript 24 项检查通过。清理脚本覆盖旧测试二进制、生成的 Apple 工程/Info.plist/图标及构建缓存；本轮清理旧测试记录、临时下载和缓存约 221 MiB，并卸载临时安装的检查工具。当前验证摘要保存在 `artifacts/validation/package-hygiene/`，过时记录通过历史链接保留。

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

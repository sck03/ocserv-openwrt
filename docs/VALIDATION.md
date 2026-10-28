# 验证记录

本页记录岚序（Lansyra）全新安装版本的验证证据与实机边界。以前的打包和系统升级记录见[历史版本](https://github.com/sck03/ocserv-openwrt/blob/ba903fe/docs/VALIDATION.md)。

## 2026-10-28 保存冲突与普通 VPN 代理接入修复

管理页更新为 0.6.1-r1，ocserv 和客户端版本不变。检查确认 OpenWrt 25.12 的 `luci.model.uci.cursor()` 忽略自定义目录并通过 rpcd 提交真实配置；原来的候选提交随后被本程序的文件冲突检查拒绝。现改用显式依赖的 `libuci-lua` 原生绑定，验证配置目录和暂存目录，并保留真正的并发修订、UCI 待提交、锁及回滚检查。模拟器同步采用实际 LuCI 桥的目录语义，新增错误绑定在写入前被拒绝的回归。

普通 VPN 模式自动接入 Nikki/Momo：由 ocserv/代理的 procd 启动事件触发一次性任务，种入动态 `vpns*` 入口并优先添加 VPN 网段的代理/DNS 允许项；保留 LAN 接口与已有访问控制项。开启隔离时自动任务不修改配置，关闭隔离后恢复普通接入。首次接入或模板升级可重启代理一次，重复事件不写闪存、不反复重启；没有新增常驻代理轮询进程。异常重启、模板不匹配、待提交修改、租约竞争、IPv6 地址池、隔离切换及清理均有回归覆盖。

已核对 Windows 事件队列和后台保存、Apple 网络设置等待、Android 会话归属及 Linux 认证助手的资源边界与构建入口；本轮没有据此删除仍在使用的模块、上游源码或许可证。事务完成后移除废弃恢复副本和 RAM 中的临时 UCI 文件，本地清理约 117.2 MiB 的构建与缓存，保留发行包、验证结果及工具链。

本地通过服务端逻辑 75、代理/隔离事务 76、生命周期 4、控制器 8、配置生成 4、进程边界 9、服务端打包 8 项；三款代理各 18 项真实 Chrome 页面流程与 8 项轮询检查无浏览器脚本错误。Windows x64 完整编译及 CTest 5/5，通过连接 61、配置/DPAPI 92、UI 51、真实 WSH 7、资源 9、回环 TLS 认证 35 项。资源检查处理 600,000 个事件、清空 1,000 轮，私有内存均为 2,293,760 字节，句柄均为 134。

Python 46 文件、5 份工作流 YAML、14 份 Shell 及管理页 JS 语法检查通过；构建参数、客户端/移动端构建、发布和清理回归通过。服务端真实 Linux 安装、证书及 nftables 检查由 GitHub 验证确认；本地 Windows 平台跳过不计为通过。尚未连接用户的 N1、真实 Nikki/Momo 核心或进行长期转发验收。安装更新的 0.6.1 APK 套装后才生效，仅推送源码不会更新路由器。

## 2026-10-28 ocserv 1.6.0 与三款代理联动

服务端从 ocserv 1.6.0-r1 开始，管理页为 0.6.0-r1，客户端版本不变。新增统一的 OpenClash/Nikki/Momo 配置模块；Nikki/Momo 使用原生 nft 接口集合覆盖动态 VPN 设备，不新增常驻轮询。事务应用前新增附加文件变化检查，并清除专用模式下冲突的手工排除路由。删除旧 guard 状态、地址池和版本输出兼容分支，以及上游已提供正式选项的测试关闭补丁。专用上网说明合并为 `VPN-ONLY-PROXY.md`。

源码切换只需版本号。实际下载的 ocserv 1.6.0 归档匹配官网 SHA-256；OpenConnect 9.21 分离签名通过固定官方密钥验证，计算摘要与仓库锁定值一致。Windows/MSYS GnuPG 路径调用已实际验证。官方 1.6.0 的 IPv6 服务端地址变更为网络地址；客户端使用下发参数，未增加旧地址适配代码。

本地通过 Lua 逻辑 74、guard 事务 61、控制器权限 8、配置生成 4、构建参数 13、服务端打包 8 项。真实 Chrome 针对三款代理分别通过 18 项页面流程，另通过 8 项轮询检查，浏览器无脚本错误。Python 45 个文件、工作流 YAML、Shell 和管理页 JS 语法检查通过。客户端构建、移动构建、发布及网络脚本检查通过。

Windows x64 原生连接 61、配置/DPAPI 92、UI 51、真实 WSH 7、资源 9、回环 TLS 认证 35 项通过；600,000 个事件、1,000 轮清空前后私有内存均为 2,293,760 字节，句柄均为 134。该短时检查不代表实际设备的长期占用。本地证据保存在 `test-results/windows-continuation/`、`test-results/server-ui-OpenClash/`、`test-results/server-ui-Nikki/`、`test-results/server-ui-Momo/` 和对应 JSON/日志。

Windows [运行 40622651476](https://github.com/sck03/ocserv-openwrt/actions/runs/40622651476) 对提交 `a2dbc17` 完成 x86/x64 编译、PE/源码包审计、两架构原生回归；x64 另通过 4 项动态 WFP 和 8 项真实 Wintun 隧道检查。之后的 `a64f61e` 只修复服务端日志窗口开启时未更新完成提示，客户端代码和工作流未变。

服务端最终提交 `a64f61e` 的 [运行 40623432797](https://github.com/sck03/ocserv-openwrt/actions/runs/40623432797) 已通过 Linux 安装/锁竞争 49、证书 14、密码文件 9、nftables 报文 21 项，以及三款代理各 18 项浏览器流程和 8 项轮询检查。新增报文检查使用真实 ifname interval 集合验证重命名接口后的 TCP、UDP、DNS；模拟代理监听器不等同于真实代理核心。此前的服务端构建由本轮主动取消，避免生成缺少提示修复的产物。

已下载的 Windows 双架构及服务端验证附件均通过 GitHub artifact SHA-256 和 ZIP CRC 校验，保存在 `artifacts/validation/2026-10-28-proxies/`。本地清理约 101.7 MiB 构建/缓存；删除临时参考目录被自动审查阻止后，按用户要求将三个目录（约 5.77 MiB）集中移动到 `.reference/待手动删除-20261028/`，由用户手动删除。保留发行包、验证记录、锁定源码和工具链。

服务端运行已成功完成六个目标的 APK 编译、ELF 审计和源码/安装套装打包：armsr/armv8、ramips/mt7621、x86/64、mediatek/filogic、ipq806x/generic、ath79/generic。实际 SDK 为 25.12.6，六份已校验审计附件中的压缩后管理页 JS/CSS 摘要一致。SDK 会压缩 JS，因此发布 JS 的摘要与未压缩源码不同；N1 APK 解压内容已确认包含最终日志窗口提示修复与代理适配。

N1 最终套装保存在 `dist/Lansyra-server-ocserv-1.6.0-r1-openwrt-25.12.6-aarch64_generic-ui-0.6.0-r1-20261028-105320.zip`。已核对外层 artifact digest、ZIP CRC、套装外部校验及内部全部 19 项 SHA-256；对应源码归档中的 app.js、proxy.lua、guard.lua 与当前源码逐字节一致。套装 SHA-256 为 `8e6534cd3ccb637e1af1df4856208bf3ea5bd219d1dfa065ed4b4967eaead42e`，BUILDINFO 记录提交 `a64f61e`。

没有将 Windows 上的平台跳过计为通过；实际 N1/OPL、真实代理核心、手机和长期转发仍需设备验收。本轮 `publish_release=false`，没有创建 Release。

## 2026-10-25 并发维护与响应性优化

本轮版本为 Windows 0.7.4、Linux/macOS 0.1.4、Android/iOS 0.2.4、管理页 0.5.5-r1，ocserv 保持 1.5.0-r7。新增独立偏好写入、Apple 系统网络设置屏障、服务端共享租约和可丢弃查询缓存；保留原数据格式、凭据来源绑定和 IPv6 防绕行策略。Android 路由补集改为区间归并与扫描，安装器和账号工具参与已有 POSIX 锁，关键原子替换补充父目录同步。版本升级说明、模块边界、构建依赖和完整测试入口已同步。

本地 Windows x64 全量编译无警告，CTest 5/5；连接 61、配置/后台保存/DPAPI 92、UI 51、真实 WSH 7、资源 9、回环认证 35 项通过。最终 UI 回归包含已完成的旧快照、删除自动连接目标后的失败恢复、存储恢复后的关键保存重试，以及注销通知返回前的写入。资源测试发送 60 万事件、清空 1,000 轮，句柄保持 134，私有内存保持 2,297,856 字节；这只是短时测试进程，不能推导整个 VPN 的长期内存占用。便携包的中英文启动、版本和 PE 系统导入审计另有独立检查。

服务端实际 Lua 模块逻辑 74、守护事务 38、进程边界 9、控制器权限 6、配置渲染 4 项及真实 Chrome 页面 18、轮询 8 项通过，浏览器无脚本错误；新增回归确认临时 occtl 帮助查询失败不会被缓存为不支持能力。打包 8 项验证新增锁和缓存模块进入实包/对应源码。构建参数 11、Windows 打包 13、发布元数据 6、客户端构建 13、移动构建 19 项通过，Python 45 个文件及全部工作流/Shell/管理页脚本语法检查通过。Android RoutePolicy/NetworkRoutes 的 14 项纯 JVM 回归含 4096 条输入、随机地址集对照及输出上限检查。

本地最终证据在 `test-results/windows-maintenance-final/`、`test-results/server-regressions.json`、`test-results/guard-transactions.json`、`test-results/server-ui/` 和 `test-results/server-polling.json`。Linux 安装/POSIX 锁竞争、Windows x86/WFP/Wintun 和 Apple 实际编译由本次 GitHub 工作流单独确认；未把本地 Windows 的平台跳过当作测试通过。真实 N1、手机、Apple 签名授权和 24–72 小时运行仍需设备验收。

运行代码分为 [`da88316`](https://github.com/sck03/ocserv-openwrt/commit/da88316) 和补充边界修复 [`acd4dae`](https://github.com/sck03/ocserv-openwrt/commit/acd4dae)。补充提交仅改变 Windows 偏好、服务端能力缓存及对应回归/说明；已核对 Apple、Android、Linux 和 macOS 应用源码在两次提交间没有变化。

| 平台 | 本轮 GitHub 验证 |
|---|---|
| Windows x86/x64，acd4dae | [运行 40164066006](https://github.com/sck03/ocserv-openwrt/actions/runs/40164066006)：双架构编译、PE/源码包审计、UI 51 项与全部原生认证/配置/资源回归通过；x64 另通过 WFP 4、真实 Wintun 8 项 |
| Linux/macOS，da88316 | [运行 40162926261](https://github.com/sck03/ocserv-openwrt/actions/runs/40162926261)：Linux 双架构构建、四组 Debian 13/Ubuntu 26.04 安装与启动，以及 macOS Universal unsigned 全部通过 |
| Android，da88316 | [运行 40162928395](https://github.com/sck03/ocserv-openwrt/actions/runs/40162928395)：三种 ABI、完整策略 JUnit、debug APK、16 KB 对齐与签名验证通过 |
| iOS，da88316 | [运行 40162931026](https://github.com/sck03/ocserv-openwrt/actions/runs/40162931026)：地址/凭据/数据包及迟到网络设置完成回归、应用与扩展 unsigned archive 通过 |
| 服务端，acd4dae | [运行 40164069173](https://github.com/sck03/ocserv-openwrt/actions/runs/40164069173)：逻辑 74、守护 38、安装/POSIX 竞争 49、密码文件 9、真实证书和隔离网络、页面 18 与轮询 8 项通过；armsr/armv8、ramips/mt7621、x86/64、mediatek/filogic、ipq806x/generic、ath79/generic 的六架构 APK/ELF 审计、源码和安装套装打包全部通过 |

最终 Windows CI 的资源回归中，x64 私有内存保持 2,093,056 字节、句柄保持 118；x86 私有内存增加 81,920 字节、句柄保持 135。两者均处理 60 万事件并清空 1,000 轮。这些短时计数没有显示无界增长，但不替代实际流量和长期设备观测。

下载的测试附件按 GitHub artifact digest 和 ZIP CRC 校验，首轮与最终结果分别保存在 `artifacts/validation/2026-10-25-maintenance/` 和其 `final/` 子目录。两轮本地清理累计移除约 250 MiB 构建及 Python 生成物，并移除被忽略的旧 GitHub 本机助手，保留工具链、锁定上游归档、许可证、发行包和验证记录。新模块和历史导入/导出功能均核对实际引用；仍被 CMake 导出元数据引用的 zlib 库文件不作为无效文件删除。

N1 的最终 `aarch64_generic` 套装已下载至 `dist/`，核对外层 artifact digest、ZIP CRC 和内部 19 项 SHA-256；实际 BUILDINFO 为 OpenWrt SDK 25.12.6、ocserv 1.5.0-r7、管理页 0.5.5-r1、提交 acd4dae。六架构包审计附件也分别校验后保存。Windows 本地最终便携包及对应源码分别为 `dist/Lansyra-0.7.4-windows-x64.zip` 和 `dist/Lansyra-0.7.4-source.zip`；已验证新模块在源码包中与工作区逐字节一致，包不携带本机辅助工具或虚拟环境。本轮全部构建关闭 Release 发布；Android 为 debug、Apple 为 unsigned，不等同于设备安装或长时间转发验收。

## 2026-10-25 事务恢复、取消与分流边界修复

本次源码版本为 Windows 0.7.3、Linux/macOS 0.1.3、Android 0.2.3、iOS 0.2.3、管理页 0.5.4-r1，ocserv 仍为 1.5.0-r7。最终应用提交为 [`9e867c1`](https://github.com/sck03/ocserv-openwrt/commit/9e867c1)，服务端和管理页回归对应 [`d08a7cb`](https://github.com/sck03/ocserv-openwrt/commit/d08a7cb)，Windows 原生回归对应 [`5a38f8a`](https://github.com/sck03/ocserv-openwrt/commit/5a38f8a)。修复服务端配置超限和中断恢复、Windows CA 并发导入和路由清理错误、IPv4 全隧道的 IPv6 防绕行、管理页旧刷新覆盖、Android 取消及错误丢失、数字网络分流，以及桌面端保存失败的一致性。新增独立的持久化、网络和会话策略模块，数据格式与用户凭据上下文保持不变。

本地已通过 Windows x64 指定 MSVCRT 工具链编译，连接 61、配置/DPAPI 70、界面 29、真实 WSH 7、资源 9、回环 TLS 认证 35 项。资源测试处理 60 万事件和 1,000 次清空，句柄保持 134，私有内存保持不变；这是短时队列测试的计数。真实 Chrome 界面 18、轮询 8 项通过，无脚本错误，包含延迟刷新、失败旧响应和保存竞态。服务端 Lua 逻辑 67、守护事务 37、命令边界 9、权限 6 项通过，新增超限拒绝、中断多文件恢复、后续编辑保护和恢复失败重试。Android 的 28 项本地 JVM 策略测试通过，数字地址辅助使用 Android 8 分支的测试桩；它不替代 Android 工程构建和真机。

GitHub Windows 运行 [`40139987024`](https://github.com/sck03/ocserv-openwrt/actions/runs/40139987024) 通过 x86/x64 编译、PE 审计、两架构原生回归；x64 另通过 4 项动态 WFP 策略和 8 项真实 Wintun 隧道检查。Linux/macOS 运行 [`40133609941`](https://github.com/sck03/ocserv-openwrt/actions/runs/40133609941) 通过 Linux 双架构安装/启动和 macOS Universal unsigned；iOS 运行 [`40133614926`](https://github.com/sck03/ocserv-openwrt/actions/runs/40133614926) 通过 unsigned archive；Android 运行 [`40134242522`](https://github.com/sck03/ocserv-openwrt/actions/runs/40134242522) 通过 debug APK、策略 JUnit、16 KB 对齐和签名检查；服务端六架构运行 [`40137068399`](https://github.com/sck03/ocserv-openwrt/actions/runs/40137068399) 通过逻辑 67、守护 37、浏览器 18、轮询 8 和六架构 APK/ELF/源码打包。所有本地下载的报告和附件均按 GitHub artifact digest、ZIP CRC 或 SHA256SUMS 校验后保存到 `artifacts/validation/2026-10-25/`。当前记录不声称真实 N1、手机、Apple 签名扩展或 24–72 小时运行已经验收。

## 2026-10-24 维护修复与模块化

本次版本为 Windows 0.7.2、Linux/macOS 0.1.2、Android 0.2.2、iOS 0.2.3、管理页 0.5.3-r1。修复配置总量上限、日志级别切换、脚本取消、Apple 重试与错误展示、Linux 断开失败、Android 会话归属与配置保护，以及服务端防火墙事务和首次安装中断恢复。模块和数据契约见 [ARCHITECTURE.md](ARCHITECTURE.md)。

本地已执行服务端逻辑 62 项、控制器权限 6 项、守护事务 37 项、配置生成 4 项、命令边界 9 项，以及真实 Chrome 管理页 18 项、轮询 6 项；浏览器无脚本错误。Windows x64 使用文档中的 MSVCRT 工具链完整编译，连接模型 60 项、配置/DPAPI 64 项、界面 29 项、真实 WSH 7 项、资源 9 项及回环 TLS 认证 35 项通过。脚本取消回归确认子进程被结束；资源回归的句柄保持 134，内存约增加 96 KiB。该计数只代表本次测试进程，不能推断整机长期占用。构建参数、发布元数据、客户端/移动端/服务端打包回归亦通过。当前未连接真实 N1 或手机。

主要应用代码提交为 [`6112121`](https://github.com/sck03/ocserv-openwrt/commit/6112121)。服务端安装测试首次运行发现模拟器没有识别新的独立 UCI 暂存目录，修正提交为 [`6fdf0a6`](https://github.com/sck03/ocserv-openwrt/commit/6fdf0a6)，应用运行代码未变。Windows 后续提交 [`a95dbf2`](https://github.com/sck03/ocserv-openwrt/commit/a95dbf2) 统一长中文路径及网关的 UTF-16 校验与 UTF-8 读取预算，并拒绝规范化后超限的网关，新增两项配置回归。

| 平台 | 本次成功验证 |
|---|---|
| Windows x86 / x64 | [运行 40103114903](https://github.com/sck03/ocserv-openwrt/actions/runs/40103114903)：提交 a95dbf2 的两架构编译、PE 审计、便携包中英文启动、配置/DPAPI 64 项及界面/脚本/资源/回环认证全部通过；x64 额外通过 8 项真实 Wintun 隧道检查，包括活动连接中提高日志级别 |
| macOS / Linux | [运行 40099858504](https://github.com/sck03/ocserv-openwrt/actions/runs/40099858504)：macOS Universal unsigned、Linux 双架构构建与认证 IPC、四组 Debian/Ubuntu 安装和启动检查全部通过 |
| Android | [运行 40099860914](https://github.com/sck03/ocserv-openwrt/actions/runs/40099860914)：上游及新增会话/存储/脱敏策略 JUnit、三个 ABI 核心、debug APK、16 KB 对齐和签名检查通过 |
| iOS | [运行 40099863090](https://github.com/sck03/ocserv-openwrt/actions/runs/40099863090)：地址、凭据、数据包校验及应用/扩展 unsigned 编译归档通过，保留 iOS 15 部署目标并为最后断开错误 API 设置系统版本边界 |
| 服务端回归 | [运行 40100077842](https://github.com/sck03/ocserv-openwrt/actions/runs/40100077842)：逻辑 62、权限 6、事务 37、真实证书 14、安装恢复 35、密码文件 9、浏览器 18、轮询 6 项及 Linux 网络命名空间防火墙 20 项检查通过 |
| OpenWrt 六架构实包 | [运行 40101264796](https://github.com/sck03/ocserv-openwrt/actions/runs/40101264796)：armsr/armv8、ramips/mt7621、x86/64、mediatek/filogic、ipq806x/generic、ath79/generic 的 ocserv 1.5.0-r7、管理页 0.5.3-r1 APK 编译、ELF 审计、对应源码及安装套装打包全部通过，包含 N1 使用的 aarch64_generic |

最终 CI 中 Windows x64 的资源回归私有内存未增长、句柄保持 118；x86 私有内存增加 80 KiB、句柄保持 135。两者均处理 60 万事件并完成 1,000 次清空。已校验 SHA-256 和 ZIP CRC 的原始 JSON/日志摘要保存在本地 `artifacts/validation/2026-10-24-maintenance/`，Windows 最终结果位于其中的 `windows-a95dbf2/`，服务端六架构审计位于 `server-six-architecture/`。本地 0.7.2 x64 便携包已按 Unicode 修复重新生成，通过实际中英文启动与导入审计。两轮项目清理脚本累计移除约 425 MiB 生成物，保留发行包、备份、锁定源码、工具链和验证记录。本轮额外安装的临时 Python 测试工具目录因自动执行审查返回 `blocked by policy` 暂时保留，未进入 Git 或发行包。

本轮关闭自动发布，Android 使用 debug、Apple 使用 unsigned。上述验证不代替实际 N1/Flippy/OpenClash、手机后台耗电、Apple 签名扩展授权或数日转发验收。

## 2026-10-24 配置联动与资源边界修复

本次维护将管理页的 VPN 服务设置与项目维护的基础防火墙规则放入同一事务：地址池同步更新源 NAT，TCP/UDP 端口同步更新入口规则；fw4 检查或重载失败会恢复 ocserv 与防火墙配置。新增 Lua 防火墙模块并纳入服务端打包审计，补充地址池、双端口和防火墙漂移回归。

VPN 专用上网事务的截止时间改用设备单调运行时间，避免 NTP 校时改变自动恢复时机。macOS 连接启动后立即清除会话密码，Windows OpenConnect 核心日志级别跟随用户设置。管理页版本发布递增为 `0.5.2-r2`。

## 2026-10-23 证书初始化、固件架构和客户端导出设置

应用与服务端修复提交为 [`428f538`](https://github.com/sck03/ocserv-openwrt/commit/428f538)。服务端更新为 ocserv 1.5.0-r7、管理页 0.5.2-r1，本次自动选择官方 OpenWrt 25.12.6 SDK。N1 使用 `aarch64_generic` 套装；安装检查优先读取固件的 `/etc/apk/arch`，不会根据 Cortex-A53 CPU 名称推断软件包架构。

| 平台 | GitHub 验证 |
|---|---|
| OpenWrt | [运行 39916569037](https://github.com/sck03/ocserv-openwrt/actions/runs/39916569037)：六架构 APK 编译、ELF 审计、源码和安装套装打包全部通过 |
| Windows | [运行 39916573461](https://github.com/sck03/ocserv-openwrt/actions/runs/39916573461)：x86/x64 编译、精简便携包检查、原生运行回归全部通过 |
| Android | [运行 39916577363](https://github.com/sck03/ocserv-openwrt/actions/runs/39916577363)：指纹长度预检查、JUnit、三个架构核心、debug APK 和 16 KB 对齐检查通过 |
| iOS | [运行 39916584974](https://github.com/sck03/ocserv-openwrt/actions/runs/39916584974)：校验测试及应用/扩展 unsigned 编译通过 |
| macOS / Linux | [运行 39916581308](https://github.com/sck03/ocserv-openwrt/actions/runs/39916581308)：macOS 首次下载遭遇 DNS 解析失败，重跑后 Universal 编译通过；Linux 两架构及四组 Debian/Ubuntu 安装检查通过 |

服务端 Lua 逻辑 55、事务与日志 37、真实证书 14、安装事务 28、密码文件 9、浏览器 18、轮询 5 组检查通过。证书回归覆盖 OpenWrt 库读取未设置变量、局域网 IP/主机名验证、重复初始化、生成失败后的临时文件清理与重试；安装回归覆盖 APK 通用别名与 `/etc/apk/arch` 覆盖值不一致的情况。页面回归确认客户端地址和证书验证方式可保存，缺少证书时不会自动退回无固定指纹的配置。

本地实际 GnuTLS/OpenSSL、Lua、浏览器和便携包打包检查通过；POSIX 私钥权限及完整安装事务由 Linux CI 验证。已下载 N1 的 `aarch64_generic` 修复包，核对 GitHub artifact SHA-256、套装外部校验、ZIP CRC 和全部 17 个内部文件 SHA-256。摘要与原始回归附件保存在 `artifacts/validation/2026-10-23/`，N1 套装位于 `dist/`。

Windows 包移除重复的完整 WAN 文档，保留短说明、构建元数据和发行许可证；服务端移除未使用的 `enable` 默认项和空白账号占位。工作流后续提交 [`c0c0a81`](https://github.com/sck03/ocserv-openwrt/commit/c0c0a81) 优先复用运行器已有 Chrome，避免重复下载浏览器与字体依赖，缺少 Chrome 时保留 Chromium 安装回退。[验证运行 39935782663](https://github.com/sck03/ocserv-openwrt/actions/runs/39935782663) 中网页环境准备和测试步骤从首次运行的 364 秒减少至 18 秒；下载网络与运行器性能仍会波动。

安装器后续修复 [`cd28d26`](https://github.com/sck03/ocserv-openwrt/commit/cd28d26) 已在上述 Linux 验证运行中通过 **32 项安装事务检查**。同名服务端原地升级保留配置、证书、运行和开机启动状态；APK 更新前先停服务，更新后确认实际状态，失败时尝试恢复当前已安装服务的运行状态，不声称回滚 APK。预演与实际安装使用一致的 `--upgrade`，预检失败不会重启已有服务。新增 `validate_only` 模式仅验证脚本、服务事务、证书及页面，跳过编译和发布。

本地 `Lansyra-N1-aarch64_generic-ocserv-1.5.0-r7-ui-0.5.2-upgrade-cd28d26.zip` 在原始 CI 套装基础上更新安装工具及说明，两个 APK 与成功构建的原件逐字节一致。`BUILDINFO.json` 分别记录应用提交和安装器提交；已重新生成并验证完整清单及 ZIP CRC，SHA-256 为 `8af5284542fcae03427388686be7028afb6db83a03e60592e02054319565f435`。这份本地套装保留原来的官方 SDK/依赖信息，不代替 OPL 真机验收。

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

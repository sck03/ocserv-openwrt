# 布利杰VPN 0.3.0

原生 Win32 / C++17 的 OpenConnect 客户端，适配 ocserv 账号密码认证。参考 OpenConnect GUI 的连接流程重新实现界面；不包含 Qt，不启动脚本解释器，不要求用户安装 .NET、VC++ 可再发行运行库或 UCRT。

项目同时包含 **ocserv 1.5.0 的 OpenWrt 安装包配方**与**中文管理页**，支持账号添加、修改密码、停用、恢复、删除，常用网络设置、在线用户及客户端配置下载。

当前为**联调版本**：x86/x64 编译、原生界面、凭据管理、证书与登录验证已测试。Win7 实机、N1 上完整 VPN 转发、路由/DNS 回滚、长时间连接和吞吐量尚未验证。

## 下载已编译版本

[Releases 发行页](https://github.com/sck03/ocserv-openwrt/releases)提供 Windows x86/x64 客户端、OpenWrt 24.10.8 IPK 与 25.12.5 APK 服务端套装、对应源码和统一的 `SHA256SUMS.txt`。按系统架构和固件版本选择 ZIP，完整解压后使用；服务端套装包含 ocserv、原 LuCI 页面、中文翻译和布利杰管理页。

## 在 GitHub 手动构建

在仓库 Actions 中分别选择：

- [手动构建 Windows 客户端](https://github.com/sck03/ocserv-openwrt/actions/workflows/build-client.yml)：生成 x86/x64 客户端；可指定官方 OpenConnect 协议核心版本和源码 SHA-256，默认 9.21。
- [手动构建 ocserv 服务端与中文管理页](https://github.com/sck03/ocserv-openwrt/actions/workflows/build-server.yml)：可指定官方 ocserv 版本和源码 SHA-256，默认 1.5.0；选择构建 OpenWrt 24.x.x、25.x.x 或两者。

两套工作流独立，点击各自的 **Run workflow** 手动运行；全部构建与校验成功后，会自动在 [Releases](https://github.com/sck03/ocserv-openwrt/releases) 发布本次产物。客户端包含 x86/x64 ZIP 和对应源码 ZIP；服务端包含所选 SDK 的 ZIP 套装（内含源码）。每个 Release 都附带统一的 `SHA256SUMS.txt`。

自动发布沿用当前联调版本的 **Pre-release** 状态。标签分别为 `client-运行ID-尝试次数`、`server-运行ID-尝试次数`；每次运行或重跑生成独立条目，保留已有发行版。附件全部上传完成后才公开，下载链接也会写入本次运行的 Summary。

SDK 参数可填具体已发布版本，或填 `auto` 选择该系列最新稳定版。默认源码版本的 SHA-256 可留空使用仓库锁定值，**更换源码版本时须填对应校验值**。详情见 [上游版本更新](docs/UPSTREAM-UPDATES.md)。

所有任务使用标准 `ubuntu-24.04` 托管环境和运行于 Node.js 24 的官方稳定版 Actions；管理页脚本检查显式使用 Node.js 24 LTS。此公开仓库使用 GitHub 对公开项目提供的免费标准运行器。**Releases 附件长期保留**，运行底部的 **Artifacts** 另保留 7 天，供下载验证工具和检查结果：

| Artifact | 内容 |
|---|---|
| `windows-x64` / `windows-x86` | 对应架构的便携 ZIP、SHA-256、验证工具和 PE 审计 |
| `openwrt-24.x-aarch64_generic` | ZIP 套装和 SHA-256；内含所选 ocserv、原 LuCI 页、中文翻译和布利杰管理页的 IPK，及源码 |
| `openwrt-25.x-aarch64_generic` | ZIP 套装和 SHA-256；内含对应 SDK 的 APK，及源码 |
| `corresponding-source` | 客户端源码、构建脚本、锁定版本的第三方源码 |
| `server-regression-results` | 服务端逻辑与事务回滚检查结果 |

客户端在 Linux 交叉编译；构建完成不代替 Windows 7 或 N1 实机验收。服务端 SDK 默认 24.10.8 / 25.12.5，支持选择同系列其他已发布版本；目标为 `armsr/armv8、aarch64_generic`。默认校验值在 [sdks.json](server/openwrt/sdks.json)，其他 SDK 从官方下载索引取得并核验。OPL/Flippy 固件先按 [N1 安装说明](docs/OPENWRT-N1.md)检查用户态兼容性，并保留固件自己的 TUN 模块。

## 员工使用

1. 完整解压与 Windows 架构一致的程序包，运行 `布利杰VPN.exe`。
2. 点击 **导入配置**，选择管理员提供的 `.bvpn` 连接文件；或先填写服务器地址，再选择服务端下载的 `ca.pem`。文件选择器第二种文件类型为 CA 证书。
3. 填写账号、密码。勾选 **记住账号和密码** 后，点击连接会将凭据保存在当前 Windows 用户的凭据管理器中。
4. 点击 **连接**。创建虚拟网卡和设置网络需要管理员权限；程序会在需要时请求 Windows UAC 授权。
5. 连接成功后按钮变为 **断开连接**。右上角最小化按钮将窗口收进系统托盘；单击托盘图标还原，右键可以断开或退出。
6. 点击关闭按钮或托盘的退出时，程序先停止 VPN 并清理本次网络配置，再结束。

初始服务器地址为 `https://192.168.19.254:4443`，这是本项目部署示例，**不表示已验证该地址正在提供服务**。更新程序前先退出旧窗口。64 位 Windows 使用 x64 包，32 位 Windows 使用 x86 包；不要跨架构混用 `wintun.dll`。

## 已实现

- 简体中文/English 切换，三个等宽输入框，空账号/密码检查。
- 原创多尺寸程序图标、系统托盘、连接/取消/断开、关闭时清理。
- 连接时长和流量显示、复制脱敏诊断、断开后恢复保存的凭据；取消连接不会被旧状态消息覆盖。
- Windows Credential Manager 记住账号密码；配置文件没有明文密码。取消记住会删除当前连接已存的凭据。
- 凭据绑定服务器 URL 和信任配置；更换服务器或 CA/固定公钥后，不会自动填入原连接的密码。
- `.bvpn` 一次导入地址和 CA；也能直接导入 PEM/DER CA 证书。CA 只在本应用中使用，不写入 Windows 全局根证书库。
- 证书链、有效期、服务器名称验证；也支持管理员分发完整 SHA-256 公钥指纹。拒绝跨来源服务器跳转、缩短指纹和额外认证字段，避免向错误入口发送密码。
- 复用 OpenConnect 9.21 的 TLS/DTLS 协议实现；UDP 优先，UDP 不可用时保留 TLS 通道。
- IP Helper 配置路由/地址，Windows 自带 netsh 配置本次虚拟网卡的 DNS，WFP 动态过滤器保护 DNS 和未提供 IPv6 隧道时的 IPv6 流量。
- 主程序静态合并 OpenConnect、OpenSSL、libxml2、zlib 及编译器运行库；只额外附带官方签名的 Wintun 组件。

## 兼容范围

| 项目 | 状态 |
|---|---|
| Windows 7 SP1 x86/x64 | 编译/API 兼容目标，需实机测试；需完整的 SHA-2 与安全加载更新 |
| Windows 8 / 8.1 / 10 / 11 x86/x64 | 按相应操作系统支持的架构构建；目前在 Windows 10 内核 17763 环境做了本地验证 |
| Windows Server 对应版本 | 需具备桌面、WFP、证书及网络组件；尚未实机验证 |
| Windows XP/Vista、Windows RT、Windows ARM 原生包 | 不在当前交付范围 |
| macOS | 本次用户要求的实现范围为 Windows，未制作 macOS 客户端 |

“接近零依赖”指用户不另装编程语言/GUI/VC 运行环境。整机 VPN 仍需虚拟网卡驱动和管理员权限；`wintun.dll` 本身包含驱动部署逻辑。应用 EXE 当前未进行商业代码签名，Wintun 文件使用官方签名且保持原样。

## 管理与开发资料

- [客户端配置、CA 导入与密码保存](docs/CLIENT.md)
- [你的 N1 与 OpenWrt 24.10 / 25.12 升级说明](docs/OPENWRT-N1.md)
- [中文管理页的使用与安装](docs/SERVER-UI.md)
- [从源码构建](docs/BUILD.md)
- [验证记录和待验收项目](docs/VALIDATION.md)
- [第三方组件与源码](THIRD-PARTY-NOTICES.md)

源代码依 GPL-3.0-or-later 提供；上游组件分别遵守各自许可证。项目中的 OpenWrt 配方和参考源码具有其原始许可证。

# Android / iOS 客户端

移动端继续使用 **OpenConnect / Cisco AnyConnect 协议**，连接本项目的 ocserv；不使用 OpenVPN 的 `.ovpn` 协议。Windows 客户端与服务端构建流程保持独立。

## GitHub Actions 构建

提交这些文件到 GitHub 的默认分支后，在 **Actions → 对应工作流 → Run workflow** 执行。两个工作流均为手动触发，仅上传保留 7 天的 artifacts，不自动发布 Releases。下载 artifact 后解压可获得客户端、`BUILDINFO.json`、`SHA256SUMS.txt` 和 `corresponding-source.zip`。

| 平台 | 工作流 | 默认产物 | 签名产物 |
| --- | --- | --- | --- |
| Android 8.0+ | `build-android.yml` | 可安装的 debug APK | release APK |
| iOS / iPadOS 15+，arm64 真机 | `build-ios.yml` | 未签名 `.xcarchive.zip`，用于编译验证，不能直接安装 | Ad Hoc IPA，仅限描述文件登记的设备 |

Android 同时编译 `arm64-v8a`、`armeabi-v7a`、`x86_64` 并打入一个 APK。iOS 使用 macOS/Xcode 编译应用和 Packet Tunnel 扩展；不生成伪装成可安装包的未签名 IPA，不自动提交 App Store。

## Android

Android 基于 **OpenTunnel** 固定提交 `0535533dfb7d3a656bfdb880d51f731c109135c1`，保留上游界面与连接功能。本仓库设置独立安装包 ID `com.bulijie.vpn` 和中文应用名，界面仍以上游英文/波斯文为主；不是 Windows 界面的完整中文移植。

新建服务器，填写 `https://域名:4443`、用户名、密码，使用 AnyConnect 协议。首次证书确认必须核对管理员提供的公钥指纹。允许系统 VPN 授权后连接。不直接导入 Windows `.bvpn` 文件。

本仓库加强了上游行为：保存的证书指纹必须是完整、规范的 `pin-sha256`；固定公钥变化时拒绝连接，需向管理员核实后手动更新。已配置指纹优先于系统/自定义 CA。失败后不自动降低 TLS 安全等级；旧服务器兼容模式仅由用户明确选择。应用禁用 Android 系统备份，避免配置和凭据随系统备份迁移。三种架构的 ELF 和 APK 均检查 16 KB 内存页兼容性。

默认 `build_type=debug` 无需 Secrets，适合测试。GitHub 临时运行器每次可能生成不同的 debug 密钥；不能保证覆盖安装前一次 debug 包，必要时先卸载。正式使用选择 `release`，在仓库 **Settings → Secrets and variables → Actions** 配置：

| Secret | 内容 |
| --- | --- |
| `ANDROID_KEYSTORE_BASE64` | 自有 JKS/keystore 文件的单行 Base64 |
| `ANDROID_KEYSTORE_PASSWORD` | keystore 密码 |
| `ANDROID_KEY_ALIAS` | 签名密钥别名 |
| `ANDROID_KEY_PASSWORD` | 密钥密码；留空使用 keystore 密码 |

请长期备份同一签名密钥以支持更新安装。release 缺少签名资料会失败，不会退回 debug 签名。工作流验证 APK 签名后才上传产物。

## iOS

iOS 为本仓库的原生 SwiftUI 界面和 Objective-C `NEPacketTunnelProvider`，使用公开的 `packetFlow` API 和 socketpair 对接 OpenConnect，不读取系统私有 TUN 文件描述符。

填写服务器、用户名、密码和管理员预先核实的完整 `pin-sha256:Base64` 公钥指纹，然后连接并允许系统添加 VPN 配置。必须核实服务器指纹；指纹不匹配不提交密码。密码仅随本次连接传递，不保存到偏好设置，进程退出后再次连接需要重新输入。

首版支持 ocserv 用户名/密码认证、IPv4/IPv6 数据通道、服务端 DNS、全隧道及 OpenConnect 的 TLS/DTLS。服务端必须分配 IPv4 地址和 DNS。服务端未分配 IPv6 时仍将 IPv6 导入隧道，避免绕过 VPN。分流规则暂不使用。尚不支持交互式 MFA、分组选择、客户端证书、自动登录及 `.bvpn` 导入；遇到额外认证字段会取消，不能拿密码填充验证码。

iOS 要求服务器 MTU 至少为 1280。收包使用最多 32 个数据包的批次，避免持续流量阻塞断开操作；校验 IP 版本、长度和 IPv6 前缀。启动尚未完成时请求停止，也会等待资源清理完成；断开过程中禁用重新连接。

`signing=unsigned` 无需 Apple 账户，生成带应用及扩展的未签名 Xcode archive，供编译验证和后续 Xcode 签名。

要直接得到可安装 IPA，需 Apple Developer Program、Apple Distribution 证书和 **两个 Ad Hoc 描述文件**。分别为 `bundle_id` 和 `bundle_id.tunnel` 注册 App ID，开启 Network Extensions / Packet Tunnel 能力，两个描述文件都包含测试设备 UDID。配置：

| Secret | 内容 |
| --- | --- |
| `IOS_TEAM_ID` | Apple Developer Team ID |
| `IOS_CERTIFICATE_BASE64` | 含私钥的 `.p12` 文件，单行 Base64 |
| `IOS_CERTIFICATE_PASSWORD` | `.p12` 导出密码 |
| `IOS_APP_PROFILE_BASE64` | 主应用 Ad Hoc `.mobileprovision`，单行 Base64 |
| `IOS_TUNNEL_PROFILE_BASE64` | Packet Tunnel 扩展 Ad Hoc `.mobileprovision`，单行 Base64 |

运行工作流选择 `signing=signed`，`bundle_id` 必须与自有 App ID 一致。脚本检查 Team、App ID、有效期、设备列表和 Packet Tunnel 权限，再签名及导出 IPA。签名文件仅在临时目录使用，退出时清理描述文件和临时钥匙串，不进入 artifacts 或源码包。App Store/TestFlight 发布不在此工作流范围内。

## 源码与本地复现

`mobile/sources.json` 固定上游源码 URL、版本及 SHA-256；下载与缓存均校验，解包拒绝越界路径。Android 上游源码和 OpenConnect 9.21 / OpenSSL 3.5.8 / libxml2 2.15.3 / LZ4 1.10.0 的实际归档随产物提供；iOS 不使用 LZ4。Java/Maven 依赖由固定的上游 Gradle 版本目录解析。构建不是位级可复现承诺，Xcode、SDK 和包管理器的辅助工具版本仍受运行器环境影响。

Android 使用 Linux、JDK 17、Android SDK 35、NDK `27.2.12479018`、CMake 和 autotools：

```sh
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.2.12479018"
bash scripts/build-android.sh debug
```

iOS 使用 macOS、Xcode、XcodeGen、CMake、pkg-config 和 autotools：

```sh
export BVPN_BUNDLE_ID=com.bulijie.vpn
bash scripts/build-ios.sh unsigned
```

脚本要求 `build/mobile/android` 或 `build/mobile/ios` 尚不存在，避免复用已修改的解包源码；再次构建请自行移走对应构建目录。保留 `.tools/mobile-downloads` 可复用已校验下载。

## 验证范围

`python3 tests/mobile_build_tests.py` 在 Windows/Linux/macOS 检查下载完整性、缓存篡改、解包路径、ELF 架构/页对齐和 Apple 描述文件校验。Android 工作流另外执行上游及本地证书策略单元测试、APK 架构/16 KB 对齐和签名验证；iOS 工作流运行 `tests/mobile_protocol_tests.c`，再编译并归档两个 target。

当前新增代码未在本地 Windows 环境进行 Android NDK / Xcode 完整编译，也未进行手机实机验收。首次 GitHub 构建和实机验收仍必须确认：证书错误时拒绝认证、正确连接、DNS/IPv4/IPv6 路由、Wi-Fi/蜂窝切换、锁屏、断开恢复、错误密码和签名安装。不能将源码/脚本检查等同于已验证的手机连接结果。

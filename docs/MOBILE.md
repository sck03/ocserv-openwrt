# Lansyra Android / iOS 客户端

Lansyra 移动端当前版本为 **Android 0.4.0 / iOS 0.3.0**，使用 **OpenConnect / Cisco AnyConnect 协议**连接本项目的 ocserv。Android / iOS 默认安装标识为 `io.github.sck03.lansyra`。Android 使用 schema 4 的独立配置/设置文件和加密命名空间，不增加旧格式迁移层；旧格式请重新导入公共配置并填写凭据。iOS 与 macOS 共用 `client/apple` 中的地址、数据包与会话代码；桌面使用方法见[桌面说明](DESKTOP.md)。

## GitHub Actions 构建

提交这些文件到 GitHub 的默认分支后，在 **Actions → 对应工作流 → Run workflow** 执行。两个工作流均为手动触发，仅上传保留 7 天的 artifacts，不自动发布 Releases。应用附件只包含 APK、IPA 或未签名归档，以及 `BUILDINFO.json`、`SHA256SUMS.txt`。对应源码单独放在 `android-source` / `ios-source` 附件中，本地产物位于 `dist/android-source/` / `dist/ios-source/`；安装应用无需下载源码或测试报告，再分发时须提供匹配的对应源码和许可证。

| 平台 | 工作流 | 默认产物 | 签名产物 |
| --- | --- | --- | --- |
| Android 8.0+ | `build-android.yml` | 可安装的 debug APK | release APK |
| iOS / iPadOS 16+，arm64 真机 | `build-ios.yml` | 未签名 `.xcarchive.zip`，用于编译验证，不能直接安装 | Ad Hoc IPA，仅限描述文件登记的设备 |

Android 同时编译 `arm64-v8a`、`armeabi-v7a`、`x86_64` 并打入一个 APK。iOS 使用 macOS/Xcode 编译应用和 Packet Tunnel 扩展；不生成伪装成可安装包的未签名 IPA，不自动提交 App Store。

## Android

Android 使用隧道提供的 DNS。本项目服务端已移除客户端 DNS 分流，DNS/代理规则在路由器侧处理。取消正在准备的连接会将界面恢复为空闲。同一服务重复收到连接命令时复用当前会话，等待旧工作线程释放资源时使用通知唤醒。

连接生命周期、服务、配置仓库、模型、加密与界面适配直接维护在 `mobile/android/owned/`，构建脚本仅适配构建/标识并复制源码。配置和设置读取失败阻止连接；界面的错误占位不用于连接。排序只提交 ID，避免旧列表覆盖新指纹。Apple 扩展在认证、证书和重连回调内部及时释放临时 Foundation 对象；实际内存和耗电变化仍需设备测量。

Android 基于 **OpenTunnel** 固定提交 `0535533dfb7d3a656bfdb880d51f731c109135c1`。连接策略、存储、主要页面、文案与小组件由本仓库维护；中文、英文和波斯文可选择，也可跟随系统。界面名称统一为 **岚序 Lansyra**。

可在配置页导入管理端导出的版本 1 `.vpn`，或新建服务器。导入后填写自己的用户名和密码；公共文件不携带凭据。首次确认证书须核对管理员提供的公钥指纹，允许系统 VPN 授权后连接。格式和目标平台限制见[公共配置契约](CONNECTION-PROFILE.md)。

指纹必须是完整、规范的 `pin-sha256`，公钥变化或证书有效期不通过时拒绝连接。CA 与 Pin 互斥，指定 CA 失败不能转为手动接受。软件令牌按核心能力显示 TOTP / HOTP；当前 Android 不编译 STOKEN。HOTP 支持带计数的 otpauth URI、base32/hex 格式，在生成验证码前原子预留下次计数；保存失败停止认证，取消认证可能消耗一个计数，需要保留网关的重同步窗口。应用禁用系统备份，三种架构检查 16 KiB 内存页兼容性。

`0.1.1` 保留完整域名、IPv6 地址、端口和认证路径，由 OpenConnect 解析 A/AAAA 并按服务端 DDNS 设置重新解析，移除独立的 IPv4 DoH 缓存。隧道 MTU 不超过服务器协商值；重连后刷新 MTU、DNS 和路由，配置失败时结束连接。Android 8.0 起均支持 IPv4/IPv6 排除路由，并在分流路由中应用排除项。

Android `0.1.2` 根据服务器实际分配的隧道地址配置 IPv6 路由、DNS 和 MTU。服务器只分配 IPv4 时，即使“允许 IPv6”保持默认开启，也可使用小于 1280 的合法 IPv4 MTU；公网入口使用 IPv6 不会强制隧道内启用 IPv6。未协商的 IPv6 家族保持 Android VPN 的默认阻断行为。旧 IPv4 路由补集与地址转换工具函数已移除，路由统一由双栈策略处理。

Android 0.2.2 的会话归属保留到原生工作线程实际退出。取消超时会再次中止数据通道，不提前宣布已断开；旧回调不能清空新连接。指纹保存绑定发起连接的配置 ID 和服务器来源，配置已删除或换服务器时不再写回。跨服务器重定向不会自动提交原服务器的凭据。

Android 0.2.3 将原生会话发布、异步命令和释放纳入同一租约。会话尚未创建时的取消仍然有效；等待认证弹窗时也检查取消，避免错过取消后等待完整认证超时。核心主循环因网络、超时、认证或服务端断开而结束时保留错误提示。

网络分流只支持数字 IPv4/IPv6 地址或 CIDR，例如 `192.168.1.0/24`、`2001:db8::/64`。域名及通配符输入会报错；DNS 搜索域不能实现网站分流。“仅包含”网络列表为空或只包含未协商的地址族时也会报错。路由补集最多生成 2048 条，超限结束配置，避免规则膨胀占用过多资源。应用分流的“仅所选应用走 VPN”必须至少有一个仍已安装的应用；空列表或全部卸载时拒绝建立隧道，其他 Android 配置错误也不会被当成未安装应用忽略。

路由输入分别最多 4096 条。策略先将重叠和相邻规则归并为地址区间，再顺序扫描排除项并输出最小 CIDR；最终输出一旦超过 2048 条就停止。大量相邻主机规则可合并为一个网段，重复规则不会反复拆分剩余路由。

配置最多 128 项、整体数据最多 1 MiB。元数据与密文分离，列表不解密；连接只解密所选配置，未变项保留原密文。编辑时空密码/令牌保留原值，关闭保存选项会删除对应秘密。配置删除与当前选择在同一 DataStore 事务提交，界面和连接使用同一个选择来源。保存失败保留草稿；公共 `.vpn` 仅导出所选连接的公共字段。旧配置列表 JSON 不再接受。

更换主机或端口时清除继承的旧凭据及信任材料，本次重新填写的用户名、密码、认证分组、令牌、CA/Pin 和证书会保留。编辑意图不写入存储。列表、当前选择及错误提示共用一次元数据解析；只切换所选配置时不会重新解析整份 JSON 或解密凭据。

Android 12+ 使用系统提供的最佳非 VPN 网络回调；Android 8–11 结合默认网络与验证状态选择可用上游。网络和地址变化合并 150 ms 后处理，停止后的旧回调不能操作新会话；Wi-Fi/移动网络切换仍需真机验收。

CA、客户端证书和私钥通过文件选择器选择，保存时有界复制到应用私有目录，单文件最多 1 MiB；原始 `content://` URI 不传给 OpenConnect，也不需要长期保留提供者授权。文件只允许应用所有者读写。失败删除新副本，成功修改或删除配置后回收无引用文件；原生会话退出后才释放其证书租约。公共 CA 可随 `.vpn` 导入，客户端证书和私钥另行选择。

自动 MTU 使用核心协商值；DTLS 的 60 秒参数表示重试间隔，TLS 回退不等待该间隔。可设置最多三个自定义 DNS，并保留其 VPN 主机路由。Android 连接其他启用 DNS 分流的服务器时会明确拒绝；本项目不再下发该策略。DPD 覆盖关闭时采用协商值，开启时允许 1–86400 秒。没有运行行为的批处理、重复证书缓存、配置级分流、CSD 路径和 OpenSSL 套件字符串已移除；“Allow legacy ciphers”仅用于明确需要的外部旧网关，与配置文件版本兼容无关。

日志脱敏后进入界面/Logcat，限制单条 8 Ki 字符、总计 256 Ki 字符及 800 条。VPN 统计按需求启动：前台显示统计的页面为 1 秒；仅通知需要时亮屏 2 秒、熄屏 5 秒；仅已放置小组件需要时亮屏 5 秒。没有需求时停止统计定时器和 JNI 请求。日志/设置页不订阅隧道统计，恢复采样时重建速率基线。`SpeedHistory` 按实际时间分桶，快照绑定会话，迟到样本不能覆盖新连接。

应用流量页仅在可见、前台且未暂停时每 5 秒采样，查询窗口从监控器创建前一小时开始，避免反复扫描全部历史。查询失败、缺失 UID 和真实归零分别处理；失败保留累计值，恢复后重新建立速率基线。暂停/离页停止查询，返回后的累计值仍可包含间隔期间的流量。统计、重置和排序在后台执行，系统计数是估算值，共用系统计数的应用不能精确拆分，不能当作逐包 VPN 吞吐。

小组件的显示更新仍受亮屏 5 秒、熄屏 30 秒上限约束，连接状态变化立即更新。无通知需求时，熄屏不为小组件单独采样。数据面唤醒锁与统计独立，每 3 小时续期 6 小时租约，连接结束或服务销毁时释放；实际耗电仍以真机测量为准。

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

支持导入版本 1 的 Pin `.vpn`、ocserv 用户名/密码认证、双栈包含和排除路由、服务器 DNS/搜索域，以及 TLS/DTLS。Apple 还可处理外部服务器的 DNS 分流域名，本项目统一下发隧道 DNS。服务器须分配 IPv4 和 DNS。IPv4 全隧道未分配 IPv6 时保留防绕行保护。仍不支持交互式 MFA、分组选择、客户端证书和自动登录；额外认证字段会明确拒绝。

iOS 要求服务器 MTU 至少为 1280。收包使用最多 32 个数据包的批次，避免持续流量阻塞断开操作；校验 IP 版本、长度和 IPv6 前缀。启动尚未完成时请求停止，也会等待资源清理完成；断开过程中禁用重新连接。

共享 Apple 隧道代码将核心回调绑定到连接代次。应用网络设置超过 30 秒会结束核心，但必须等待 NetworkExtension 的实际完成回调后才开放下一次连接，防止旧地址、DNS 或路由晚到覆盖新连接；等待由异步屏障完成，不长期占用工作线程。非阻塞数据通道满时丢弃当前包，并在扩展诊断的 `dropped_to_core` 中记录饱和计数。

iOS 最低版本提高到 16，直接读取扩展返回的最后断开原因，显示证书、认证或网络错误，已删除 iOS 15 回退分支。旧连接的异步错误不会覆盖新连接状态。

`0.1.1` 的应用和隧道扩展共用 HTTPS 地址校验，支持带方括号的 IPv6，拒绝错误端口和含凭据的地址。重连后重新应用协商的 MTU、DNS 和远端地址；网络设置失败时结束连接。

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

`mobile/sources.json` 固定上游源码 URL、版本及 SHA-256；下载与缓存均校验，解包拒绝越界路径。Android 编译版本和 Apple 源码目录通过 `scripts/source-directory.py --mobile` 从同一清单解析，升级依赖不再需要手动修改 shell 脚本；归档名与目录不一致时停止构建。Android 上游源码和 OpenConnect 9.21 / OpenSSL 4.1.0 / libxml2 2.15.4 / LZ4 1.11.1 的实际归档随产物提供；iOS 不使用 LZ4。Java/Maven 依赖由本仓库 `mobile/android/libs.versions.toml` 固定；应用 Gradle 配置也直接维护，使用 AGP 内置 Kotlin、正式 compilerOptions 和校验过的 Gradle 9.9.1 分发包。构建不是位级可复现承诺，Xcode、SDK 和包管理器的辅助工具版本仍受运行器环境影响。

Android 使用 Linux、JDK 17、Android SDK 37.2（`platforms;android-37.2`，targetSdk 37）、Build Tools 37.0.0、NDK `30.0.16248370`、CMake 和 autotools：

```sh
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/30.0.16248370"
bash scripts/build-android.sh debug
```

iOS 使用 macOS、Xcode、XcodeGen、CMake、pkg-config 和 autotools：

```sh
export VPN_BUNDLE_ID=io.github.sck03.lansyra
bash scripts/build-ios.sh unsigned
```

脚本要求 `build/mobile/android` 或 `build/mobile/ios` 尚不存在，避免复用已修改的解包源码；再次构建请自行移走对应构建目录。保留 `.tools/mobile-downloads` 可复用已校验下载。

## 验证范围

历史版本 `7ec83b8` 于 2026-10-14 通过完整构建：[Android 0.2.1 debug](https://github.com/sck03/ocserv-openwrt/actions/runs/38513611697)和 [iOS 0.2.2 unsigned](https://github.com/sck03/ocserv-openwrt/actions/runs/38513614174)。本次维护修改了会话、存储和错误处理；当前版本的构建结果单独记录在 [验证记录](VALIDATION.md)，历史结果不代替本次验证。

`python3 tests/mobile_build_tests.py` 在 Windows/Linux/macOS 检查下载完整性、缓存篡改、解包路径、ELF 架构/页对齐和 Apple 描述文件校验；并发下载测试按文件身份核对路径，兼容 MSYS 的路径分隔符。Android 工作流另外执行上游及本地策略单元测试、4096 条路由规则与双栈边界回归、APK 架构/16 KB 对齐和签名验证；iOS 工作流运行 `tests/mobile_protocol_tests.c` 和 `tests/ios_connection_tests.m`（含迟到网络设置完成、屏障及重复完成回归），再编译并归档两个 target。

尚未进行手机实机验收，也未使用真实发布密钥验证 Android release 或 iOS Ad Hoc 签名导出。实机仍须确认：证书错误时拒绝认证、正确连接、DNS/IPv4/IPv6 路由、Wi-Fi/蜂窝切换、锁屏、断开恢复、错误密码和签名安装。编译及脚本检查不等同于已验证的手机连接结果。

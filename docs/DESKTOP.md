# macOS 与 Linux 桌面客户端

macOS 0.2.0 支持 macOS 13+，同一应用包含 Apple Silicon 和 Intel；Linux 0.3.0 支持 Debian 13 / Ubuntu 26.04 的 amd64、arm64。Windows 0.9.0 支持 Windows 10/11，见[Windows 使用说明](CLIENT.md)。

| 平台 | 界面与隧道 | 认证与密码 | 安装包 |
|---|---|---|---|
| macOS | SwiftUI、菜单栏、Network Extension 系统扩展；复用 iOS 的数据包与地址校验 | ocserv 用户名/密码、管理员预先核实的 pin-sha256；可选系统钥匙串 | Universal `.app.zip` |
| Linux | GTK 3、NetworkManager OpenConnect 插件；系统处理权限、路由和 DNS | 系统 OpenConnect 认证窗口，支持其认证分组、验证码及证书确认；可选系统钥匙串 | amd64 / arm64 `.deb` |

两端提供服务器配置、连接/取消、明确的连接状态、可选登录后连接、最多三次网络失败重试及诊断导出。取消会使旧操作失效，异步完成的旧连接会被清理；Linux 激活取消后即使只返回空结果，也回到空闲并允许再次连接。认证、证书和配置问题不自动重试。

当前界面名称统一为 **岚序 Lansyra**。macOS 本轮使用 schema 2 配置和带版本的钥匙串引用，不迁移旧配置，请新建或导入公共连接信息。Linux 由 NetworkManager 管理系统连接。macOS 使用 `io.github.sck03.lansyra.macos` 及匹配的 Apple 描述文件；Linux 使用 `lansyra` DEB。

## macOS

1. 将签名后的 `Lansyra.app` 放入 `/Applications`，启动应用。
2. 新建配置或导入服务端导出的固定指纹 `.vpn`；填写用户名、密码和管理员核实的完整 `pin-sha256`。当前 macOS 认证为用户名/密码；不将普通密码填入 MFA 字段。
3. 点击连接，按系统提示允许系统扩展和 VPN 配置。证书公钥和有效期不通过时拒绝登录。
4. 可选启用“在钥匙串中保存密码”。启用登录后自动连接前必须先保存有效配置和钥匙串密码；登录项可能还需要在系统设置中批准。

网络失败后重试时，应用重新从该配置的钥匙串获取密码；未保存密码时会请求重新输入，取消即可结束重试。启动请求结束后清除应用持有的本次密码，不将空密码发送给扩展。

macOS 与 iOS 共用版本 1 `.vpn` 导入器和服务器路由策略，支持双栈包含、排除及搜索域。对外部服务器仍保留 DNS 分流域名处理，本项目统一下发隧道 DNS。导入仅支持固定指纹和 AnyConnect，不支持的文件会明确报错。保存或连接已有配置保留原顺序，相同内容不会因排序重复写入。

macOS 配置最多 128 项，JSON 在读写两端限制为 1 MiB。保存先验证和归一化候选；同一服务器且密码未变化时复用现有钥匙串引用，配置也未变化时跳过文件写入。密码或服务器变化才创建绑定来源的新钥匙串版本，再原子提交权限为 0600 的配置引用，最后清理旧项。异常或中断不会改写旧配置引用的密码；中断可能留下未引用的钥匙串项。重试读取也通过后台存储 actor，不在主线程同步访问钥匙串。

原始 OpenConnect 日志不被保存。诊断文件包含产品版本、会话编号、状态代次、错误类别及有限的状态记录；不导出服务器地址、用户名、密码、Cookie 或认证表单。睡眠使用核心暂停命令，唤醒后恢复当前隧道；旧数据包回调携带代次，不能向新会话发送数据。

GitHub 工作流默认生成 **未签名编译产物**。未签名包用于检查两种 CPU 的编译和打包，不能据此宣称系统扩展已可用。实际连接需要 Apple 颁发、具备 Network Extension / System Extension 权限的签名及对应描述文件；脚本不绕过 macOS 安全设置。

在已安装 Xcode、Command Line Tools 和签名身份的 Mac 上：

```sh
brew install autoconf automake libtool pkg-config cmake xcodegen
python3 -m pip install Pillow
bash scripts/build-macos.sh unsigned
```

签名构建使用 `signed` 参数，并设置 `MACOS_TEAM_ID`、`MACOS_SIGN_IDENTITY`、`MACOS_APP_PROFILE`、`MACOS_TUNNEL_PROFILE`，以及与 Apple App ID 对应的 `VPN_BUNDLE_ID`。应用和扩展分别需要描述文件；扩展 ID 为应用 ID 加 `.tunnel`。对外分发仍须按 Apple 要求签名及公证。构建目录必须是新的目录，可先使用项目清理脚本清理上次构建。

## Linux

从指定 CA 切换到首次确认时，保存操作会同时清除旧 CA 路径及该模式设置的强制校验标志。后续连接由 NetworkManager 的 OpenConnect 认证助手按当前配置处理证书。

按实际 CPU 下载并全新安装对应包：

```sh
sudo apt install ./lansyra_0.3.0_amd64.deb
# ARM64 使用 lansyra_0.3.0_arm64.deb
```

依赖由 Debian/Ubuntu 软件源提供，包括 GTK、NetworkManager、OpenConnect 插件及认证窗口。桌面界面以普通用户运行，系统通过 Polkit 管理 VPN 权限。需要正在运行的 NetworkManager 和桌面会话；不作为无图形环境的命令行客户端使用。

DEB 只有几十 KB、解压后的可执行文件约一百 KB 可以是正常的：它包含本项目的原生界面和认证代理，GTK、VPN 核心、路由及 DNS 管理由系统库和插件提供，并非将这些组件静态合并的独立便携包。请使用上面的 `apt install ./…deb` 安装命令自动满足依赖；仅复制可执行文件不能替代安装。首次下载及安装的总空间取决于系统已有依赖，可用 `apt --simulate install ./lansyra_0.3.0_amd64.deb` 查看待安装的软件包。

可新建服务器，或在服务端选择 Linux 后导出版本 1 `.vpn` 并导入。Linux 支持 CA 与首次确认方式，不接受固定 Pin 文件；公共 CA 有界复制到本用户配置目录，导入取消或失败会清理新文件。连接时由系统 OpenConnect 窗口完成密码、分组、验证码和证书验证，配置 CA 时禁用手动跳过验证失败。保存密码由该系统窗口选择，诊断不收集其原始输出或系统日志。

0.1.5 修复认证完成后助手退出异常导致界面卡顿或进程残留的问题。收尾全程异步，超时自动终止助手并回收进程；收尾期间仍可取消连接。用户填写密码、分组或验证码的时间不受收尾超时限制。

登录后连接绑定启用时的服务器配置；关闭开关即删除本用户的登录启动项。钥匙串未解锁、证书需要确认或启用 MFA 时，仍需完成系统交互。网络切换、休眠与路由恢复由 NetworkManager 管理；只有网络中断或连接超时触发最多三次重试。

系统拒绝断开时，客户端显示原因并允许重试；退出请求不会把仍在运行的 VPN 当作已清理。编辑或删除弹窗期间，客户端保持所选系统配置的有效引用。

Linux 的连接控制器独立管理 NetworkManager 会话、认证代理、取消与重试，GTK 编辑器仅产生草稿。编辑从原连接克隆候选，保留未展示的路由、DNS、代理等设置；授权失败不改写原连接。重新打开会接管已有活动连接。成功修改或删除后回收无引用的受管 CA，提交候选显式保护新 CA，不依赖 libnm 缓存及时更新；外部证书保留。偏好和登录启动项在后台串行合并写入，退出等待完成。

从源码构建：

```sh
sudo apt install build-essential cmake ninja-build pkg-config python3 file \
  libgtk-3-dev libnm-dev libsecret-1-dev network-manager-openconnect-gnome \
  dbus-x11 xvfb xauth desktop-file-utils
bash scripts/build-linux.sh
```

发行包在 Debian 13 上分别原生构建两个架构，由 CPack / `dpkg-shlibdeps` 从实际 ELF 生成动态库依赖，并声明 NetworkManager 和 OpenConnect 认证插件。认证助手由运行时 libnm 根据当前安装的 OpenConnect 插件定位。DEB 只安装客户端、桌面入口、图标、说明和许可证；测试程序与截图单独归档。`lansyra --version` 可在无图形环境确认程序能加载，`lansyra --check-runtime` 检查并显示实际认证助手路径。诊断为 UTF-8 文本，包含客户端/NetworkManager 版本、会话状态、错误类别和最多 256 条状态记录。

## 构建与验证边界

[桌面工作流](https://github.com/sck03/ocserv-openwrt/actions/workflows/build-desktop.yml) 构建 Linux 两种架构及 macOS Universal 包，运行协议/取消/重试回归、Linux 图形启动检查、Apple 地址和数据包校验。Linux 还检查安装包实际文件、ELF 架构、符号裁剪和动态库加载，并在未预装开发库的 Debian 13 / Ubuntu 26.04 容器中用 APT 安装再启动。Linux、macOS 应用与 `linux-source`、`macos-source` 对应源码附件分开上传，本地源码输出为 `dist/linux-source/`、`dist/macos-source/`。源码和测试附件不属于客户端安装内容。产物保留 7 天；该工作流不发布 Release。

Windows 配置保存、导入、导出、删除和退出刷新在后台执行，处理期间窗口继续响应绘制和会话事件；一次只允许一个配置操作。macOS 的配置文件和钥匙串访问由串行存储 actor 执行，读取或保存期间禁用冲突操作。Linux 无法写入偏好时明确提示，重试选项会回到原值。各平台均不隐式迁移其他产品或旧格式数据。

自动构建、隔离窗口及协议测试不替代 macOS 签名扩展加载、真实 Linux VPN 转发和设备网络切换验收。实际运行证据以[验证记录](VALIDATION.md)注明的提交和平台为准。

macOS 与 iOS 共用诊断响应白名单和 1 秒超时。迟到、重复或跨连接代次的响应不会覆盖新会话报告；扩展无响应时仍保留已生成的客户端快照。

Linux 的 PEM 证书导入使用 GIO TLS 后端，运行及测试环境须安装 `glib-networking`；DEB 将其列为显式依赖。仅安装 GTK/libnm 开发包的构建环境仍可能缺少此运行时组件。

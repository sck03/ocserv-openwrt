# macOS 与 Linux 桌面客户端

桌面端均按全新安装提供。macOS 0.1.0 支持 macOS 13+，同一应用包含 Apple Silicon 和 Intel；Linux 0.1.0 支持 Debian 12 / Ubuntu 24.04 的 amd64、arm64。Windows 0.7.0 支持 Windows 10/11，见[Windows 使用说明](CLIENT.md)。

| 平台 | 界面与隧道 | 认证与密码 | 安装包 |
|---|---|---|---|
| macOS | SwiftUI、菜单栏、Network Extension 系统扩展；复用 iOS 的数据包与地址校验 | ocserv 用户名/密码、管理员预先核实的 pin-sha256；可选系统钥匙串 | Universal `.app.zip` |
| Linux | GTK 3、NetworkManager OpenConnect 插件；系统处理权限、路由和 DNS | 系统 OpenConnect 认证窗口，支持其认证分组、验证码及证书确认；可选系统钥匙串 | amd64 / arm64 `.deb` |

两端提供服务器配置、连接/取消、明确的连接状态、可选登录后连接、最多三次网络失败重试及诊断导出。取消会使旧操作失效，异步完成的旧连接会被清理。认证、证书和配置问题不自动重试。

## macOS

1. 将签名后的 `LinkoraVPN.app` 放入 `/Applications`，启动应用。
2. 新建配置或导入服务端导出的固定指纹 `.vpn`；填写用户名、密码和管理员核实的完整 `pin-sha256`。当前 macOS 认证为用户名/密码；不将普通密码填入 MFA 字段。
3. 点击连接，按系统提示允许系统扩展和 VPN 配置。证书公钥和有效期不通过时拒绝登录。
4. 可选启用“在钥匙串中保存密码”。启用登录后自动连接前必须先保存有效配置和钥匙串密码；登录项可能还需要在系统设置中批准。

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

按实际 CPU 下载并全新安装对应包：

```sh
sudo apt install ./linkora-vpn_0.1.0_amd64.deb
# ARM64 使用 linkora-vpn_0.1.0_arm64.deb
```

依赖由 Debian/Ubuntu 软件源提供，包括 GTK、NetworkManager、OpenConnect 插件及认证窗口。桌面界面以普通用户运行，系统通过 Polkit 管理 VPN 权限。需要正在运行的 NetworkManager 和桌面会话；不作为无图形环境的命令行客户端使用。

DEB 只有几十 KB、解压后的可执行文件约一百 KB 可以是正常的：它包含本项目的原生界面和认证代理，GTK、VPN 核心、路由及 DNS 管理由系统库和插件提供，并非将这些组件静态合并的独立便携包。请使用上面的 `apt install ./…deb` 安装命令自动满足依赖；仅复制可执行文件不能替代安装。首次下载及安装的总空间取决于系统已有依赖，可用 `apt --simulate install ./linkora-vpn_0.1.0_amd64.deb` 查看待安装的软件包。

新建服务器时填写 HTTPS 网关，可预填用户名，并选择公共 CA 文件。连接时使用系统 OpenConnect 窗口核对证书并完成密码、分组或验证码认证。Linux 的证书策略由该窗口和所选 CA 管理，不将 Windows `.vpn` 固定指纹文件当成 Linux 系统配置直接导入。保存密码由系统认证窗口选择，诊断导出不收集该窗口的原始输出或系统日志。

登录后连接绑定启用时的服务器配置；关闭开关即删除本用户的登录启动项。钥匙串未解锁、证书需要确认或启用 MFA 时，仍需完成系统交互。网络切换、休眠与路由恢复由 NetworkManager 管理；只有网络中断或连接超时触发最多三次重试。

从源码构建：

```sh
sudo apt install build-essential cmake ninja-build pkg-config python3 \
  libgtk-3-dev libnm-dev libsecret-1-dev network-manager-openconnect-gnome \
  dbus-x11 xvfb xauth desktop-file-utils
bash scripts/build-linux.sh
```

发行包在 Debian 12 上分别原生构建两个架构，由 CPack / `dpkg-shlibdeps` 从实际 ELF 生成动态库依赖，并声明 NetworkManager 和 OpenConnect 认证插件。DEB 只安装客户端、桌面入口、图标、说明和许可证；测试程序与截图单独归档。`linkora-vpn --version` 可在无图形环境确认程序能加载。诊断为 UTF-8 文本，包含客户端/NetworkManager 版本、会话状态、错误类别和最多 256 条状态记录。

## 构建与验证边界

[桌面工作流](https://github.com/sck03/ocserv-openwrt/actions/workflows/build-desktop.yml) 构建 Linux 两种架构及 macOS Universal 包，运行协议/取消/重试回归、Linux 图形启动检查、Apple 地址和数据包校验。Linux 还检查安装包实际文件、ELF 架构、符号裁剪和动态库加载，并在未预装开发库的 Debian 12 / Ubuntu 24.04 容器中用 APT 安装再启动。Linux、macOS 应用与 `linux-source`、`macos-source` 对应源码附件分开上传，本地源码输出为 `dist/linux-source/`、`dist/macos-source/`。源码和测试附件不属于客户端安装内容。产物保留 7 天；该工作流不发布 Release。

自动构建、隔离窗口及协议测试不替代 macOS 签名扩展加载、真实 Linux VPN 转发和设备网络切换验收。实际运行证据以[验证记录](VALIDATION.md)注明的提交和平台为准。

# 上游版本与 SDK 更新

Windows 协议核心默认 OpenConnect 9.21，服务端默认且最低为 ocserv 1.6.0。手动 Actions 只需填写正式源码版本号：ocserv 自动读取官网对应 `.tar.xz.sha256sum`；OpenConnect 自动下载归档及分离签名，使用固定官方主密钥指纹 `BE07D9FD54809AB2C4B0FF5F63762CDA67E2F359` 校验后计算 SHA-256。默认版本复用仓库锁定值。解析只在验证任务中执行一次，构建与源码包共用结果，实际下载仍校验摘要。不存在、预发布、低于最低版本或签名不匹配的源码不会进入编译。

本地可运行 `python3 scripts/prepare-build.py server --version 1.6.0` 或 `python3 scripts/prepare-build.py client --version 9.21`。脚本修改当前检出的配方/源码清单；切换其他 OpenConnect 版本需要 GnuPG。内部参数 `--resolved-sha256` 仅供同一工作流的后续任务复用已解析结果。

## ocserv 1.6.0 基线

官方于 2026-10-16 发布 1.6.0。本项目按全新安装配置，不迁移 1.5 的事务、旧地址池格式或旧规则。删除关闭测试的源码补丁，使用正式 Meson 选项 `-Dtests=false`。

- IPv6 隧道服务端地址现在是 `ipv6-network` 的网络地址，例如 `fd77::/64` 对应 `fd77::`。自行设置隧道 DNS 或脚本时应使用该地址。普通局域网网关和公网服务器地址不受这项变化影响。
- IPv4 `/31` 和 IPv6 `/127` 是新增的点对点分配能力；管理页仍面向多用户地址池，不自动改为点对点部署。
- TLS 1.3 的 `rekey-method=ssl` 使用 KeyUpdate；当前 OpenConnect/GnuTLS 处理协议更新，客户端未写死服务端 IPv6 `::1`，无需增加地址修补或迁移代码。
- 默认用户/组配置文件缺失或错误现在会拒绝相应会话；本项目使用生成的标准 plain 认证文件。RADIUS 和 OIDC 的上游变化不改变默认账号流程；浏览器 OIDC 尚未在本项目客户端启用。

VPN 专用代理模式仍提供 IPv4 隧道并阻止通过 N1 的 IPv6 绕行，外层 VPN 连接可使用 IPv6。三款代理的配置和适用范围见 [专用上网](VPN-ONLY-PROXY.md)。

## OpenWrt SDK

项目支持 **25.12.x / APK**，为 `server/openwrt/targets.json` 中的六个目标分别构建；完整列表见 [OpenWrt 多架构说明](OPENWRT.md)。

服务端工作流只有一个 SDK 输入 `sdk_version`：

- `auto`（默认，留空同样适用）：按数字版本选择 25.12 系列最新稳定补丁版，不跨发行系列，不选择 RC 或 snapshot。工作流只解析一次版本，六个目标使用同一版本。
- 已经正式发布的 `25.12.x`（例如 `25.12.5`）：固定到指定版本，便于复现既有构建。

所有目标统一从官方下载目录解析 SDK 和唯一的 SHA256SUMS 项，下载后验证归档校验值；不再维护单独的 armsr SDK 地址和版本副本。

本地 Linux 构建：

```sh
python3 scripts/fetch-sdk.py --version auto --target armsr/armv8 --output /tmp/sdk-25.12
bash server/tools/build-ocserv.sh /tmp/sdk-25.12
python3 scripts/package-server.py /tmp/sdk-25.12
```

`--target` 可换成目标清单中的其他 target/subtarget，省略时默认为 armsr/armv8。SDK 必须放在源码 Git 仓库之外。构建记录保留目标、SDK 校验值、feed 提交、源码校验与 ELF 依赖。内核 TUN 使用设备固件自己的实现，不用官方 SDK 内核模块替换。

构建工具链宿主使用 `ubuntu-26.04`，这是 GitHub 运行器系统，与 OpenWrt 版本选择无关。升级源码、SDK 或 LuCI feed 后，应重新运行本地和 CI 检查，并完成设备上登录、转发、DNS、重连与开关恢复验证。

## 工作流稳定版本（2026-10-05 核对）

官方 Actions 使用稳定发布标签：checkout 7.0.1、setup-python 7.0.0、setup-node 7.0.0、setup-java 6.0.1、cache 6.1.0、upload-artifact 7.0.1、download-artifact 8.0.1。通过各 Action 仓库的 Releases 核对，升级时排除预发布版。

显式配置的 Python 使用 3.14，JavaScript 检查使用 Node.js 26；`check-latest` 获取该系列最新补丁。管理页浏览器回归使用 Playwright 1.63.0，Lua 回归使用 lupa 2.8。Android JDK 17、NDK 和系统运行器保持现有构建兼容要求。

2026-10-28 核对的官方核心为 [OpenConnect 9.21](https://www.infradead.org/openconnect/download/) 和 [ocserv 1.6.0](https://www.infradead.org/ocserv/download/)。[OpenWrt SDK](https://downloads.openwrt.org/releases/) 继续在每次运行时解析 25.12 系列最新稳定版。填写未来版本只负责可靠地选取官方源码，仍须通过项目的编译、测试和实际设备验收。

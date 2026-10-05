# 上游版本与 SDK 更新

Windows 协议核心默认 OpenConnect 9.21，服务端默认 ocserv 1.5.0。手动 Actions 中可以填写新的官方版本号；更改源码版本时须同时提供对应官方归档的 SHA-256。默认版本留空校验值时使用仓库锁定记录。脚本拒绝路径、命令片段和低于当前最低 API 要求的版本。

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

构建工具链宿主使用 `ubuntu-24.04`，这是 GitHub 运行器系统，与 OpenWrt 版本选择无关。升级源码、SDK 或 LuCI feed 后，应重新运行本地和 CI 检查，并完成设备上登录、转发、DNS、重连与开关恢复验证。

## 工作流稳定版本（2026-10-05 核对）

官方 Actions 使用稳定发布标签：checkout 7.0.1、setup-python 7.0.0、setup-node 7.0.0、setup-java 6.0.1、cache 6.1.0、upload-artifact 7.0.1、download-artifact 8.0.1。通过各 Action 仓库的 Releases 核对，升级时排除预发布版。

显式配置的 Python 使用 3.14，JavaScript 检查使用 Node.js 26；`check-latest` 获取该系列最新补丁。管理页浏览器回归使用 Playwright 1.63.0，Lua 回归使用 lupa 2.8。Android JDK 17、NDK 和系统运行器保持现有构建兼容要求。

本次核对的官方最新核心仍为 [OpenConnect 9.21](https://www.infradead.org/openconnect/download/)、[ocserv 1.5.0](https://www.infradead.org/ocserv/download/) 和 [OpenWrt 25.12.5](https://downloads.openwrt.org/releases/)。源码归档继续使用 SHA-256 锁定；不自动替换协议核心或随意跨工具链版本。

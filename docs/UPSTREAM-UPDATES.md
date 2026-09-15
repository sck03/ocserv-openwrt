# 上游版本与 SDK 更新

Windows 协议核心默认 OpenConnect 9.21，服务端默认 ocserv 1.5.0。手动 Actions 中可以填写新的官方版本号；更改源码版本时须同时提供对应官方归档的 SHA-256。默认版本留空校验值时使用仓库锁定记录。脚本拒绝路径、命令片段和低于当前最低 API 要求的版本。

## OpenWrt SDK

项目仅支持 **25.12.x / armsr/armv8 / aarch64_generic / APK**。

服务端工作流只有一个 SDK 输入 `sdk_version`：

- `25.12.5`：默认锁定版本，校验值见 `server/openwrt/sdks.json`。
- 其他已经正式发布的 `25.12.x`：从官方下载目录解析 SDK 和唯一的 SHA256SUMS 项。
- `auto`：按数字版本选择 25.12 系列最新稳定补丁版，不跨发行系列，不选择 RC 或 snapshot。

本地 Linux 构建：

```sh
python3 scripts/fetch-sdk.py --version 25.12.5 --output /tmp/sdk-25.12
bash server/tools/build-ocserv.sh /tmp/sdk-25.12
python3 scripts/package-server.py /tmp/sdk-25.12
```

SDK 必须放在源码 Git 仓库之外。构建记录保留 SDK 校验值、feed 提交、源码校验与 ELF 依赖。OPL/Flippy 的内核 TUN 使用固件自己的实现，不用官方 SDK 内核模块替换。

构建工具链宿主使用 `ubuntu-24.04`，这是 GitHub 运行器系统，与 OpenWrt 版本选择无关。升级源码、SDK 或 LuCI feed 后，应重新运行本地和 CI 检查，并完成设备上登录、转发、DNS、重连与开关恢复验证。

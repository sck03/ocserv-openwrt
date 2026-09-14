# 手动选择新的官方源码版本

客户端和服务端分别使用独立的 GitHub Actions 工作流，都只在手动点击 **Run workflow** 时运行。构建所选 Git 分支的本项目代码，并使用本次输入选择上游协议核心。

| 工作流 | 版本参数 | 默认值 | 对应官方源码 |
|---|---|---|---|
| 手动构建 Windows 客户端 | `openconnect_version` | `9.21` | [OpenConnect 发布源码](https://www.infradead.org/openconnect/download/)的 `openconnect-版本.tar.gz` |
| 手动构建 ocserv 服务端与中文管理页 | `ocserv_version` | `1.5.0` | [ocserv 发布源码](https://www.infradead.org/ocserv/download/)的 `ocserv-版本.tar.xz` |

默认版本可留空 SHA-256，使用仓库已经锁定的校验值。换成其他官方发布版本时，同时填写对应的 `openconnect_sha256` 或 `ocserv_sha256`。SHA-256 是 64 位十六进制字符。下载和核验官方源码后，也可用下面命令读取文件校验值：

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath .\ocserv-版本.tar.xz
```

Linux 对应 `sha256sum ocserv-版本.tar.xz`。客户端填写的是 OpenConnect 官方核心的版本，**布利杰VPN 界面版本**由 `CMakeLists.txt`、`src/common.h` 与 `resources/app.rc` 的项目源码维护，两者含义不同。

版本参数作用于当次构建。生成的对应源码包会包含当次选择后的配置、源码与校验值，`BUILDINFO.json` 记录实际版本。长期修改默认版本时，同步更新工作流的默认值及：

- 客户端：`scripts/sources.json` 中 OpenConnect 的文件名、官方 URL、SHA-256。OpenSSL、libxml2、zlib 的源码目录也从这个清单解析。
- 服务端：`server/openwrt/ocserv/Makefile` 的 `PKG_VERSION`、`PKG_HASH`；打包脚本从配方读取版本，不固定输出 1.5.0 文件名。

当前最低支持 OpenConnect 9.21、ocserv 1.5.0。新版本须保持所用 API、认证流程和构建方式兼容；上游若改动这些内容，可能需要更新配方或代码。编译错误、缺少源码校验值、校验失败、非预期的 DLL 或缺少安装包都会使构建失败，不会上传为成功产物。兼容目标仍需 Windows 7 与 N1 实机验证。

## SDK 的 24.x.x / 25.x.x 选择

服务端工作流独立提供：

- `series`：`all`、`24` 或 `25`；
- `sdk_24_version`：具体 24.x.x 稳定发行版，默认 `24.10.8`；
- `sdk_25_version`：具体 25.x.x 稳定发行版，默认 `25.12.5`。

SDK 版本也可填 `auto`，按数字版本排序选择该系列最新稳定版；不会隐式选择 RC 或 SNAPSHOT。脚本从 OpenWrt 官方下载索引定位 `armsr/armv8` 的 Linux SDK 并验证官方 SHA-256。产物按 SDK 实际启用的包管理格式打包，包含具体 SDK 版本与依赖记录。

这些参数保持 N1 所需的 `aarch64_generic` 用户态目标，并不表示任意 N1 固件的 ABI 都已实测兼容。固件升级后选择新固件对应的 SDK；TUN 内核模块使用该固件提供的版本。OPL/Flippy 的部署检查见 [N1 说明](OPENWRT-N1.md)。

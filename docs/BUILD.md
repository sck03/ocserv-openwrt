# 构建

## GitHub Actions（全部使用 Linux）

仓库 Actions 有两套独立工作流：**手动构建 Windows 客户端**与**手动构建 ocserv 服务端与中文管理页**。选择所需工作流 → Run workflow → main。全部构建与校验成功后自动发布到 Releases。不需要额外配置仓库 Secret，发布任务使用自动提供的 `GITHUB_TOKEN`，仅该任务授予 `contents: write`；没有 push、pull_request 或定时触发器。标准 `ubuntu-24.04` 运行器可用于这个公开仓库的免费构建，Release 附件长期保留，artifact 保留 7 天。

Actions 使用已核实的官方稳定版：`checkout@v7.0.1`、`upload-artifact@v7.0.1`、`download-artifact@v8.0.1`、`setup-python@v7.0.0`、`setup-node@v7.0.0`，它们的执行运行时均为 Node.js 24。管理页 JavaScript 检查显式使用 Node.js 24 LTS 的最新补丁版，Lua 回归使用 Python 3.14 和 Lupa 2.8。Ubuntu 26.04 的托管镜像目前仍为预览版，构建主机继续使用稳定的 24.04（核实日期：2026-09-14）。

服务端的 `build_jobs` 默认 `auto`，通过 `nproc` 使用运行器全部可用 CPU，GNU Make 和 SDK 内的 Ninja 共用并行任务额度。也可填写正整数指定任务数；构建日志会打印实际选择值和可用 CPU 数。本地 Linux 构建同样默认自动选择，可用 `BUILD_JOBS=4 bash server/tools/build-ocserv.sh /path/to/sdk` 覆盖。线程数设置不会增加运行器本身的 CPU 资源。

- 客户端：`scripts/build-client-linux.sh x64|x86` 使用 Ubuntu 的 MinGW MSVCRT 交叉工具链，静态合并协议与运行库，审计导入表，再打包原样 Wintun DLL。
- 服务端：`scripts/fetch-sdk.py --version 具体版本或auto --output /tmp/sdk` 校验并解压 SDK；SDK 放在源码 Git 仓库之外。`server/tools/build-ocserv.sh` 构建所选 ocserv 和独立中文管理页，共两个 APK；不再编译有重复表单和密码保存缺陷的上游界面。
- 源码：客户端 artifact `corresponding-source` 包含第三方源压缩包；服务端 ZIP 套装的 `source` 目录包含 ocserv 源码、配方及此次 LuCI feed 源码。
- 结果：客户端执行 PE 静态检查；服务端先运行 Lua 5.1 管理逻辑回归，再检查包产物、AArch64 ELF 和管理页样式完整性。Linux 编译和 Windows/N1 实机运行分开记录。

SDK 的版本、哈希、feed 配置和实际提交随产物记录。具体 SDK 版本在手动运行参数中选择，脚本自动解析官方 URL 与 SHA-256；仅接受 25.12.x SDK 的 APK 格式。使用固件作者 SDK 时，可直接在独立 Linux 目录调用 `build-ocserv.sh`。上游版本参数见 [更新说明](UPSTREAM-UPDATES.md)。

服务端校验读取 SDK 自动清理后保留的 `.pkgdir` 包缓存。新增管理页保留原始 CSS，避免 SDK 旧版 CSSTidy 删除 Grid、`gap`、`inset` 等样式；打包后的 JavaScript 和 CSS 同时放在 ZIP 套装的 `validation/ui` 中供复核。

## Windows 客户端

开发机需要 MSYS2 的构建工具、7-Zip 和 PowerShell。终端用户不需要这些工具。实际使用 w64devkit 的 MSVCRT 工具链生成 x86/x64 程序，不用 MSYS2 UCRT GCC 生成发布二进制。

示例 MSYS2 安装位置为 `D:\msys64`，在 MSYS2 shell 安装构建工具：

```sh
pacman -S --needed make perl autoconf automake libtool pkgconf gettext-devel mingw-w64-ucrt-x86_64-cmake
```

PowerShell 中运行：

```powershell
.\scripts\fetch-deps.ps1
.\scripts\build.ps1 -Architecture all -MsysRoot D:\msys64 -OutputRoot build/verified
```

构建脚本锁定并检查源文件 SHA-256。首次静态编译 OpenSSL 较耗时；后续复用 `.deps/x64` 和 `.deps/x86`。`--with-vpnc-script` 是 OpenConnect 构建期默认值，客户端运行时明确传入空脚本指针并自行配置网络，发行包没有该脚本，也不会运行它。

静态组件：OpenConnect 9.21、OpenSSL 3.5.8、libxml2 2.15.3、zlib 1.3.2。动态驱动组件：官方 Wintun 0.14.1。禁用 PKCS#11、TPM、SSO 等此版本不使用的可选功能与动态 OpenSSL providers。

## 验证

```powershell
.\build\verified\x64\bridge_tests.exe
python tests/auth_integration.py --client build/verified/x64/bridge_tests.exe --openssl .deps/x64/bin/openssl.exe --output test-results/auth-x64
python tests/profile_integration.py --client build/verified/x64/bridge_tests.exe --certificate test-results/auth-x64/fixture.pem --output test-results/profile-x64
python scripts/audit-pe.py build/verified/x64/布利杰VPN.exe --output test-results/imports-x64.json
```

对 x86 同样运行。集成测试使用本机回环地址和临时测试证书，不接入真实 N1，也不创建虚拟网卡。凭据管理测试写入带测试进程标识的合成凭据，完成后删除。

`布利杰VPN.exe --smoke-test 输出目录` 在屏幕之外渲染本程序自己的窗口，导出中英文及空凭据错误的 BMP，并退出。此模式不读取已保存密码、不连接 VPN、不添加托盘图标。图标源为 `resources/app.svg`；`scripts/make-icon.py` 可用 Pillow 重建 ICO，已生成的 ICO 本身足以构建，无需 Python。

## 发布

GitHub 手动构建完成后，客户端发布任务等待 x86、x64 和对应源码三个任务全部成功；服务端发布任务等待25.12 SDK 的构建任务全部成功。发布任务从本次运行下载产物，汇总 `SHA256SUMS.txt`，重新核验全部 ZIP，再通过运行器自带的 GitHub CLI 上传。

每次发布先创建草稿，全部附件上传成功后才公开为 **Pre-release**。自动标签为 `client-运行ID-尝试次数` 或 `server-运行ID-尝试次数`，指向实际构建的提交；客户端和服务端分别发布，不覆盖已有发行版。重跑会使用新的尝试次数创建新条目；上传失败会使任务失败，仅留下未公开的草稿。发布成功后的下载链接见任务 Summary。

客户端 Release 附带两种架构的便携 ZIP、对应源码 ZIP 和统一校验文件。服务端 Release 附带所选 SDK 的 ZIP 套装及统一校验文件；套装保留顶层版本目录，内含两个安装包、源码、管理工具、说明、构建元数据和逐文件 `SHA256SUMS`。ZIP 名称及 BUILDINFO 均记录管理页版本。

本地打包 Windows 客户端：

```powershell
.\scripts\package.ps1 -BuildRoot build/verified
```

发布脚本仅 strip 本项目 EXE，保持 Wintun DLL 原样，附带许可证、用户说明和源码说明。源码包中包含本项目源码、构建脚本及第三方源代码压缩包，可重建和重新链接。

## N1 服务端

使用匹配实际固件的 Linux OpenWrt SDK。客户端的 Windows 工具链不能构建或代替 N1 的 Linux `.apk` 包：

```sh
bash server/tools/build-ocserv.sh /path/to/matching-sdk
```

配方和架构检查不等于已验证 OPL 定制固件 ABI。参阅 N1 文档后再选择 SDK。不要安装这次编译产生的官方内核模块到 Flippy 内核。

服务端事务验证另运行 `python tests/guard_integration.py`。网络隔离验证使用打包的 `guard.nft.in` 模板，在 Linux 网络命名空间运行 `sudo python3 tests/vpn_guard_tests.py`。

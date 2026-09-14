# 构建

## GitHub Actions（全部使用 Linux）

仓库 Actions 有两套独立工作流：**手动构建 Windows 客户端**与**手动构建 ocserv 服务端与中文管理页**。选择所需工作流 → Run workflow → main。不需要仓库 Secret；没有 push、pull_request 或定时触发器。标准 `ubuntu-24.04` 运行器可用于这个公开仓库的免费构建，artifact 保留 7 天。

- 客户端：`scripts/build-client-linux.sh x64|x86` 使用 Ubuntu 的 MinGW MSVCRT 交叉工具链，静态合并协议与运行库，审计导入表，再打包原样 Wintun DLL。
- 服务端：`scripts/fetch-sdk.py 24|25 --version 具体版本或auto --output /tmp/sdk` 校验并解压 SDK；SDK 放在源码 Git 仓库之外。`server/tools/build-ocserv.sh` 同时构建所选 ocserv、原 LuCI 页、简体中文翻译与新增管理页。
- 源码：客户端 artifact `corresponding-source` 包含第三方源压缩包；服务端 artifact 的 `source` 目录包含 ocserv 源码、配方及此次 LuCI feed 源码。
- 结果：客户端执行 PE 静态检查，服务端检查包产物和 AArch64 ELF；共同运行 Lua 5.1 管理逻辑回归。Linux 构建不会被描述为 Windows 或 N1 实机测试。

SDK 的版本、哈希、feed 配置和实际提交随产物记录。具体 SDK 版本在手动运行参数中选择，脚本自动解析官方 URL 与 SHA-256；包格式按 SDK 实际配置选择。使用固件作者 SDK 时，可直接在独立 Linux 目录调用 `build-ocserv.sh`。上游版本参数见 [更新说明](UPSTREAM-UPDATES.md)。

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

```powershell
.\scripts\package.ps1 -BuildRoot build/verified
```

发布脚本仅 strip 本项目 EXE，保持 Wintun DLL 原样，附带许可证、用户说明和源码说明。源码包中包含本项目源码、构建脚本及第三方源代码压缩包，可重建和重新链接。

## N1 服务端

使用匹配实际固件的 Linux OpenWrt SDK。客户端的 Windows 工具链不能构建或代替 N1 的 Linux `.ipk/.apk` 包：

```sh
bash server/tools/build-ocserv.sh /path/to/matching-sdk
```

配方和架构检查不等于已验证 OPL 定制固件 ABI。参阅 N1 文档后再选择 SDK。不要安装这次编译产生的官方内核模块到 Flippy 内核。

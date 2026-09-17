# 构建与发布

## GitHub Actions

使用 Actions 中的 **手动构建 Windows 客户端** 或 **手动构建 ocserv 服务端与中文管理页**，选择分支后手动运行。客户端在 Ubuntu 24.04 交叉编译，在 Windows Server 2022 原生回归；服务端使用 Ubuntu 24.04。没有 push、pull_request 或定时触发器。

| 客户端参数 | 默认值 | 用途 |
|---|---|---|
| openconnect_version | 9.21 | 官方协议核心版本 |
| openconnect_sha256 | 空 | 默认使用锁定值，改版本时须提供官方源码 SHA-256 |
| build_jobs | auto | 使用全部可用 CPU，也可指定正整数 |
| publish_release | true | 检查成功后发布 Pre-release，仅 main 生效 |

客户端流程为：参数和脚本检查 → x64/x86 编译及 PE 审计 → Windows 配置、界面、回环认证和便携 EXE 启动回归 → 与对应源码一同发布。源码归档与静态依赖有缓存；源码、配方、编译器或补丁变化时重建。artifact 保留 7 天。

Actions 使用官方 checkout、cache、setup-node、setup-python、upload-artifact 和 download-artifact。JavaScript 检查用 Node.js 24，Windows 测试用 Python 3.14；用户运行客户端无需这些工具。

服务端 build_jobs=auto 通过 nproc 使用可用 CPU，GNU Make 和 SDK 的 Ninja 共用额度。sdk_version 默认 25.12.5，auto 只选稳定的 25.12.x。SDK 放在 Git 仓库之外，仅构建 ocserv 和独立中文管理页两个 APK。详见 [上游更新](UPSTREAM-UPDATES.md)。

## 客户端结构

根 CMakeLists.txt 只调用 client/CMakeLists.txt：

| 文件 | 职责 |
|---|---|
| client/application.*、main.cpp | 主窗口、配置列表、菜单、托盘和生命周期 |
| client/ui.*、dialogs.cpp | Win32 控件、配置/认证弹窗和日志 |
| client/profile.*、platform.* | 便携配置、DPAPI、地址与公共证书解析 |
| client/session.*、certificates.cpp | 官方核心回调、认证、Windows 证书链与系统证书 |
| client/vendor/、patches/ | JSON、上游网络脚本、许可证和核心补丁 |
| client/tests/ | 配置、界面、认证和网络脚本回归 |
| scripts/build-dependencies.sh | Linux/MSYS2 共用静态依赖配方 |

版本和 SHA-256 集中在 scripts/sources.json。静态组件：OpenConnect 9.21、GnuTLS 3.8.13、GMP 6.3.0、Nettle 3.10.2、stoken 0.92、libxml2 2.15.3、zlib 1.3.2。驱动为未修改的官方 Wintun 0.14.1。支持软件令牌、Windows 系统证书；PKCS#11、TPM、浏览器 SSO 未启用。

旧 src/ 的网络/WFP 实现、默认 INI、OpenSSL 配方和旧测试已移除。上游及补丁说明见 [client/UPSTREAM.md](../client/UPSTREAM.md)。

## Linux 交叉编译

使用 Ubuntu 24.04 或相同的 MSVCRT MinGW 工具链：

~~~sh
sudo apt-get install build-essential cmake ninja-build perl pkg-config autoconf automake \
  libtool gettext gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix \
  gcc-mingw-w64-i686-posix g++-mingw-w64-i686-posix mingw-w64-common patch xz-utils python3
BUILD_JOBS=auto bash scripts/build-client-linux.sh x64
BUILD_JOBS=auto bash scripts/build-client-linux.sh x86
python3 scripts/package-source.py
~~~

CMake 需要 3.24+，Python 需要 3.11+。构建目录为 build/client/<arch>，依赖为 .deps/<arch>，输出在 dist/。Linux 不执行 Windows EXE。

依赖先校验归档再解压，补丁只应用于构建目录中的源码副本。没有校验标记的旧源码目录会报错，应使用干净工作区，或在确认只是构建缓存后将旧目录移出 .deps/sources/ 再构建。

## Windows 构建

开发机需要 Python 3.11+、PowerShell、7-Zip 和 MSYS2。使用校验过的 w64devkit 2.10.0 MSVCRT 编译器；MSYS2 UCRT64 仅提供 CMake/Ninja 等开发工具。

在 MSYS2 安装：

~~~sh
pacman -S --needed make perl autoconf automake libtool pkgconf gettext-devel patch python \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
~~~

PowerShell：

~~~powershell
python -m pip install -r client/tests/requirements.txt
.\scripts\fetch-deps.ps1
.\scripts\build.ps1 -Architecture all -MsysRoot D:\msys64
.\scripts\package.ps1 -Architecture all
~~~

-Jobs 0 默认使用可用 CPU。后续构建检查并复用匹配的依赖。Windows 与 Linux 调用同一打包器，统一文件、架构、许可证和校验规则。

## 验证

~~~sh
python tests/build_config_tests.py
python tests/release_tests.py
python tests/client_build_tests.py
node client/tests/script_tests.js
~~~

Windows 原生回归：

~~~powershell
python scripts/test-client.py --build build/client/x64 --package dist/BulijieVPN-0.5.0-windows-x64.zip --output test-results/client-x64
python scripts/test-client.py --build build/client/x86 --package dist/BulijieVPN-0.5.0-windows-x86.zip --output test-results/client-x86
~~~

测试子进程的 PATH 仅保留 Windows 系统目录。认证测试运行真实 OpenConnect/GnuTLS 回调和临时回环 HTTPS 服务；界面测试只操作自己的窗口并导出截图。包内实际 EXE 分别以中文、英文启动和退出。基础回归不创建网卡或修改主机路由。

PE 审计要求正确架构、子系统 6.1、允许的系统 DLL，拒绝已知的 Win7 后新增 API、非系统运行库及未经审计的延迟导入。Wintun 是显式加载的驱动，另外与官方归档核对哈希。

## 发布

客户端等待 windows-tests 和 source 全部成功后发布；服务端等待其构建校验完成。任务下载同次运行的产物，检查 ZIP 和 SHA256SUMS，先建草稿，全部上传成功后公开为 Pre-release。仅发布任务获得 contents: write，使用自动提供的 GitHub token。

标签为 client-运行ID-尝试次数 或 server-运行ID-尝试次数，指向实际提交。重跑创建新条目，不覆盖已有附件。Release 附件长期保留，地址写入工作流 Summary。

客户端附件包含 x64/x86 便携 ZIP、对应源码 ZIP 和校验文件。打包只 strip 本项目 EXE，Wintun 保持原样；附带许可证、使用说明和 BUILDINFO。源码包包含项目源码、配方、补丁及全部锁定第三方归档，排除本机工具、测试数据和旧代码。

## N1 服务端

使用匹配固件的 OpenWrt 25.12.x / armsr/armv8 / aarch64_generic / APK SDK：

~~~sh
python3 scripts/fetch-sdk.py --version 25.12.5 --output /tmp/sdk-25.12
bash server/tools/build-ocserv.sh /tmp/sdk-25.12
python3 scripts/package-server.py /tmp/sdk-25.12
~~~

服务端 ZIP 包含 ocserv 源码、配方、LuCI feed 源码、BUILDINFO、安装工具和逐文件校验。包审计读取 SDK 清理后保留的 .pkgdir；管理页保留原始 CSS，附带 validation/ui 供复核。

事务回归运行 python tests/guard_integration.py；网络隔离验证在 Linux 网络命名空间运行 sudo python3 tests/vpn_guard_tests.py。SDK/架构检查不替代 OPL 定制固件验收，不用官方模块替换 Flippy 内核模块。

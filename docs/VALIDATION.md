# 验证记录

客户端 0.5 重构日期：2026-09-17；服务端管理页 0.4.1 修复验证日期：2026-09-17。本地 Windows 环境为 10.0.17763，客户端 CI 使用 Windows Server 2022。测试使用临时合成账号、证书和独立目录，不读取真实 VPN 密码，也没有修改用户 N1。

## 0.4.1 实际运行接口修复

在官方 OpenWrt 25.12.5 AArch64 rootfs 中复现并修正：Lua `%x` 不接受超过有符号 32 位范围的参数、nixio 权限参数需要八进制数字而非十进制位掩码，以及实际 ocserv 版本输出为 `OpenConnect VPN Server 1.5.0`。

真实 Lua/nixio/UCI/rpcd 回归通过 15 项，覆盖创建账号、密码文件同步、哈希校验、停用/恢复、旧明文密码迁移及文件权限。原版 OpenConnect CLI 对 APK 中的 ocserv 通过 7 项认证检查：正确密码、错误密码、改密立即生效、旧密码失效、停用、恢复和删除。该测试停在认证成功获取 cookie 阶段，不创建 VPN 隧道。

`tests/openwrt_runtime_tests.lua` 和 `tests/ocserv_auth_tests.py` 仅允许在带 `/tmp/ocserv-easy-test-root` 标记的独立 OpenWrt 测试根目录内运行。测试目录使用官方 rootfs、真实 LuCI 运行库与合成账号；通过 QEMU 运行 AArch64 程序，不代表已在用户 N1 上验证流量转发。

## Windows 客户端 0.5

旧版客户端的测试和结果不用于证明重构版。新版发布工作流必须通过以下独立检查，实际结果和窗口截图保存在 client-windows-regressions artifact：

| 检查 | 覆盖范围 |
|---|---|
| 原生配置与 DPAPI | 多配置、Unicode、加密、来源绑定、损坏配置保护、公开配置导入/导出 |
| 真实 TLS/登录 | 固定指纹、CA、首次确认、更换公钥、密码重试、验证码、认证分组、取消、跨来源跳转及过期证书 |
| 原生窗口 | 中英文主窗口、配置编辑、VPN 信息页、日志、托盘、保存/取消与控件边界 |
| Windows Script Host | 真实执行失败的 pre-init 脚本，检查普通错误、退出码 259 和脚本异常传播 |
| 网络脚本 | 执行随包 JScript，模拟 netsh/route；必要操作失败、空 DNS/WINS 清理、UTF-16 日志 |
| 便携 EXE | 清空开发工具 PATH 后，解压包内实际 EXE 以中英文启动并退出 |
| PE 与打包 | x86/x64、子系统 6.1、系统 DLL、已知 Win7 后新增 API、Wintun 哈希、源码归档校验及本机数据排除 |

认证回归使用真实 OpenConnect/GnuTLS 和临时回环 HTTPS 服务，取消或拒绝证书时密码提交次数必须为 0。Windows Script Host 测试在创建网卡前故意退出；以上基础回归不创建 VPN 网卡、不修改本机路由，也不接入真实 N1。

Wintun 是显式加载的独立驱动组件，不属于“静态导入表仅含系统 DLL”这一结论。它与官方对应架构的 DLL 单独核对，不会被 strip 或修改。

## 服务端与管理页

| 检查 | 结果与范围 |
|---|---|
| Lua 5.1 管理逻辑 | 43 项，覆盖账号管理、密码迁移、OpenWrt 整数/权限接口约束、版本查询失败、会话撤销、并发、失败回滚、公开配置导出 |
| VPN 专用上网事务 | 23 项，覆盖开关、配置保留、确认超时和开机恢复；UCI/nixio/服务边界为模拟接口 |
| 全新安装与重试 | 20 项，执行实际 install.sh 和 preflight-n1.sh；模拟 APK/UCI/服务，覆盖 btrfs 存储报告失败、aarch64、旧页面清理、手动安装后的补修、备份与回滚 |
| 证书初始化 | 7 项，以真实 GnuTLS certtool/OpenSSL 验证 IP/主机名、用途、私钥权限及保留现有证书 |
| 浏览器交互 | 12 组流程，验证账号增改删、密码二次确认、停用/恢复、服务设置保存、开关完整操作、网络编辑锁、两种配置下载、中英文及移动端；无脚本错误 |
| nftables 隔离 | 18 项，真实 Linux 网络命名空间、路由、连接跟踪、NAT、REDIRECT 与 TPROXY；覆盖 LAN 网关/DNS/代理绕过、VPN 来源伪造、IPv6、管理访问和恢复 |

隔离网络使用 veth 模拟 VPN 接口；登录入口和代理监听为测试服务。它验证规则的实际行为，不代表真实 ocserv 认证或 OpenClash 分流已在 N1 验收。

本地详细结果位于 test-results。服务端仅维护 OpenWrt 25.12.x 的 APK 构建；旧发行版历史记录由 Git 历史和 GitHub Actions 保留。

## 仍需实机验收

- Windows 7 SP1 x86/x64 启动、凭据存储、SHA-2 驱动安装与网络通信。
- 提权后的 Wintun、路由、DNS 配置和退出/异常退出后的回收。重构客户端不再实现旧版自定义 WFP 开关。
- OPL v0.0.8 / 6.12.66-flippy-94+ 的 APK 依赖与安装、实际 CSTP/DTLS 数据转发、同账号连接限制。
- OpenClash 各模式、DNS/IPv6 防绕过、断网重连、睡眠唤醒、重启持久化及长期性能。

编译、模拟事务和回环测试不能代替以上设备验收。

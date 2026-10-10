# Linkora VPN 客户端增强建议

参考 [GlobalProtect-openconnect 3.0.0](https://github.com/yuezk/GlobalProtect-openconnect/tree/8724ed3b373536942660731c7bd529f325c014c4)。Windows 0.7.0 已落实会话归属、脱敏诊断与可选自动连接/重试；macOS、Linux 桌面实现与限制见[桌面说明](DESKTOP.md)。后续企业认证方向仍为功能评估。

Linkora VPN 当前已有多服务器配置、托盘、认证表单、DPAPI 密码保存、日志脱敏、超时重连和有界事件队列。后续增强应在这些能力上完善，保持 Windows 原生界面和现有 ocserv 协议。

| 方向 | 可以借鉴的做法 | 服务端条件 | 建议顺序 |
|---|---|---|---|
| 会话稳定性 | 会话编号、状态代次、取消与清理归属；快速启停、网络变化和睡眠恢复回归 | 继续使用当前 ocserv | 已实现 |
| 诊断与日志 | 错误分类、诊断导出；认证字段及常见编码形式在进入日志队列前脱敏 | 无需新增服务端组件 | 已实现 |
| 连接体验 | 默认关闭的登录后连接与有限网络重试，明确的状态与倒计时 | 继续使用当前 ocserv | 已实现 |
| 界面与权限分离 | 普通权限界面与小型特权服务分工，严格校验本地调用方及会话生命周期 | 无需更换 ocserv；Windows 客户端需要新增后台服务与安装流程 | 单独实施 |
| MFA | 根据服务端认证挑战处理第二因素，不把普通密码当作验证码 | 配置 ocserv 支持的 RADIUS/PAM 等认证后端，同时评估 OpenWrt 构建依赖；iOS 还需扩展交互认证界面 | 有实际需求再做 |
| 浏览器 SSO | 由系统浏览器完成身份认证，再将结果交还 VPN 连接流程 | 需要身份提供方和匹配的服务端认证流程 | 有实际需求再做 |
| GlobalProtect Portal / Gateway / HIP | 门户与网关选择、设备状态报告及相应会话维护 | 需要 GlobalProtect 协议服务端或另外实现对应服务；现有 ocserv 不提供这些接口 | 不列入当前 ocserv 增强范围 |

VPN 始终需要服务端。当前 N1/OpenWrt 上的 ocserv 已承担这一角色；提升客户端稳定性、诊断和使用体验，通常无需部署另一套 VPN 服务端。

## 值得阅读的开源实现

- [会话注册与代次校验](https://github.com/yuezk/GlobalProtect-openconnect/blob/8724ed3b373536942660731c7bd529f325c014c4/apps/gpservice/src/session_registry.rs)：限制待激活会话，识别过期连接，明确资源所有者。
- [取消与资源释放测试](https://github.com/yuezk/GlobalProtect-openconnect/blob/8724ed3b373536942660731c7bd529f325c014c4/apps/gpservice/src/vpn_task/ownership_tests.rs)：通过真实回环 HTTPS 和受控响应验证取消后的清理顺序。
- [日志脱敏](https://github.com/yuezk/GlobalProtect-openconnect/blob/8724ed3b373536942660731c7bd529f325c014c4/crates/gpapi/src/utils/redact.rs)与[认证数据生命周期](https://github.com/yuezk/GlobalProtect-openconnect/blob/8724ed3b373536942660731c7bd529f325c014c4/crates/gpapi/src/session/connection.rs)：敏感字段隐藏及内存清理思路。
- [外部浏览器认证](https://github.com/yuezk/GlobalProtect-openconnect/blob/8724ed3b373536942660731c7bd529f325c014c4/crates/auth/src/browser/browser_auth.rs)：桌面浏览器与 VPN 进程分工。其 GlobalProtect 回调不能直接用于 ocserv。

该项目的 CLI、后台服务及多个库提供开源代码，许可证按组件分为 MIT、GPL-3.0 等；GP Connect 桌面 GUI 外壳是专有组件，不能视为可直接复制的开源界面。具体范围见其[许可证说明](https://github.com/yuezk/GlobalProtect-openconnect/blob/8724ed3b373536942660731c7bd529f325c014c4/README.md#licensing)。

建议先对照补强会话状态与异常恢复测试，再完善诊断和可选自动连接。权限分离与企业认证分别作为独立改动评估，不为这些可选能力增加当前便携版的默认依赖。

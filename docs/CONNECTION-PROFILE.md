# 公共连接配置与客户端能力

当前 `.vpn` 使用唯一的 **Version 1** 格式，分发服务器连接信息。各平台的私有配置、密码和令牌存储保持独立。不读取旧版无版本号文件或 Android 配置列表 JSON；请从新版管理页重新导出。

## 格式

UTF-8 文本，最大 128 KiB，只有一个 `[VPN]` 段。字段区分大小写，重复字段、未知字段、NUL 和其他配置段均拒绝。

```ini
[VPN]
Version=1
Name=公司 VPN
Server=https://vpn.example.com:4443
Protocol=anyconnect
Trust=prompt
```

| 字段 | 约束 |
| --- | --- |
| Version | 必须为 `1` |
| Name | 可选；最多 128 个 UTF-16 单元、512 个 UTF-8 字节 |
| Server | HTTPS 服务器，最多 4096 个 UTF-8 字节；不得带账号密码 |
| Protocol | 默认 `anyconnect`；受目标客户端核心支持范围限制 |
| Trust | 必须为 `pin`、`ca` 或 `prompt` |
| ServerPin | 仅 `pin` 使用，必须是规范的完整 `pin-sha256:` 公钥指纹 |
| CABase64 | 仅 `ca` 使用，Base64 编码的公共 PEM 证书，解码后最大 64 KiB |

`pin` 和 `ca` 不能混用；`prompt` 不携带指纹或 CA。文件不接受用户名、密码、软件令牌、私钥、本机路径、脚本或私有连接选项。UDP、重连、应用分流等设置在各客户端本机管理。

## 平台能力

| 客户端 | `.vpn` 证书方式 | 路由 | DNS 分流 |
| --- | --- | --- | --- |
| Windows | Pin / CA / 首次确认 | 服务器包含与排除路由 | 由网络脚本和系统策略处理 |
| Android | Pin / CA / 首次确认 | 服务器路由、数字网段与应用分流 | 不支持按域名选择解析器；服务端须关闭 DNS 分流 |
| macOS / iOS | Pin | 服务器双栈包含与排除路由 | 支持服务器下发的域名列表 |
| Linux | CA / 首次确认 | 由 NetworkManager 处理 | 由系统插件处理 |

管理页根据目标客户端筛选导出方式，后端再次校验。Apple 客户端只接受 AnyConnect 和 Pin；Linux 导入不接受 Pin，证书验证与认证交互由系统 OpenConnect 助手执行，配置 CA 时禁止手动跳过验证失败，同时遵循系统信任设置。Windows / Android 的指定 CA 与 Pin 互斥，指定 CA 失败不能改为手动接受。

IPv4、IPv6 的服务器包含列表分别处理；已协商的地址族没有包含列表时使用该族默认路由。Android 手动“仅包含”列表保持明确范围。便利性的本地绕过不会覆盖服务器明确指定的私网，服务器和用户的显式排除仍生效，最终无流量路由会报错。VPN DNS 保留必要的主机路由。Apple 对没有协商 IPv6 的 IPv4 全隧道保留 IPv6 防绕行保护，IPv4 分流不会无条件捕获全部 IPv6。

## 管理接口

完整快照包含 `api_version: 2`、操作权限、客户端能力、配置修订号和安装包版本。HTTP 修改请求必须带数值 `api_version: 2` 及当前 `revision`，旧接口与未知版本直接拒绝。页面版本及静态资源缓存标记读取已安装 APK 元数据。

ocserv 的配置渲染模块使用 `settings_contract = 2`。管理包与服务包须成套安装。服务设置、保存客户端设置和临时导出共用 DNS 校验。入口是 IP 地址时，该地址不能同时用作隧道 DNS。仅配置 IPv4 地址池时，下发的 DNS 中至少需要一个 IPv4 地址。

`client_targets[].unavailable` 表示当前设置下无法分发给该平台的原因。开启 DNS 分流时，选择 Android 会禁用保存与下载并显示原因。已保存 Android 为目标时，开启 DNS 分流也会被拒绝。保存及下载都检查所选证书方式需要的 CA 或指纹。

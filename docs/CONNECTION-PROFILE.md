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

CA 必须由 1–64 个有效 `CERTIFICATE` PEM 块组成，块外仅允许空白；DER、PKCS#7、私钥、尾随垃圾或损坏证书均拒绝。Windows 导出及 Windows/Linux/Android 导入执行同一公共格式约束；Android 手动选择 CA 也在保存前校验，避免导入成功但原生核心无法读取。

`pin` 和 `ca` 不能混用；`prompt` 不携带指纹或 CA。文件不接受用户名、密码、软件令牌、私钥、本机路径、脚本或私有连接选项。UDP、重连、应用分流等设置在各客户端本机管理。

## 平台能力

| 客户端 | `.vpn` 证书方式 | 路由 | 本项目的 DNS |
| --- | --- | --- | --- |
| Windows | Pin / CA / 首次确认 | 服务器包含与排除路由 | 网络脚本设置服务端下发的 DNS |
| Android | Pin / CA / 首次确认 | 服务器路由、数字网段与应用分流 | 隧道 DNS，可在本机指定解析器 |
| macOS / iOS | Pin | 服务器双栈包含与排除路由 | 服务端下发的隧道 DNS |
| Linux | CA / 首次确认 | 由 NetworkManager 处理 | 由系统插件应用隧道 DNS |

管理页根据目标客户端筛选导出方式，后端再次校验。Apple 客户端只接受 AnyConnect 和 Pin；Linux 导入不接受 Pin，证书验证与认证交互由系统 OpenConnect 助手执行，配置 CA 时禁止手动跳过验证失败，同时遵循系统信任设置。Windows / Android 的指定 CA 与 Pin 互斥，指定 CA 失败不能改为手动接受。

本项目不再下发按域名选择解析器的 `split-dns`。客户端获得隧道 DNS；使用代理时，由路由器上的代理 DNS 规则决定后续解析。客户端的流量路由分流与此独立。Apple 对其他服务器下发的 DNS 域名列表仍有解析能力；Android 明确拒绝此类外部配置，Windows 未实现此能力。

CA 模式在保存及下载时，以 `/etc/ocserv/ca.pem` 验证实际服务端证书链、TLS 服务端用途（EKU serverAuth）和导出地址。证书或 CA 在验证期间变化会拒绝导出。`client_trust.pin/profile/address` 分别提供 `available` 和可选 `error`，页面与导出共用同一能力服务；证书文件存在不代表 Pin 可导出；外部证书可选固定 Pin，或先提供匹配的公共 CA。多证书/虚拟主机不能被猜测为单一证书。

IPv4、IPv6 的服务器包含列表分别处理；已协商的地址族没有包含列表时使用该族默认路由。Android 手动“仅包含”列表保持明确范围。便利性的本地绕过不会覆盖服务器明确指定的私网，服务器和用户的显式排除仍生效，最终无流量路由会报错。VPN DNS 保留必要的主机路由。Apple 对没有协商 IPv6 的 IPv4 全隧道保留 IPv6 防绕行保护，IPv4 分流不会无条件捕获全部 IPv6。

## 管理接口

基础快照 `/data` 包含 `api_version: 5`、操作权限、配置修订号、目标客户端清单和安装包版本。`/client-status` 独立返回带同一 `revision` 的 `client_trust` 与 `certificates_ready`；`/guard-status` 独立返回拓扑探测结果。HTTP 修改请求必须带数值 `api_version: 5` 及当前 `revision`，旧接口与未知版本直接拒绝。页面版本及静态资源缓存标记读取已安装 APK 元数据。

ocserv 的配置渲染模块使用 `settings_contract = 3`。管理包与服务包须成套全新安装，不提供旧配置迁移。服务设置拒绝未知字段，`split_dns` 已删除；自定义配置也不能覆盖 `split-dns` 或 `tunnel-all-dns`。服务设置、保存客户端设置和临时导出共用 DNS 校验。入口是 IP 地址时，该地址不能同时用作隧道 DNS。仅配置 IPv4 地址池时，下发的 DNS 中至少需要一个 IPv4 地址。

`data` 返回完整页面快照及初始在线记录。`status` 每 3 秒返回运行状态、`online_count`、`online_version`、`online_error`、恢复状态与隔离状态；`online` 返回完整在线记录。内容版本是不透明标识，不能当作配置修订号。账号页在内容版本变化时获取列表；其他标签只取摘要。切换标签、隐藏、编辑或提交可取消列表读取，迟到列表不能覆盖较新的摘要。

在线快照与小摘要共用 5 秒有效期、内容版本和独立的失效代次。TTL 到期后重新查询，排序规范化后的记录完全相同时保留内容版本；记录变化或显式失效后重新建立版本。失效代次仍在每次刷新时变化，用于拒绝迟到写入。查询锁独立于配置锁，最多等待 5 秒复用另一请求的刷新；缓存或锁不可用时仍受原查询时限约束。摘要命中不解码完整会话列表，不执行 occtl，也不写文件。缓存格式 3 可丢弃，不迁移旧格式。

下载错误以 JSON 错误码返回，页面在原位置显示具体原因并恢复按钮；成功结果仍是附件。请求有 15 秒超时，生成下载后释放临时 Blob URL。旧 `ca_available` / `ca_error` / `online_generation` 字段已移除。

# OpenWrt 实机验收

适用于本项目 OpenWrt 25.12 / APK 服务端。使用尚未部署的测试路由器和局域网 SSH，先备份配置。测试会添加临时账号、重启 VPN 和修改服务设置；不能在有正式用户连接时执行。密码通过 `LANSYRA_ROUTER_PASSWORD` 环境变量传入，不写入测试源码或报告。

## 三个入口

1. `tests/router_runtime_tests.lua` 在路由器本机运行：真实 UCI 渲染、ocserv 配置校验、nixio 进程退出/超时、原子文件与目录同步。只读现有配置，临时文件在执行后移除。
2. `tests/router_api_tests.py` 在管理电脑运行：真实 LuCI 登录、接口版本、CSRF、修订冲突、账号操作、服务控制、设置预检/回滚、五个平台配置导出和状态耗时。使用随机测试账号，结束时删除自己的账号并恢复原设置、导出选项及启停状态。
3. `tests/router_vpn_tests.py` 在管理电脑运行：通过 SSH 在路由器建立独立客户端网络命名空间，使用真正的 OpenConnect 客户端验证 TLS、DTLS、网关 ICMP、DNS、HTTPS、账号撤销与按连接断开。需要路由器已有 `openconnect`、完整 iproute2、nftables、curl 和 nslookup；管理电脑需要 `paramiko`。只给临时 veth 增加一条入站规则，不改管理电脑的路由。

接口和流量测试必须显式添加 `--exercise`：

```sh
python tests/router_api_tests.py --url http://192.168.1.1 --exercise
python tests/router_vpn_tests.py --host 192.168.1.1 --exercise
```

将 `router_runtime_tests.lua` 上传到路由器临时目录，再执行 `lua /tmp/router_runtime_tests.lua`。确认后删除上传的脚本。流量测试使用 `198.18.253.0/30` 作为隔离的传输网段，发现该路由已存在时拒绝运行；使用当前 VPN 地址池和 DNS，不假定公共 DNS 一定可达。测试结束清理客户端、账号、veth、命名空间、临时防火墙规则及 DNS 文件。若连接或清理失败，报告保留错误；检查现场后重试，不把失败当作通过。

## 结果与边界

Python 报告默认写入 `test-results/router-api.json` 和 `test-results/router-vpn.json`，不保存登录密码、Cookie 或 VPN 凭据。正式记录前保存精简报告，再清理临时输出。安装/卸载还应单独确认 APK 状态、恢复目录消失、账号证书清理及重复运行结果；APK 的 `--simulate` 输出不能代替实际卸载。

原生实机检查不能被提供额外方法的 UCI 替身或自动搜索 PATH 的执行器替身代替。Linux 安装/卸载回归分别在 `server_install_tests.py`、`server_uninstall_tests.py`；其执行器替身遵守 execv 路径约束。配置渲染回归的原生游标不提供 LuCI 的 `get_first`。

网络命名空间测试验证真实的加密、认证、隧道和转发路径，但客户端仍在同一台路由器中，不能替代 Windows/手机、外网接入、长期重连、真实代理节点或吞吐压力验收。未配置 OpenClash/Nikki/Momo 时，实机只验证隔离预检拒绝和无副作用；正常代理接入与故障恢复另由隔离回归覆盖，不能据此宣称真实代理出口已通过。

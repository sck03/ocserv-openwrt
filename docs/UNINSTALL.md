# 服务端一键卸载与重装

适用于本项目 OpenWrt 25.12 / APK 服务端。通过**局域网 SSH** 以 root 执行；卸载会断开所有 VPN 连接，删除账号、配置和 `/etc/ocserv` 内的证书。管理页 **系统维护** 提供步骤说明和命令复制，实际卸载在 SSH 终端执行。

```sh
lansyra-uninstall --yes
```

不加参数时输入 `UNINSTALL` 确认；`--dry-run` 只预览软件包计划，不恢复事务、停止服务或删除配置。无需更新软件源，也不依赖系统 `timeout`。服务命令复用项目的 Lua/nixio 执行器，包含时限、输出预算及超时后的子进程组清理。

安装后的命令尚未更新或仍提示缺少 `timeout` 时，将**新套装完整解压**到 `/tmp/lansyra`，直接使用套装入口，无需先覆盖安装：

```sh
cd /tmp/lansyra
sh uninstall.sh --yes
sh install.sh
```

`uninstall.sh` 及 `with-lock.lua`、`lease.lua`、`run.lua`、`process.lua`、`network.lua`、`firewall.lua` 必须在同一目录；请保留整个套装，不单独复制脚本。不迁移旧数据；重新安装后重新创建账号，核实新证书指纹并分发客户端配置。

## 自动清理范围

1. 使用已安装版本的恢复入口，恢复普通配置事务并关闭 VPN 专用上网；恢复失败保留记录和软件包。
2. 取得与管理页共用的维护锁，拒绝未提交的 UCI 修改，预演 APK 删除计划。
3. 在 `/root/lansyra-before-uninstall-*` 保存配置、证书和完整的重试入口，备份目录权限为 700。创建 `.lansyra-uninstall-runtime` 临时 APK 依赖，使用已经安装的 Lua/nixio，不联网下载。
4. 停止并禁用 `ocserv` 和 `ocserv-easy-guard`；删除受管防火墙段 `ocserv_vpn`、`ocserv_vpn_to_lan`、`ocserv_vpn_nat`、`ocserv_vpn_entry`。删除 Nikki/Momo 的 `lansyra_vpn` 段和精确匹配的 `Lansyra VPN ingress` 模板标记，重载受影响的运行中服务。
5. 删除 `ocserv`、`luci-app-ocserv-easy` 及存在的原版页面、中文翻译包。恢复所需运行库继续保留；其余不再需要的依赖由 APK 自动清理，不使用强制或反向依赖递归删除。
6. 确认软件包已移除后，删除 VPN 账号、证书、临时密码和页面缓存，完成网络事务，再释放临时运行库依赖。共享锁 inode 保留到重启，避免等待者使用不同锁。

代理软件、订阅、其他管理员规则、外部证书和所有安装/卸载备份保留。普通 VPN 接入曾启用的代理全局开关也保留，避免覆盖后续设置。自行添加的 WAN 规则、主路由转发和 DDNS 按实际用途处理。

## 失败与重试

网络变更在 `/etc/lansyra-uninstall` 保存前后文件，逐个原子替换。防火墙检查、重载或 APK 删除失败时恢复网络文件；已停止的 VPN 服务不会自动启用。再次执行同一命令即可重试。进程中断后先核对全部现场，发现管理员后续修改则保留现场与记录，不覆盖新配置。

`卸载预演` 下的 `Purging … OK` 是 `apk del --simulate` 输出，尚未删除软件包。只有最后出现 `卸载完成。备份：…` 且脚本退出码为 0，才表示全部卸载完成。页面仍可打开或 `/etc/init.d/ocserv status` 显示 `active with no instances`，都不代表 VPN 进程正在运行。

旧脚本曾以裸命令名 `fw4` 调用不搜索 PATH 的执行器，导致网络恢复返回 127、保留恢复记录，并阻止下一次安装。修正版使用 `command -v fw4` 得到的完整路径，并报告失败命令与退出码；用修正版套装中的 `uninstall.sh` 重试即可继续恢复，不要手动删除 `/etc/lansyra-uninstall` 来绕过检查。

若命令已随软件包删除，使用终端打印的实际目录执行 `sh /root/lansyra-before-uninstall-实际后缀/uninstall.sh --yes`，或重新解压套装执行 `sh uninstall.sh --yes`。中断期间保留 Lua/nixio；全部清理完成后，即使运行库已被自动移除，备份入口仍能确认已卸载或完成最后的依赖释放。卸载恢复记录存在时，安装器拒绝开始新安装。

遇到 `/etc/ocserv/easy-install-network` 未完成首次安装记录，先用对应完整套装运行 `sh install.sh` 完成安装恢复，再卸载。损坏或不支持的隔离事务使用原版本处理，不直接删除记录。卸载依赖现有 APK、UCI、Lua/nixio 和 Firewall4，不适用于 IPK 固件或其他项目。

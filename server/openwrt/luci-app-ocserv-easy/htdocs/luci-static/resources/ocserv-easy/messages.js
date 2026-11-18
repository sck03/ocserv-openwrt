/* SPDX-License-Identifier: GPL-3.0-or-later */
(function () {
  "use strict";
  window.OcservEasyMessages = function (t) {
    var errors = {
      interface_mismatch: t("页面与服务接口版本不一致，请刷新页面或重新安装完整套装。", "Page and service versions differ. Reload or reinstall the complete bundle."),
      unsupported_client_profile: t("目标客户端不支持所选证书方式，请重新选择客户端和导出方式。", "The target client does not support this certificate method."),
      split_dns_requires_domain: t("启用 DNS 分流前请填写默认域名。", "Enter a default domain before enabling split DNS."),
      invalid_certificate_path: t("请填写有效的证书和私钥绝对路径。", "Enter valid absolute certificate and private key paths."),
      unsupported_client_dns: t("此客户端不支持 DNS 分流，请调整服务设置或选择其他客户端。", "This client does not support split DNS."),
      file_too_large: t(
        "配置超过大小上限，原配置已保留。请减少配置内容后重试。",
        "The configuration exceeds its size limit. Previous settings were preserved.",
      ),
      recovery_pending: t(
        "上次服务设置尚需恢复，请稍后刷新；恢复记录位于 /etc/ocserv/easy-transaction。",
        "A previous configuration transaction needs recovery. Refresh shortly; records are in /etc/ocserv/easy-transaction.",
      ),
      guard_requires_fw4: t(
        "需要 Firewall4 和 nftables。",
        "Firewall4 and nftables are required.",
      ),
      guard_requires_proxy: t(
        "请先安装、配置并运行 OpenClash、Nikki 或 Momo 中的一款。",
        "Install, configure and run one of OpenClash, Nikki or Momo first.",
      ),
      guard_proxy_unsupported: t(
        "检测到运行中的其他代理，但尚未提供经过验证的 VPN 接入适配。请切换到已支持的代理。",
        "Another proxy is running without a verified VPN ingress adapter. Select a supported proxy.",
      ),
      guard_proxy_conflict: t(
        "检测到多款代理同时启用。请只保留一款启用，再开启专用上网。",
        "Multiple proxy services are enabled. Leave only one enabled before using VPN-only access.",
      ),
      guard_proxy_mode: t(
        "Nikki/Momo 需要关闭仅核心模式，并配置 TCP 与 UDP 透明代理。",
        "Disable core-only mode and configure TCP and UDP transparent proxying in Nikki/Momo.",
      ),
      guard_proxy_template: t(
        "此版本的代理入口模板不兼容或已被修改，未应用设置。请先关闭专用上网再升级代理。",
        "The proxy ingress template is unsupported or modified. No settings were applied. Disable VPN-only access before upgrading the proxy.",
      ),
      guard_requires_ocserv: t(
        "请先完成账号和证书设置，并启动 VPN 服务。",
        "Set up accounts and certificates, then start VPN first.",
      ),
      guard_requires_routed_vpn: t(
        "此功能需要普通账号认证和独立 VPN 地址池，请关闭代理 ARP。",
        "Use password authentication and a separate VPN pool with proxy ARP disabled.",
      ),
      guard_requires_lan: t(
        "无法读取 LAN 的 IPv4 地址、设备或防火墙区域。",
        "Cannot read the LAN IPv4 address, device or firewall zone.",
      ),
      guard_admin_not_lan: t(
        "请通过同一局域网的 IPv4 地址访问此页面后开启。",
        "Open this page over IPv4 from the same LAN before enabling.",
      ),
      guard_side_router_only: t(
        "此开关适用于 N1 单网口旁路由；检测到了正在使用的 WAN。",
        "This switch is for a single-interface N1 side router; an active WAN was detected.",
      ),
      guard_extra_offload: t(
        "检测到额外的 SFE/快捷转发加速，请先关闭并重启 N1。",
        "Disable extra SFE/shortcut forwarding acceleration and reboot the N1 first.",
      ),
      guard_pool_overlap: t(
        "VPN 地址池与局域网重叠，请先在服务设置中更换地址池。",
        "The VPN pool overlaps the LAN. Change it in Service settings first.",
      ),
      guard_port_conflict: t(
        "VPN 端口与 SSH 或网页管理端口冲突。",
        "The VPN port conflicts with SSH or web administration.",
      ),
      guard_requires_dnsmasq: t(
        "未找到 dnsmasq 配置。",
        "No dnsmasq configuration was found.",
      ),
      guard_section_conflict: t(
        "发现同名的防火墙配置，请先核对并处理冲突，再使用此开关。",
        "Firewall sections already exist. Review and resolve the conflicts before enabling this switch.",
      ),
      guard_worker_failed: t(
        "后台配置进程未启动，请重试或查看系统日志。",
        "The background configuration process did not start. Retry or check the system log.",
      ),
      guard_firewall_failed: t(
        "防火墙校验或应用失败，正在恢复原配置。",
        "Firewall validation or apply failed. Restoring the previous configuration.",
      ),
      guard_dns_failed: t(
        "DNS 服务重启失败，正在恢复原配置。",
        "DNS restart failed. Restoring the previous configuration.",
      ),
      guard_proxy_failed: t(
        "代理服务或 VPN 入口规则未就绪，正在恢复原配置。",
        "The proxy service or VPN ingress rules are not ready. Restoring the previous configuration.",
      ),
      guard_confirmation_expired: t(
        "应用后未能确认管理页面可访问，已尝试自动恢复。",
        "The page could not confirm connectivity after applying. Automatic restoration was attempted.",
      ),
      guard_state_invalid: t(
        "恢复记录无法读取，请检查 /etc/ocserv/easy-guard 中的备份。",
        "Cannot read the recovery record. Check /etc/ocserv/easy-guard.",
      ),
      guard_settings_locked: t(
        "请先关闭 VPN-only 开关，再修改 VPN 网络设置。账号仍可正常管理。",
        "Disable VPN-only mode before changing VPN networking. Account management remains available.",
      ),
      invalid_username: t(
        "账号限 1–64 位英文、数字、点、下划线、短横线或 @，以英文或数字开头。",
        "Use 1–64 letters, digits, dots, underscores, hyphens or @. Start with a letter or digit.",
      ),
      duplicate_user: t(
        "这个账号已存在，请使用其他名称。",
        "This username already exists.",
      ),
      invalid_group: t(
        "用户组格式无效；一般保持 * 即可。",
        "Invalid group. Normally leave this as *.",
      ),
      password_required: t(
        "新增账号或修复密码记录时需要填写新密码。",
        "A password is required for a new account or an invalid password record.",
      ),
      password_mismatch: t(
        "两次输入的密码不一致，请重新核对。",
        "The two passwords do not match. Check them and try again.",
      ),
      invalid_password: t(
        "密码至少 8 个字符，不能包含换行，UTF-8 长度最多 128 字节。",
        "Use a password of at least 8 characters, without control characters, and at most 128 UTF-8 bytes.",
      ),
      hash_failed: t(
        "无法生成或保留密码，请设置新密码并检查系统的 SHA-512 crypt 支持。",
        "Cannot generate or preserve this password. Set a new password and check SHA-512 crypt support.",
      ),
      invalid_existing_user: t(
        "现有配置中有不支持的账号名，请修改或删除该账号。",
        "An existing username is unsupported. Edit or delete that account.",
      ),
      invalid_existing_password: t(
        "现有账号的密码记录无效，请先修正。",
        "An existing account has an invalid password record.",
      ),
      stale_revision: t(
        "配置已被其他页面修改。请关闭此对话框，刷新页面后重试。",
        "Configuration changed in another page. Close this dialog, refresh and try again.",
      ),
      pending_changes: t(
        "其他页面有尚未提交的 VPN 或防火墙修改，请先应用或撤销。",
        "Another editor has pending VPN or firewall changes. Apply or discard them first.",
      ),
      busy: t(
        "另一个操作正在进行，请稍后重试。",
        "Another operation is in progress. Try again shortly.",
      ),
      upgrade_required: t(
        "请先升级到 ocserv 1.6.0 或后续版本。",
        "Upgrade to ocserv 1.6.0 or later first.",
      ),
      version_unavailable: t(
        "暂时无法读取 ocserv 版本。请稍后刷新重试，或运行诊断脚本检查服务。",
        "The ocserv version could not be read. Refresh and retry, or run the diagnostic script.",
      ),
      plain_auth_required: t(
        "账号管理需要使用 plain 用户名密码认证。",
        "Account management requires plain username/password authentication.",
      ),
      custom_auth_file: t(
        "正在使用自定义认证文件；请先恢复标准 UCI 账号配置。",
        "A custom authentication file is active. Restore standard UCI account management first.",
      ),
      proxy_arp_managed: t(
        "当前启用了代理 ARP。使用本页管理网络前，请先将 VPN 改为独立地址池。",
        "Proxy ARP is enabled. Configure a separate VPN pool before managing networking here.",
      ),
      custom_override: t(
        "额外配置文件覆盖了这些设置，请先在原设置中移除重复项。",
        "The extra configuration file overrides these settings. Resolve duplicate directives in the original configuration first.",
      ),
      config_check_failed: t(
        "ocserv 配置检查未通过，未应用更改。请检查证书、地址池及原有额外配置；首次部署需先生成服务器证书。",
        "ocserv rejected the proposed configuration. No changes were applied. Check certificates, address pools and extra configuration; a new deployment needs server certificates first.",
      ),
      rollback_failed: t(
        "回滚未完成，请检查服务状态。上次配置保存在 /etc/ocserv/easy-backup。",
        "Rollback was incomplete. Check the service. Previous settings are in /etc/ocserv/easy-backup.",
      ),
      restart_failed: t(
        "服务未能重新启动，已尝试恢复原配置。请检查服务日志。",
        "The service did not restart. Restoration of the previous configuration was attempted. Check the service log.",
      ),
      terminate_failed: t(
        "未能确认该账号的所有登录已失效，账号修改已回滚。请检查 occtl 和服务状态后重试。",
        "Could not confirm session revocation. The account change was rolled back. Check occtl and the service, then retry.",
      ),
      disconnect_failed: t(
        "该连接可能已经断开，请刷新在线列表。",
        "The connection may already be closed. Refresh the online list.",
      ),
      forbidden: t(
        "当前登录只有查看权限。",
        "Your login has read-only access.",
      ),
      session_expired: t(
        "管理登录可能已过期，请刷新页面重新登录。",
        "Your administration login may have expired. Refresh and sign in again.",
      ),
      request_timeout: t(
        "请求超时，请检查网络后刷新。",
        "Request timed out. Check the network and refresh.",
      ),
      action_timeout: t(
        "等待操作结果超时，服务端可能仍在执行。请刷新确认状态后再操作。",
        "Timed out waiting for the result; the server may still be working. Refresh to check its state before another action.",
      ),
      internal_error: t(
        "管理页读取或应用失败。请运行套装中的 diagnose-n1.sh，查看故障位置后重试。",
        "The administration page could not read or apply settings. Run diagnose-n1.sh from the bundle to locate the failure.",
      ),
      invalid_pool: t(
        "请填写有效 IPv4 网段，掩码范围为 /8 至 /30。",
        "Enter a valid IPv4 network with a /8 to /30 mask.",
      ),
      pool_not_network: t(
        "地址池应填写网段地址，例如 10.77.0.0，不能填写 10.77.0.1。",
        "Use a network address such as 10.77.0.0, not 10.77.0.1.",
      ),
      invalid_dns: t(
        "请填写 1–3 个有效 DNS 服务器 IP，每行一个。",
        "Enter 1–3 valid DNS server IP addresses, one per line.",
      ),
      duplicate_dns: t("DNS 地址不能重复。", "DNS addresses must be unique."),
      dns_is_endpoint: t(
        "DNS 请使用服务端的隧道 IP，例如 10.77.0.1，不要使用 VPN 入口地址。",
        "Use the server tunnel IP for DNS, such as 10.77.0.1, rather than the VPN endpoint address.",
      ),
      invalid_routes: t(
        "请填写有效网段，每行一个，例如 192.168.19.0/24。",
        "Enter valid network routes, one per line, such as 192.168.19.0/24.",
      ),
      route_needs_ipv6: t(
        "下发 IPv6 路由前，需要先设置 IPv6 地址池。",
        "Configure an IPv6 address pool before advertising IPv6 routes.",
      ),
      invalid_ipv6_pool: t(
        "IPv6 地址池应使用地址/前缀格式，前缀范围为 16–112。",
        "Use an IPv6 address/prefix with a prefix length of 16–112.",
      ),
      invalid_url: t(
        "请输入完整的 HTTPS 地址，例如 https://vpn.example.com:4443。",
        "Enter a complete HTTPS address, such as https://vpn.example.com:4443.",
      ),
      invalid_cert_mode: t(
        "证书验证方式无效，请重新选择。",
        "Choose a valid certificate verification method.",
      ),
      certificate_init_failed: t(
        "证书初始化失败，请查看系统 ocserv 日志；检查 certtool、剩余空间及自定义证书路径。",
        "Certificate initialization failed. Check the ocserv system log, certtool, free space and custom certificate paths.",
      ),
      invalid_domain: t("默认域名格式无效。", "The default domain is invalid."),
      invalid_port: t(
        "端口范围为 1–65535。",
        "Ports must be between 1 and 65535.",
      ),
      invalid_client_limit: t(
        "同账号在线数不能大于总在线数。",
        "The per-account limit cannot exceed the total connection limit.",
      ),
      read_failed: t(
        "读取文件失败，请检查文件权限和存储状态后重试。",
        "Reading a file failed. Check file permissions and storage health, then retry.",
      ),
      write_failed: t(
        "保存失败，请检查剩余空间及文件权限。",
        "Saving failed. Check free space and file permissions.",
      ),
      service_failed: t(
        "服务操作失败，请检查 ocserv 日志。",
        "Service operation failed. Check the ocserv log.",
      ),
      unknown_schema: t(
        "未找到标准 ocserv UCI 配置，请先安装并初始化 ocserv。",
        "Standard ocserv UCI configuration was not found. Install and initialize ocserv first.",
      ),
      ca_unavailable: t(
        "服务器 CA 证书不可用，请先完成证书配置。",
        "The server CA certificate is unavailable. Complete certificate setup first.",
      ),
      firewall_unavailable: t(
        "无法读取基础防火墙配置，未应用网络设置。",
        "The baseline firewall configuration is unavailable; network settings were not applied.",
      ),
      firewall_section_conflict: t(
        "发现与 VPN 基础规则同名的自定义防火墙段，请先处理冲突。",
        "A custom firewall section conflicts with the managed VPN rules. Resolve it first.",
      ),
      firewall_check_failed: t(
        "防火墙配置检查失败，正在恢复原配置。",
        "Firewall configuration validation failed. Restoring the previous configuration.",
      ),
      firewall_reload_failed: t(
        "防火墙重载失败，正在恢复原配置。",
        "Firewall reload failed. Restoring the previous configuration.",
      ),
    };
    var effects = {
      saved: t(
        "已保存，下次启动服务时生效。",
        "Saved for the next service start.",
      ),
      updated_live: t(
        "账号已生效，其他在线用户保持连接。",
        "Account updated. Other users remain connected.",
      ),
      terminated: t(
        "账号已更新，该账号的旧登录已失效。",
        "Account updated and its previous sessions revoked.",
      ),
      restarted: t(
        "配置已应用，VPN 服务已重新启动。",
        "Configuration applied and VPN service restarted.",
      ),
      disconnected: t(
        "已断开该连接。账号仍可重新登录。",
        "Connection disconnected. The account can sign in again.",
      ),
      service_changed: t(
        "服务操作已执行，状态将自动刷新。",
        "Service operation submitted. Status will refresh automatically.",
      ),
    };
    effects.address_saved = t(
      "连接地址已保存，当前 VPN 连接保持不变。",
      "Connection address saved. Current VPN connections are unchanged.",
    );
    effects.firewall_repaired = t(
      "基础 VPN 防火墙规则已同步。",
      "Baseline VPN firewall rules were synchronized.",
    );
    effects.profile_settings_saved = t(
      "客户端地址和证书验证方式已保存。",
      "Client address and certificate verification preference saved.",
    );
    effects.certificates_initialized = t(
      "服务端证书已就绪，可以下载客户端连接配置。",
      "The server certificate is ready. You can download client profiles.",
    );
    effects.unchanged = t(
      "内容没有变化，无需重复保存。",
      "Nothing changed; no update was needed.",
    );
    effects.guard_queued = t(
      "正在配置，仅需稍候。页面会自动检查管理连接；无法连接时会自动恢复。",
      "Applying settings. This page checks connectivity automatically and restores settings if it cannot reconnect.",
    );
    effects.guard_restoring = t(
      "正在关闭并恢复启用前的设置…",
      "Disabling and restoring the previous settings…",
    );
    effects.guard_enabled = t(
      "已开启，仅 VPN 用户可通过 N1 使用代理上网。",
      "Enabled. Only VPN clients can access the proxy through this N1.",
    );
    effects.guard_disabled = t(
      "已关闭，已恢复启用前的设置。",
      "Disabled. Previous settings restored.",
    );
    return { errors: errors, effects: effects };
  };
})();

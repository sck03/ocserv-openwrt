/* SPDX-License-Identifier: GPL-3.0-or-later */
window.OcservEasyGuard = function ({ t, element, array, errors, showReader, button, mutationButton, complete, notice, errorText, confirm, writable }) {
  function showGuardLog() {
    var steps = {
      proxy_connect: t("VPN 代理接入", "VPN proxy connection"),
      request: t("操作请求", "Request"),
      confirmation: t("管理连接确认", "Management confirmation"),
      apply: t("启用配置", "Apply"),
      restore: t("恢复配置", "Restore"),
      operation_failed: t("操作失败", "Operation failed"),
      restore_failed: t("恢复失败", "Restore failed"),
      firewall_reload: t("重载防火墙", "Reload firewall"),
      firewall_check: t("检查防火墙", "Check firewall"),
      dns_restart: t("重启 DNS", "Restart DNS"),
      openclash_restart: t("重启 OpenClash", "Restart OpenClash"),
      openclash_ready: t("等待 OpenClash", "Wait for OpenClash"),
      nikki_restart: t("重启 Nikki", "Restart Nikki"),
      nikki_ready: t("等待 Nikki", "Wait for Nikki"),
      momo_restart: t("重启 Momo", "Restart Momo"),
      momo_ready: t("等待 Momo", "Wait for Momo"),
      ocserv_restart: t("重启 VPN 服务", "Restart VPN"),
      ocserv_ready: t("等待 VPN 服务", "Wait for VPN"),
      ocserv_config_check: t("检查 VPN 配置", "Check VPN configuration"),
      nft_check: t("检查隔离规则", "Check isolation rules"),
      nft_apply: t("应用隔离规则", "Apply isolation rules"),
    };
    var codes = {
      start: t("开始", "Started"),
      completed: t("完成", "Completed"),
      running: t("已运行", "Running"),
      timeout: t("等待超时", "Timed out"),
      pending: t("等待管理连接确认", "Awaiting management confirmation"),
      enabled: t("已开启", "Enabled"),
      enable: t("开启", "Enable"),
      disable: t("关闭并恢复", "Disable and restore"),
      confirm: t("确认", "Confirm"),
      boot: t("开机恢复", "Boot recovery"),
    };
    showReader(
      t("VPN 专用上网服务日志", "VPN-only service log"),
      [
        element("p", {
          className: "easy-muted",
          text: t(
            "仅保留最近 24 小时、最多 256 条日志；自动删除过期记录，设备重启后清空。记录操作步骤、退出码和常见错误类型，不保存账号、密码或订阅内容。",
            "Keeps up to 256 entries for 24 hours, automatically expires them, and clears on router reboot. Records steps, exit codes and common error categories without accounts, passwords or subscriptions.",
          ),
        }),
      ],
      t("刷新日志", "Refresh log"),
      "guard-log",
      function (log) {
        return (
          array(log.entries)
            .map(function (row) {
              var code = codes[row.code] || errors[row.code] || row.code;
              if (/^exit_/.test(row.code))
                code =
                  t("退出码 ", "Exit code ") +
                  row.code.slice(5) +
                  (row.code === "exit_124"
                    ? t("（超时或输出超限）", " (timeout or output limit)")
                    : "");
              return (
                new Date(row.time * 1000).toLocaleString() +
                "  " +
                (steps[row.step] || row.step) +
                "：" +
                code +
                (row.detail ? " — " + row.detail : "")
              );
            })
            .join("\n") ||
          t("最近 24 小时暂无日志。", "No logs in the last 24 hours.")
        );
      },
    );
  }
  function showProxyStatus() {
    function status(value) {
      return value === true
        ? t("正常", "OK")
        : value === false
          ? t("未就绪", "Not ready")
          : t("未知", "Unknown");
    }
    function listener(value) {
      return value === true
        ? t("已监听", "Listening")
        : value === false
          ? t("未监听", "Not listening")
          : t("未知", "Unknown");
    }
    showReader(
      t("代理接入诊断", "Proxy integration diagnostics"),
      [],
      t("重新检查", "Check again"),
      "proxy-status",
      function (p) {
        var lines = [
          t("代理：", "Proxy: ") + (p.label || p.proxy),
          t("核心进程：", "Core process: ") + status(p.running),
        ];
        if (p.supported === false) lines.push(errors.guard_proxy_unsupported);
        else {
          lines.push(
            t("VPN 入口：", "VPN ingress: ") + status(p.ingress_ready),
          );
          if (p.ingress_ready === false)
            lines.push(
              t(
                "VPN 流量尚未接入代理，请查看接入日志。",
                "VPN traffic is not connected to the proxy. Check the integration log.",
              ),
            );
          if (p.tcp_mode || p.udp_mode) lines.push("TCP: " + p.tcp_mode + "; UDP: " + p.udp_mode);
          [
            ["lan_dns_hijack", "DNS"],
            ["openclash_dns_redirect", "DNS"],
            ["lan_redirect", "Redirect"],
            ["lan_tproxy", "TPROXY"],
          ].forEach(function (item) {
            var target = (p.targets || {})[item[0]];
            if (!target) return;
            lines.push(
              item[1] + t(" 规则：", " rules: ") + status(target.available),
            );
            array(target.ports).forEach(function (port) {
              lines.push(
                "  " +
                  port.port +
                  "  TCP: " +
                  listener(port.tcp) +
                  "; UDP: " +
                  listener(port.udp),
              );
            });
            if (!array(target.ports).length)
              lines.push(t("  未发现目标端口", "  No target port found"));
          });
          if (p.tun)
            lines.push(
              "TUN: " +
                (p.tun.device || "?") +
                "; " +
                t("路由表 ", "table ") +
                (p.tun.table || "?") +
                "; " +
                t("策略规则：", "policy rule: ") +
                status(p.tun.rule),
            );
          if (p.tcp_mode === "redirect")
            lines.push(
              t(
                "Redirect 使用 TCP；UDP 按上方所选模式接管。",
                "Redirect uses TCP; UDP follows the mode shown above.",
              ),
            );
        }
        lines.push(
          t(
            "端口来自当前运行规则；监听存在不代表节点或外网已连通。",
            "Ports come from live rules; a listener does not verify the upstream node or internet connection.",
          ),
        );
        return lines.join("\n");
      },
    );
  }
  function renderGuard(state, content) {
    var g = state.guard || { phase: "disabled" },
      labels = {
        disabled: t("已关闭", "Disabled"),
        queued: t("等待应用…", "Queued…"),
        applying: t("正在应用…", "Applying…"),
        pending: t("正在确认管理连接…", "Checking management connectivity…"),
        enabled: t("已开启", "Enabled"),
        restoring: t("正在恢复…", "Restoring…"),
        recovery_failed: t("恢复需要重试", "Restoration needs a retry"),
      };
    var proxyName = { openclash: "OpenClash", nikki: "Nikki", momo: "Momo" }[
      g.proxy
    ];
    var switching = ["queued", "applying", "pending", "restoring"].includes(
      g.phase,
    );
    var toggle = mutationButton(
      g.enabled ? t("关闭并恢复", "Disable and restore") : t("开启", "Enable"),
      function () {
        if (g.enabled)
          complete({ action: "guard", command: "disable" }).catch(function (e) {
            notice(errorText(e), true);
          });
        else
          confirm(
            t("开启仅 VPN 上网", "Enable VPN-only access"),
            t("将为 ", "Configure ") +
              (proxyName || "") +
              t(
                " 设置 VPN 入口、DNS 重定向、访问控制和隔离防火墙，并短暂重启相关服务。适用于 N1 旁路由，保留当前电脑的管理访问；关闭时恢复原设置。Nikki/Momo 升级前须先关闭本开关。",
                " with VPN ingress, DNS redirection, access controls and an isolation firewall, then briefly restart related services. For N1 side routers; retains management access and restores settings on disable. Disable this switch before upgrading Nikki/Momo.",
              ),
            { action: "guard", command: "enable" },
          );
      },
      g.enabled ? "cbi-button-reset" : "cbi-button-apply",
    );
    toggle.disabled = !writable() || !state.permissions.guard || switching || (!g.enabled && !g.available);
    if (!state.permissions.guard) toggle.title = errors.forbidden;
    var children = [
      element("div", { className: "easy-heading" }, [
        element("h3", {
          text: t(
            "仅 VPN 用户可使用代理",
            "Only VPN clients may use the proxy",
          ),
        }),
        element("strong", {
          className: g.phase === "enabled" ? "easy-good" : "easy-muted",
          text: labels[g.phase] || g.phase,
        }),
      ]),
      element("p", {
        text: t(
          "开启后，未登录 VPN 的局域网设备不能把 N1 当作网关、DNS 或代理服务器使用。普通设备通过主路由上网不受影响。",
          "When enabled, LAN devices without VPN cannot use this N1 as a gateway, DNS or proxy server. Internet access through the main router continues normally.",
        ),
      }),
      element("p", {
        className: "easy-muted",
        text: t(
          "支持 OpenClash、Nikki、Momo，自动识别唯一启用的代理。默认关闭，不修改订阅、节点和分流规则。",
          "Supports OpenClash, Nikki and Momo; detects the single enabled proxy. Disabled by default; keeps subscriptions, nodes and routing rules.",
        ),
      }),
    ];
    if (proxyName)
      children.push(
        element("p", { text: t("联动代理：", "Proxy: ") + proxyName }),
      );
    if (g.lan)
      children.push(
        element("p", {
          text:
            t("N1 地址：", "N1 address: ") +
            g.lan +
            "　" +
            t("管理电脑：", "Management computer: ") +
            g.admin +
            "　" +
            t("VPN 网段：", "VPN pool: ") +
            g.pool,
        }),
      );
    if (g.reason)
      children.push(
        element("p", {
          className: "easy-notice error",
          text: errorText({ code: g.reason }),
        }),
      );
    if (g.phase === "disabled" && g.last && g.last.failure)
      children.push(
        element("p", {
          className: "easy-notice error",
          text:
            t("上次应用已恢复：", "Previous apply restored: ") +
            errorText({ code: g.last.failure }),
        }),
      );
    if (g.phase === "disabled" && g.last && g.last.preserved)
      children.push(
        element("p", {
          className: "easy-muted",
          text: t(
            "已保留启用期间由其他页面修改的设置。",
            "Settings changed in other pages while enabled were preserved.",
          ),
        }),
      );
    var diagnose = button(t("检查代理", "Check proxy"), showProxyStatus);
    diagnose.disabled = switching;
    children.push(
      element("div", { className: "easy-buttons" }, [
        toggle,
        button(t("查看日志", "View log"), showGuardLog),
        diagnose,
      ]),
    );
    content.appendChild(
      element("section", { className: "easy-card" }, children),
    );
  }
  return renderGuard;
};

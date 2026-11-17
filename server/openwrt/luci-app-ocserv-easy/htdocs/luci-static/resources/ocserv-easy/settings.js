/* SPDX-License-Identifier: GPL-3.0-or-later */
window.OcservEasySettings = function ({ t, element, field, array, writable, options, errors, confirm }) {
  "use strict";
  function renderSettings(state, content) {
    var s = state.settings,
      inputs = {};
    function textInput(key, label, help, attributes) {
      var input = element(
        "input",
        Object.assign({ value: s[key] || "" }, attributes || {}),
      );
      inputs[key] = input;
      return field(label, input, help);
    }
    function flag(key, label, help) {
      var input = element("input", {
        type: "checkbox",
        checked: s[key] === "1",
      });
      inputs[key] = input;
      return element("div", { className: "easy-field" }, [
        element("label", { className: "easy-check" }, [input, label]),
        element("small", { className: "easy-muted", text: help || "" }),
      ]);
    }
    var general = element("div", { className: "easy-fields" }, [
      textInput(
        "port",
        t("服务端口（TCP）", "Service port (TCP)"),
        t(
          "使用 4443 可避开路由器 HTTPS 管理端口。",
          "4443 avoids the router HTTPS administration port.",
        ),
        { type: "number", min: 1, max: 65535, required: true },
      ),
      textInput(
        "max_clients",
        t("最大在线设备数", "Maximum online devices"),
        null,
        { type: "number", min: 1, max: 4096, required: true },
      ),
      textInput(
        "max_same",
        t("每个账号最多在线设备数", "Devices per account"),
        t(
          "设为 1 可限制一个账号同时一台设备。",
          "Set to 1 to allow one device per account.",
        ),
        { type: "number", min: 1, max: 64, required: true },
      ),
      textInput(
        "dpd",
        t("断线检测间隔（秒）", "Dead-peer detection (seconds)"),
        t("通常使用 60–120 秒。", "60–120 seconds is typical."),
        { type: "number", min: 10, max: 3600, required: true },
      ),
      textInput(
        "ipaddr",
        t("VPN IPv4 网段", "VPN IPv4 network"),
        t(
          "例如 10.77.0.0，避免与现有局域网重叠。",
          "For example 10.77.0.0. Avoid overlap with existing LANs.",
        ),
        { required: true },
      ),
      textInput(
        "netmask",
        t("子网掩码", "Subnet mask"),
        t("例如 255.255.255.0 或 24。", "For example 255.255.255.0 or 24."),
        { required: true },
      ),
    ]);
    var dns = element("textarea", {
      value: array(s.dns).join("\n"),
      required: true,
    });
    inputs.dns = dns;
    var routeMode = element("select", {}, [
      element("option", {
        value: "all",
        text: t("全部流量通过 VPN", "All traffic through VPN"),
      }),
      element("option", {
        value: "split",
        text: t("仅指定网段通过 VPN", "Only selected networks"),
      }),
    ]);
    routeMode.value = s.route_mode;
    inputs.route_mode = routeMode;
    var routes = element("textarea", {
      value: array(s.routes).join("\n"),
      placeholder: "192.168.19.0/24",
    });
    inputs.routes = routes;
    var routesField = field(
      t("VPN 路由", "VPN routes"),
      routes,
      t(
        "每行一个网段，例如 192.168.19.0/24。",
        "One network per line, such as 192.168.19.0/24.",
      ),
      true,
    );
    function routeVisibility() {
      routesField.hidden = routeMode.value !== "split";
      routes.required = routeMode.value === "split";
    }
    routeMode.addEventListener("change", routeVisibility);
    routeVisibility();
    general.append(
      field(
        t("DNS 服务器", "DNS servers"),
        dns,
        t(
          "每行一个 IP；使用本机 DNS 时填写隧道地址，例如 10.77.0.1。",
          "One IP per line. For DNS on this router, use its tunnel address, such as 10.77.0.1.",
        ),
      ),
      field(t("流量路由方式", "Traffic routing"), routeMode),
      routesField,
    );
    var advanced = element("details", {}, [
      element("summary", { text: t("更多设置", "More settings") }),
      element("div", { className: "easy-fields" }, [
        flag(
          "udp",
          t("启用 UDP 加速（推荐）", "Enable UDP acceleration (recommended)"),
          t(
            "UDP 不通时客户端可回退 TCP。",
            "Clients can fall back to TCP when UDP is unavailable.",
          ),
        ),
        textInput(
          "udp_port",
          t("UDP 端口", "UDP port"),
          t("留空表示与 TCP 端口相同。", "Leave blank to use the TCP port."),
          { type: "number", min: 1, max: 65535 },
        ),
        flag(
          "predictable_ips",
          t("同账号优先分配相同 IP", "Prefer the same IP for each account"),
        ),
        flag(
          "cisco_compat",
          t("兼容 Cisco AnyConnect", "Cisco AnyConnect compatibility"),
        ),
        flag(
          "compression",
          t("启用压缩", "Enable compression"),
          t(
            "通常关闭，减少 CPU 开销。",
            "Usually disabled to reduce CPU work.",
          ),
        ),
        textInput(
          "default_domain",
          t("默认域名", "Default domain"),
          t("可留空。", "Optional."),
        ),
        textInput(
          "ip6addr",
          t("VPN IPv6 地址池", "VPN IPv6 pool"),
          t("可留空，例如 fd77::/64。", "Optional, for example fd77::/64."),
        ),
        textInput(
          "easy_public_url",
          t("员工连接地址", "Employee connection address"),
          t(
            "填写客户端可访问的 HTTPS 域名或 IP；外网用户应使用公网入口。",
            "Use an HTTPS hostname or IP reachable by clients; remote users need a public endpoint.",
          ),
          { placeholder: "https://vpn.example.com:4443" },
        ),
      ]),
    ]);
    var save = element("button", {
      type: "submit",
      className: "cbi-button cbi-button-apply",
      text: t("保存并应用", "Save and apply"),
      disabled:
        !writable() ||
        options.firewall_writable === false ||
        !state.settings_supported ||
        state.firewall_sync === false ||
        !!(state.guard && state.guard.enabled),
    });
    var form = element("form", {}, [
      general,
      advanced,
      element("div", { className: "easy-save-bar" }, [
        element("span", {
          className: "easy-muted",
          text: t(
            "应用前检查配置；运行中的 VPN 会短暂重启。",
            "Checks configuration before applying; a running VPN briefly restarts.",
          ),
        }),
        save,
      ]),
    ]);
    form.addEventListener("submit", function (event) {
      event.preventDefault();
      var values = {};
      Object.keys(inputs).forEach(function (key) {
        var input = inputs[key];
        values[key] =
          input.type === "checkbox" ? (input.checked ? "1" : "0") : input.value;
      });
      values.dns = dns.value
        .split(/\r?\n/)
        .map(function (x) {
          return x.trim();
        })
        .filter(Boolean);
      values.routes = routes.value
        .split(/\r?\n/)
        .map(function (x) {
          return x.trim();
        })
        .filter(Boolean);
      confirm(
        t("应用服务设置", "Apply service settings"),
        state.running
          ? t(
              "网络参数变更会短暂重启 VPN。仅修改员工连接地址不会中断当前连接。确认保存？",
              "Network changes briefly restart VPN. Changing only the employee connection address keeps current connections. Save these settings?",
            )
          : t(
              "保存后在下次启动服务时生效。",
              "Settings take effect on the next service start.",
            ),
        { action: "settings", settings: values, allow_restart: true },
      );
    });
    content.appendChild(
      element("section", { className: "easy-card" }, [
        element("h3", { text: t("常用服务设置", "Common service settings") }),
        form,
      ]),
    );
    if (!state.settings_supported)
      content.appendChild(
        element("p", {
          className: "easy-notice error",
          text: t(
            "当前使用自定义认证或代理 ARP。请先恢复普通账号认证和独立 VPN 地址池，再使用本页管理网络。",
            "Custom authentication or proxy ARP is configured. Restore password authentication and a separate VPN pool before managing networking here.",
          ),
        }),
      );
    if (state.firewall_sync === false)
      content.appendChild(
        element("p", {
          className: "easy-notice error",
          text: errors.firewall_unavailable,
        }),
      );
    if (state.guard && state.guard.enabled)
      content.appendChild(
        element("p", {
          className: "easy-muted",
          text: errors.guard_settings_locked,
        }),
      );
  }
  return renderSettings;
};

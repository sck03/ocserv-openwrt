/* SPDX-License-Identifier: GPL-3.0-or-later */
window.OcservEasyExport = function ({ t, element, field, options, mutationButton, button, complete, notice, errorText }) {
  async function download(kind, server, platform) {
    var controller = new AbortController();
    var timer = setTimeout(function () { controller.abort(); }, 15000);
    try {
      var response = await fetch(options.base + "/download", {
        method: "POST", credentials: "same-origin", cache: "no-store", signal: controller.signal,
        body: new URLSearchParams({token: options.token, kind: kind, server: server, platform: platform || "windows"}),
      });
      var type = response.headers.get("Content-Type") || "";
      if (type.includes("application/json")) {
        var error = await response.json();
        throw {code: error.error || "network_error"};
      }
      if (!response.ok || !type.includes("application/octet-stream")) throw {code: "session_expired"};
      var url = URL.createObjectURL(await response.blob());
      var link = element("a", {href: url, download: kind === "ca" ? "ca.pem" : "Lansyra.vpn"});
      document.body.appendChild(link);
      link.click();
      link.remove();
      setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
    } catch (error) {
      notice(errorText(controller.signal.aborted ? {code: "request_timeout"} : error), true);
    } finally { clearTimeout(timer); }
  }
  function renderExport(state, content) {
    var targets = state.client_targets, trustModes = state.client_trust;
    var platform = element("select", {}, targets.map(function (target) {
      return element("option", {value: target.id, text: target.label});
    }));
    platform.value = state.client_profile.platform;
    var server = element("input", {
      value:
        state.settings.easy_public_url ||
        (state.guard && state.guard.lan
          ? state.guard.lan + ":" + state.settings.port
          : ""),
      type: "text",
      required: true,
      maxLength: 512,
      placeholder: "192.168.19.253:4443",
    });
    var trust = element("select", {}, [
      element("option", {
        value: "pin",
        text: t(
          "固定服务器指纹（推荐）",
          "Pin the server public key (recommended)",
        ),
        disabled: !trustModes.pin.available,
      }),
      element("option", {
        value: "address",
        text: t(
          "首次连接时确认证书",
          "Confirm certificate on first connection",
        ),
      }),
      element("option", {
        value: "profile",
        text: t("附带公共 CA 证书", "Include the public CA certificate"),
        disabled: !trustModes.profile.available,
      }),
    ]);
    trust.value = (state.client_profile || {}).cert_mode || "pin";
    var profile = element("button", {
      type: "submit",
      className: "cbi-button cbi-button-apply",
      text: t("下载 .vpn 连接配置", "Download .vpn profile"),
    });
    var compatibility = element("p", {className:"easy-notice error", hidden:true});
    var save = mutationButton(
      t("保存客户端设置", "Save client settings"),
      function () {
        if (!form.reportValidity()) return;
        complete({
          action: "client_profile",
          server: server.value.trim(),
          cert_mode: trust.value,
          platform: platform.value,
        }).catch(function (error) {
          notice(errorText(error), true);
        });
      },
    );
    var readOnly = save.disabled, downloading = false;
    async function runDownload(kind, address, target) {
      if (downloading) return;
      downloading = true; updateDownload(); ca.disabled = true;
      try { await download(kind, address, target); }
      finally { downloading = false; updateDownload(); ca.disabled = !trustModes.profile.available; }
    }
    function updateDownload() {
      var target = targets.find(function (item) { return item.id === platform.value; });
      var capability = trustModes[trust.value];
      var reason = !target || !target.modes.includes(trust.value) ? "unsupported_client_profile" :
        !capability || !capability.available ? (capability && capability.error) || "ca_unavailable" : null;
      compatibility.hidden = !reason;
      compatibility.textContent = reason ? errorText({code: reason}) : "";
      var option = trust.selectedOptions[0];
      profile.disabled = downloading || !!reason || !option || option.disabled;
      save.disabled = readOnly || profile.disabled;
    }
    function selectPlatform() {
      var target = targets.find(function (item) { return item.id === platform.value; });
      if (!target) { updateDownload(); return; }
      Array.from(trust.options).forEach(function (option) {
        option.disabled = !target.modes.includes(option.value) ||
          !trustModes[option.value].available;
      });
      if (!target.modes.includes(trust.value)) trust.value = target.modes[0];
      updateDownload();
    }
    platform.addEventListener("change", selectPlatform);
    trust.addEventListener("change", updateDownload);
    selectPlatform();
    var form = element("form", {}, [
      field(t("目标客户端", "Target client"), platform,
        t("仅提供该客户端支持的证书方式。所有客户端均可导入版本 1 的 .vpn 公共配置。",
          "Only supported certificate methods are offered. Every client can import version 1 public .vpn profiles.")),
      field(
        t("客户端实际连接的服务器地址", "Server address used by clients"),
        server,
        t(
          "例如 vpn.example.com:4443 或 [2001:db8::1]:4443。外网请填写实际公网地址和外部端口。",
          "For example vpn.example.com:4443 or [2001:db8::1]:4443. Remote clients need the actual public address and external port.",
        ),
      ),
      field(t("证书验证方式", "Certificate verification"), trust),
      compatibility,
      element("p", {
        text: t(
          "固定指纹会校验服务器公钥，适用于局域网自签证书、域名和 IPv6。更换公钥后需重新分发配置。选择 CA 时，证书必须包含填写的域名或 IP。保存后刷新页面仍保留所选方式；下载的配置使用当前选择。",
          "Pinning verifies the server public key and supports self-signed LAN certificates, hostnames and IPv6. Redistribute profiles after changing the key. CA verification requires a certificate covering the entered hostname or IP. Saving retains the selection after refresh; downloads use the current selection.",
        ),
      }),
      element("p", {
        className: "easy-muted",
        text: t(
          "请从可信的管理连接下载并分发配置。导入后填写各自的账号密码；配置不包含账号、密码或私钥。",
          "Download and distribute profiles through a trusted management connection. Users enter their own credentials after import; profiles contain no usernames, passwords or private keys.",
        ),
      }),
      element("div", { className: "easy-buttons" }, [save, profile]),
    ]);
    if (!state.certificates_ready)
      content.appendChild(
        element("section", { className: "easy-card" }, [
          element("p", {
            className: "easy-notice error",
            text: t(
              "服务端证书尚未就绪。局域网也需要 TLS 证书；可自动生成本地证书，无需域名。",
              "The server certificate is not ready. LAN connections also require TLS certificates. A local certificate can be generated without a hostname.",
            ),
          }),
          mutationButton(
            t("生成缺失证书", "Generate missing certificates"),
            function () {
              complete({ action: "initcerts" }).catch(function (error) {
                notice(errorText(error), true);
              });
            },
            "cbi-button-apply",
          ),
        ]),
      );
    form.addEventListener("submit", function (event) {
      event.preventDefault();
      if (profile.disabled) return;
      runDownload(trust.value, server.value.trim(), platform.value);
    });
    var ca = button(t("下载 ca.pem", "Download ca.pem"), function () {
      runDownload("ca", "");
    });
    ca.disabled = !trustModes.profile.available;
    if (trustModes.profile.error) ca.title = errorText({code: trustModes.profile.error});
    content.appendChild(
      element("section", { className: "easy-card" }, [
        element("h3", {
          text: t("分发客户端配置", "Distribute client profiles"),
        }),
        form,
      ]),
    );
    content.appendChild(
      element("section", { className: "easy-card" }, [
        element("h3", {
          text: t("单独下载 CA 证书", "Download the CA certificate"),
        }),
        element("p", {
          className: "easy-muted",
          text: t(
            "适用于已有服务器地址的岚序 Lansyra，或其他支持导入 CA 的客户端。",
            "For Lansyra with a configured server address, or another client that can import a CA.",
          ),
        }),
        ca,
        trustModes.profile.error ? element("p", {className: "easy-notice error", text: errorText({code: trustModes.profile.error})}) : null,
      ]),
    );
    content.appendChild(
      element("section", { className: "easy-card" }, [
        element("h3", {
          text: t("从外网连接", "Connecting over the Internet"),
        }),
        element("p", {
          text: t(
            "域名需要解析到可达的公网 IP。IPv4 旁路由部署需要在主路由转发 VPN 的 TCP/UDP 端口；IPv6 需要两端都有 IPv6，并在主路由放行 N1 的 VPN 端口。",
            "The hostname must resolve to a reachable public IP. An IPv4 side router needs TCP/UDP forwarding on the main router. IPv6 requires connectivity on both ends and a firewall rule allowing the N1 VPN ports.",
          ),
        }),
        element("p", {
          className: "easy-muted",
          text: t(
            "运营商内网无法仅靠域名实现外网直连；可使用可达的公网 IPv6、申请公网 IPv4，或部署公网中转。请用手机流量等独立网络验收。",
            "A hostname alone cannot make a server behind carrier NAT reachable. Use public IPv6, request a public IPv4 address, or deploy a public relay. Test from a separate network, such as mobile data.",
          ),
        }),
      ]),
    );
  }
  return renderExport;
};

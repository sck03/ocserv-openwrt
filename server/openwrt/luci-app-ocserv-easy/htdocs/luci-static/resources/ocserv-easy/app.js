/* SPDX-License-Identifier: GPL-3.0-or-later */
(function () {
  "use strict";
  var options = JSON.parse(
    document.getElementById("ocserv-easy-options").textContent,
  );
  var root = document.getElementById("ocserv-easy");
  var chinese = /^zh/i.test(options.language || "");
  var state,
    activeTab = "users",
    busy = false,
    modal = null,
    readerOpen = false,
    messageBox,
    content,
    statusBox,
    serviceButton;
  var listViews = {
      users: { query: "", page: 0 },
      online: { query: "", page: 0 },
    },
    accountList = null,
    onlineList = null,
    onlineError = null;
  var onlineCounts = Object.create(null);
  var statusKey = null,
    modalCleanup = null,
    modalTrigger = null;
  var element = window.OcservEasyDOM.element;
  var api = window.OcservEasyAPI(options),
    request = api.request,
    readSlot = api.readSlot;
  var messages = window.OcservEasyMessages(t),
    errors = messages.errors,
    effects = messages.effects;
  function t(zh, en) {
    return chinese ? zh : en;
  }
  function array(value) {
    return Array.isArray(value) ? value : [];
  }
  function button(label, handler, kind) {
    return element("button", {
      type: "button",
      className: "cbi-button " + (kind || "cbi-button-neutral"),
      text: label,
      onClick: handler,
    });
  }
  function writable() {
    return !!options.writable && !!state && state.supported;
  }
  function mutationButton(label, handler, kind) {
    var b = button(label, handler, kind);
    b.disabled = !writable();
    return b;
  }
  function errorText(error) {
    return (
      errors[error.code] ||
      t(
        "操作失败，请检查输入或刷新后重试。",
        "Operation failed. Check the input or refresh and retry.",
      ) +
        " (" +
        String(error.code || "network_error") +
        ")"
    );
  }
  function notice(text, isError) {
    messageBox.textContent = text;
    messageBox.className = "easy-notice" + (isError ? " error" : "");
    messageBox.hidden = !text;
  }
  async function complete(payload, preserveDialog) {
    if (busy) throw { code: "busy" };
    busy = true;
    // One operation owns both the submission and its refreshed revision.
    ++loadGeneration;
    loadRead.cancel();
    statusRead.cancel();
    var controls = Array.from(
      root.querySelectorAll("button,input,select,textarea"),
    ).map(function (node) {
      var old = node.disabled;
      node.disabled = true;
      return [node, old];
    });
    try {
      payload.revision = state.revision;
      var result = await request("action", payload);
      try {
        if (!(await load(preserveDialog))) return;
      } catch (_) {
        // The write succeeded. Do not present a retryable save failure.
        notice(
          t(
            "操作已成功，但状态刷新失败。请刷新页面确认最新状态。",
            "The operation succeeded, but refreshing its status failed. Refresh to see the latest state.",
          ),
          true,
        );
        return;
      }
      if (
        payload.action === "guard" &&
        state.guard &&
        state.guard.phase === "disabled" &&
        payload.command === "disable"
      )
        result.effect = "guard_disabled";
      notice(effects[result.effect] || t("操作完成。", "Done."));
    } finally {
      busy = false;
      controls.forEach(function (pair) {
        pair[0].disabled = pair[1];
      });
    }
  }
  function removeModal() {
    if (modalCleanup) modalCleanup();
    modalCleanup = null;
    if (modal) modal.remove();
    modal = null;
    readerOpen = false;
    if (modalTrigger && modalTrigger.isConnected) modalTrigger.focus();
    modalTrigger = null;
  }
  function closeModal() {
    if (modal && !busy) {
      var refresh = readerOpen;
      removeModal();
      if (refresh) renderTab();
    }
  }
  function showModal(title, children, submit, label, cleanup) {
    closeModal();
    modalTrigger = document.activeElement;
    modalCleanup = cleanup;
    loadRead.cancel();
    statusRead.cancel();
    var errorBox = element("div", {
      className: "easy-notice error",
      hidden: true,
    });
    var submitButton = element("button", {
      type: "submit",
      className: "cbi-button cbi-button-apply",
      text: label || t("保存", "Save"),
    });
    var form = element(
      "form",
      {},
      [element("h3", { id: "easy-modal-title", text: title })].concat(
        children,
        [
          errorBox,
          element("div", { className: "easy-buttons" }, [
            button(t("取消", "Cancel"), closeModal),
            submitButton,
          ]),
        ],
      ),
    );
    form.addEventListener("input", function () {
      errorBox.hidden = true;
    });
    form.addEventListener("submit", async function (event) {
      event.preventDefault();
      if (busy) return;
      errorBox.hidden = true;
      try {
        await submit();
        closeModal();
      } catch (error) {
        errorBox.textContent = errorText(error);
        errorBox.hidden = false;
      }
    });
    modal = element("div", { className: "easy-modal-backdrop" }, [
      element(
        "div",
        {
          className: "easy-modal",
          role: "dialog",
          "aria-modal": "true",
          "aria-labelledby": "easy-modal-title",
        },
        [form],
      ),
    ]);
    modal.addEventListener("keydown", function (event) {
      if (event.key === "Escape") {
        event.preventDefault();
        closeModal();
      }
      if (event.key === "Tab") {
        var fields = Array.from(
          modal.querySelectorAll(
            'input,button,select,textarea,summary,[tabindex="0"]',
          ),
        ).filter(function (n) {
          return !n.disabled && n.getClientRects().length;
        });
        var first = fields[0],
          last = fields[fields.length - 1];
        if (!first) event.preventDefault();
        else if (event.shiftKey && document.activeElement === first) {
          event.preventDefault();
          last.focus();
        } else if (!event.shiftKey && document.activeElement === last) {
          event.preventDefault();
          first.focus();
        }
      }
    });
    root.appendChild(modal);
    var first = form.querySelector(
      'input:not([type="checkbox"]),select,button',
    );
    if (first) first.focus();
  }
  function showReader(title, children, refreshLabel, path, format) {
    var output = element("pre", { className: "easy-guard-log", tabindex: "0" }),
      reader = readSlot(path),
      closed = false;
    async function refresh() {
      if (reader.pending || closed) return;
      output.textContent = t("正在读取…", "Loading…");
      try {
        await reader.run(function (data) {
          output.textContent = format(data);
          output.scrollTop = output.scrollHeight;
        });
      } catch (error) {
        if (!closed) output.textContent = errorText(error);
      }
    }
    showModal(
      title,
      children.concat([button(refreshLabel, refresh), output]),
      async function () {},
      t("关闭", "Close"),
      function () {
        closed = true;
        reader.cancel();
      },
    );
    readerOpen = true;
    refresh();
  }
  function confirm(title, description, payload) {
    showModal(
      title,
      [element("p", { text: description })],
      function () {
        return complete(payload);
      },
      t("确认", "Confirm"),
    );
  }
  function field(label, input, help, wide) {
    if (!input.id)
      input.id = "easy-field-" + Math.random().toString(36).slice(2);
    return element(
      "div",
      { className: "easy-field" + (wide ? " wide" : "") },
      [element("label", { for: input.id, text: label }), input].concat(
        help ? [element("small", { className: "easy-muted", text: help })] : [],
      ),
    );
  }
  function editUser(user) {
    var name = element("input", {
      value: user ? user.name : "",
      maxlength: 64,
      required: true,
      autocomplete: "off",
      pattern: "[A-Za-z0-9][A-Za-z0-9_.@\\-]*",
    });
    var needsPassword = !user || !!user.needs_password;
    var password = element("input", {
      type: "password",
      autocomplete: "new-password",
      minlength: 8,
      maxlength: 128,
      required: needsPassword,
    });
    var repeat = element("input", {
      type: "password",
      autocomplete: "new-password",
      maxlength: 128,
      required: needsPassword,
    });
    var group = element("input", {
      value: user ? user.group : "*",
      maxlength: 128,
    });
    var enabled = element("input", {
      type: "checkbox",
      checked: !user || user.enabled || user.needs_password,
    });
    var reveal = element("input", {
      type: "checkbox",
      onChange: function () {
        password.type = repeat.type = reveal.checked ? "text" : "password";
      },
    });
    var fields = element("div", { className: "easy-fields" }, [
      field(
        t("登录账号", "Username"),
        name,
        t(
          "支持英文、数字及 . _ - @",
          "Letters, digits and . _ - @ are supported.",
        ),
        true,
      ),
      field(
        user ? t("新密码", "New password") : t("密码", "Password"),
        password,
        needsPassword
          ? t(
              "至少 8 个字符，保存后即可用于登录。",
              "At least 8 characters. Ready for login after saving.",
            )
          : t("留空保留原密码。", "Leave blank to keep the current password."),
      ),
      field(t("再次输入密码", "Confirm password"), repeat),
      element("div", { className: "easy-field wide" }, [
        element("label", { className: "easy-check" }, [
          enabled,
          t("允许此账号登录", "Allow this account to sign in"),
        ]),
        element("label", { className: "easy-check" }, [
          reveal,
          t("显示新密码", "Show new password"),
        ]),
      ]),
    ]);
    var advanced = element("details", {}, [
      element("summary", { text: t("用户组（可选）", "Group (optional)") }),
      field(
        t("用户组", "Group"),
        group,
        t("不需要分组时保持 *。", "Leave * unless you need groups."),
      ),
    ]);
    showModal(
      user ? t("修改账号", "Edit account") : t("添加账号", "Add account"),
      [
        fields,
        advanced,
        element("p", {
          className: "easy-muted",
          text: t(
            "修改密码、账号名称、用户组或停用账号时，会使该账号的旧登录失效。",
            "Changing the password, username, group or disabling an account revokes its previous sessions.",
          ),
        }),
      ],
      async function () {
        if (password.value !== repeat.value)
          throw { code: "password_mismatch" };
        await complete({
          action: "save_user",
          id: user ? user.id : "",
          name: name.value,
          group: group.value || "*",
          password: password.value,
          enabled: enabled.checked,
        });
        password.value = repeat.value = "";
      },
      t("保存并生效", "Save and activate"),
    );
  }
  function createList(view, headers, searchLabel, searchText, renderRow) {
    return window.LansyraTable({
      view: view,
      headers: headers,
      key: function (item) {
        return item.id;
      },
      searchText: searchText,
      renderRow: renderRow,
      labels: {
        search: searchLabel,
        previous: t("上一页", "Previous"),
        next: t("下一页", "Next"),
        empty: t("暂无记录", "No records"),
        noMatches: t(
          "没有匹配的记录，请调整搜索条件。",
          "No matching records. Try another search.",
        ),
        count: function (first, last, count, total) {
          return (
            t(
              first + "–" + last + " / " + count + " 条",
              first + "–" + last + " of " + count,
            ) +
            (count === total
              ? ""
              : t("（全部 " + total + " 条）", " (" + total + " total)"))
          );
        },
        page: function (page, total) {
          return t(
            "第 " + page + " / " + total + " 页",
            "Page " + page + " of " + total,
          );
        },
      },
    });
  }
  function updateOnlinePresence() {
    onlineCounts = Object.create(null);
    if (!state.online_error)
      array(state.online).forEach(function (row) {
        if (typeof row.name !== "string" || !row.name) return;
        onlineCounts[row.name] = (onlineCounts[row.name] || 0) + 1;
      });
    // Update only visible badges; account actions keep their DOM and focus.
    if (accountList)
      accountList.element
        .querySelectorAll(".easy-presence")
        .forEach(renderPresence);
  }
  function renderPresence(badge) {
    var count = onlineCounts[badge.dataset.account] || 0;
    var text = state.online_error
      ? t("未知", "Unknown")
      : count
        ? t("在线", "Online") + (count > 1 ? " (" + count + ")" : "")
        : t("离线", "Offline");
    var className =
      "easy-presence " +
      (!state.online_error && count ? "easy-good" : "easy-muted");
    if (badge.textContent !== text) badge.textContent = text;
    if (badge.className !== className) badge.className = className;
  }
  function renderUsers() {
    updateOnlinePresence();
    accountList = createList(
      listViews.users,
      [
        t("登录账号", "Username"),
        t("用户组", "Group"),
        t("账号状态", "Account status"),
        t("在线状态", "Online"),
        t("操作", "Actions"),
      ],
      t("搜索账号", "Search accounts"),
      function (user) {
        return user.name;
      },
      function (user) {
        var toggle = mutationButton(
          user.enabled ? t("停用", "Disable") : t("恢复", "Enable"),
          function () {
            confirm(
              user.enabled
                ? t("停用账号", "Disable account")
                : t("恢复账号", "Enable account"),
              user.name +
                " — " +
                (user.enabled
                  ? t(
                      "此账号将无法登录，现有登录也会失效。",
                      "This account will be blocked and existing sessions revoked.",
                    )
                  : t(
                      "恢复后可使用原密码登录。",
                      "The existing password will work again.",
                    )),
              {
                action: "save_user",
                id: user.id,
                name: user.name,
                group: user.group,
                password: "",
                enabled: !user.enabled,
              },
            );
          },
        );
        if (user.needs_password) toggle.disabled = true;
        var presence = element("span", { "data-account": user.name });
        renderPresence(presence);
        return element("tr", {}, [
          element("td", { text: user.name }),
          element("td", { text: user.group }),
          element("td", {
            className: user.needs_password
              ? "easy-bad"
              : user.enabled
                ? "easy-good"
                : "easy-muted",
            text: user.needs_password
              ? t("需重设密码", "Reset password")
              : user.enabled
                ? t("已启用", "Enabled")
                : t("已停用", "Disabled"),
          }),
          element("td", {}, [presence]),
          element("td", {}, [
            element("div", { className: "easy-buttons" }, [
              mutationButton(
                user.needs_password
                  ? t("重设密码", "Reset password")
                  : t("修改", "Edit"),
                function () {
                  editUser(user);
                },
              ),
              toggle,
              mutationButton(
                t("删除", "Delete"),
                function () {
                  confirm(
                    t("删除账号", "Delete account"),
                    t("删除账号“", 'Delete account "') +
                      user.name +
                      t(
                        "”？该账号的旧登录将失效。",
                        '"? Previous sessions will be revoked.',
                      ),
                    { action: "delete_user", id: user.id },
                  );
                },
                "cbi-button-remove",
              ),
            ]),
          ]),
        ]);
      },
    );
    accountList.update(array(state.users));
    if (
      array(state.users).some(function (user) {
        return user.needs_password;
      })
    )
      content.appendChild(
        element("div", { className: "easy-notice error" }, [
          element("p", {
            text: t(
              "部分账号的密码记录无效，请点击“重设密码”后重新登录。",
              "Some accounts have invalid password records. Reset their passwords before signing in.",
            ),
          }),
        ]),
      );
    content.appendChild(
      element("section", { className: "easy-card", id: "easy-accounts" }, [
        element("div", { className: "easy-heading" }, [
          element("h3", { text: t("登录账号", "Accounts") }),
          mutationButton(
            t("＋ 添加账号", "＋ Add account"),
            function () {
              editUser(null);
            },
            "cbi-button-add",
          ),
        ]),
        accountList.element,
      ]),
    );
    onlineList = createList(
      listViews.online,
      [
        t("账号", "Username"),
        t("来源 IP", "Remote IP"),
        t("VPN IP", "VPN IP"),
        t("连接时间", "Connected"),
        t("操作", "Actions"),
      ],
      t("搜索在线账号或 IP", "Search connections by account or IP"),
      function (row) {
        return row.name + " " + row.ip + " " + row.vpn_ip;
      },
      function (row) {
        return element("tr", {}, [
          element("td", { text: row.name }),
          element("td", { text: row.ip }),
          element("td", { text: row.vpn_ip }),
          element("td", { text: row.since || row.state }),
          element("td", {}, [
            mutationButton(t("断开", "Disconnect"), function () {
              confirm(
                t("断开连接", "Disconnect"),
                row.name +
                  " — " +
                  t(
                    "仅断开此连接；如需禁止登录，请停用账号。",
                    "Disconnect this connection. Disable the account to prevent sign-in.",
                  ),
                { action: "disconnect", id: row.id },
              );
            }),
          ]),
        ]);
      },
    );
    onlineError = element("p", {
      className: "easy-online-error easy-bad",
      role: "status",
      hidden: true,
      text: t(
        "在线状态暂不可用，账号显示为“未知”。请检查网络连接或 occtl，页面会自动重试。",
        "Online status is unavailable; accounts show Unknown. Check the connection or occtl. The page will retry automatically.",
      ),
    });
    content.appendChild(
      element("section", { className: "easy-card", id: "easy-online" }, [
        element("h3", { text: t("在线连接", "Online connections") }),
        onlineError,
        onlineList.element,
      ]),
    );
    renderOnline();
  }
  function renderOnline() {
    if (!onlineList) return;
    onlineList.update(state.online_error ? [] : array(state.online));
    onlineError.hidden = !state.online_error;
  }
  function renderSettings() {
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
        else if (p.proxy !== "openclash") {
          lines.push(
            t("VPN 入口：", "VPN ingress: ") + status(p.ingress_ready),
          );
          if (!p.ingress_ready)
            lines.push(
              t(
                "VPN 流量尚未接入代理，请查看接入日志。",
                "VPN traffic is not connected to the proxy. Check the integration log.",
              ),
            );
          lines.push("TCP: " + p.tcp_mode + "; UDP: " + p.udp_mode);
          [
            ["lan_dns_hijack", "DNS"],
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
  function renderGuard() {
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
    toggle.disabled = !writable() || switching || (!g.enabled && !g.available);
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
  function download(kind, server) {
    var form = element(
      "form",
      { method: "POST", action: options.base + "/download", target: "_blank" },
      [
        element("input", {
          type: "hidden",
          name: "token",
          value: options.token,
        }),
        element("input", { type: "hidden", name: "kind", value: kind }),
        element("input", { type: "hidden", name: "server", value: server }),
      ],
    );
    document.body.appendChild(form);
    form.submit();
    form.remove();
  }
  function renderExport() {
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
        disabled: !state.certificates_ready,
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
        disabled: !state.ca_available,
      }),
    ]);
    trust.value = (state.client_profile || {}).cert_mode || "pin";
    var profile = element("button", {
      type: "submit",
      className: "cbi-button cbi-button-apply",
      text: t("下载 .vpn 连接配置", "Download .vpn profile"),
    });
    var save = mutationButton(
      t("保存客户端设置", "Save client settings"),
      function () {
        if (!form.reportValidity()) return;
        complete({
          action: "client_profile",
          server: server.value.trim(),
          cert_mode: trust.value,
        }).catch(function (error) {
          notice(errorText(error), true);
        });
      },
    );
    function updateDownload() {
      profile.disabled =
        trust.value === "pin"
          ? !state.certificates_ready
          : trust.value === "profile"
            ? !state.ca_available
            : false;
    }
    trust.addEventListener("change", updateDownload);
    updateDownload();
    var form = element("form", {}, [
      field(
        t("客户端实际连接的服务器地址", "Server address used by clients"),
        server,
        t(
          "例如 vpn.example.com:4443 或 [2001:db8::1]:4443。外网请填写实际公网地址和外部端口。",
          "For example vpn.example.com:4443 or [2001:db8::1]:4443. Remote clients need the actual public address and external port.",
        ),
      ),
      field(t("证书验证方式", "Certificate verification"), trust),
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
      download(trust.value, server.value.trim());
    });
    var ca = button(t("下载 ca.pem", "Download ca.pem"), function () {
      download("ca", "");
    });
    ca.disabled = !state.ca_available;
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
  function renderStatus() {
    var label = state.running
      ? t("停止服务", "Stop service")
      : t("启动服务", "Start service");
    if (serviceButton.textContent !== label) serviceButton.textContent = label;
    if (accountList) updateOnlinePresence();
    renderOnline();
    var key = JSON.stringify([
      state.running,
      array(state.online).length,
      state.online_error,
      array(state.users).length,
    ]);
    if (key === statusKey) return;
    statusKey = key;
    statusBox.replaceChildren();
    [
      [
        t("VPN 服务", "VPN service"),
        state.running ? t("运行中", "Running") : t("已停止", "Stopped"),
        state.running ? "easy-good" : "easy-muted",
      ],
      [
        t("在线连接", "Online connections"),
        state.online_error ? "—" : String(array(state.online).length),
        "",
      ],
      [t("登录账号", "Accounts"), String(array(state.users).length), ""],
    ].forEach(function (item) {
      statusBox.appendChild(
        element("div", { className: "easy-card" }, [
          element("span", { className: "easy-muted", text: item[0] }),
          element("strong", { className: item[2], text: item[1] }),
        ]),
      );
    });
  }
  function renderTab() {
    formEdited = false;
    accountList = null;
    onlineList = null;
    onlineError = null;
    onlineCounts = Object.create(null);
    content.replaceChildren();
    if (activeTab === "users") renderUsers();
    else if (activeTab === "settings") renderSettings();
    else if (activeTab === "guard") renderGuard();
    else if (activeTab === "maintenance")
      content.appendChild(window.LansyraMaintenance(element, button, t));
    else renderExport();
    root.querySelectorAll('[role="tab"]').forEach(function (b) {
      var selected = b.dataset.tab === activeTab;
      b.setAttribute("aria-selected", String(selected));
      b.tabIndex = selected ? 0 : -1;
    });
    content.setAttribute("aria-labelledby", "easy-tab-" + activeTab);
  }
  function selectTab(tab) {
    // Reselecting a tab must not discard its unsaved form or list nodes.
    if (activeTab === tab) return;
    activeTab = tab;
    renderTab();
  }
  function render() {
    removeModal();
    root.replaceChildren();
    accountList = null;
    onlineList = null;
    onlineError = null;
    serviceButton = mutationButton("", function () {
      confirm(
        state.running ? t("停止 VPN", "Stop VPN") : t("启动 VPN", "Start VPN"),
        state.running
          ? t("所有 VPN 连接将断开。", "All VPN connections will close.")
          : t(
              "使用已保存的配置启动服务。",
              "Start the service with saved settings.",
            ),
        {
          action: "service",
          command: state.running ? "stop" : "start",
          allow_restart: true,
        },
      );
    });
    var actions = element("div", { className: "easy-buttons" }, [
      button(t("刷新", "Refresh"), function () {
        load().catch(function (e) {
          notice(errorText(e), true);
        });
      }),
      serviceButton,
      mutationButton(
        state.autostart
          ? t("关闭开机启动", "Disable autostart")
          : t("开机自动启动", "Enable autostart"),
        function () {
          complete({
            action: "service",
            command: state.autostart ? "disable" : "enable",
          }).catch(function (e) {
            notice(errorText(e), true);
          });
        },
      ),
    ]);
    root.appendChild(
      element("div", { className: "easy-heading" }, [
        element("div", {}, [
          element("h2", { text: "岚序 Lansyra" }),
          element("span", {
            className: "easy-muted",
            text:
              t("管理页 ", "UI ") +
              state.ui_version +
              " · ocserv " +
              state.version,
          }),
        ]),
        actions,
      ]),
    );
    messageBox = element("div", {
      className: "easy-notice",
      role: "status",
      hidden: true,
    });
    root.appendChild(messageBox);
    statusBox = element("div", { className: "easy-summary" });
    statusKey = null;
    root.appendChild(statusBox);
    renderStatus();
    if (state.installation_pending)
      root.appendChild(
        element("p", {
          className: "easy-notice error",
          text: t(
            "安装尚未完成。请在完整服务端套装目录重新运行 sh install.sh，完成证书和 VPN 网络初始化。",
            "Installation is incomplete. Run sh install.sh again from the complete server bundle to finish certificate and VPN network initialization.",
          ),
        }),
      );
    if (state.recovery_pending) notice(errors.recovery_pending, true);
    if (!state.supported)
      notice(
        state.version === "unknown"
          ? errors.version_unavailable
          : errors.upgrade_required,
        true,
      );
    else if (!options.writable) notice(errors.forbidden);
    var tabs = element("div", {
      className: "easy-tabs",
      role: "tablist",
      "aria-label": t("管理功能", "Administration"),
    });
    [
      ["users", t("账号与在线用户", "Accounts and connections")],
      ["settings", t("服务设置", "Service settings")],
      ["guard", t("VPN 专用上网", "VPN-only access")],
      ["export", t("客户端配置", "Client profiles")],
      ["maintenance", t("系统维护", "Maintenance")],
    ].forEach(function (item) {
      var b = button(item[1], function () {
        selectTab(item[0]);
      });
      b.dataset.tab = item[0];
      b.id = "easy-tab-" + item[0];
      b.setAttribute("role", "tab");
      b.setAttribute("aria-controls", "easy-panel");
      tabs.appendChild(b);
    });
    tabs.addEventListener("keydown", function (event) {
      if (busy || event.altKey || event.ctrlKey || event.metaKey) return;
      var buttons = Array.from(tabs.children),
        index = buttons.indexOf(event.target);
      if (index < 0) return;
      if (event.key === "ArrowRight") index = (index + 1) % buttons.length;
      else if (event.key === "ArrowLeft")
        index = (index + buttons.length - 1) % buttons.length;
      else if (event.key === "Home") index = 0;
      else if (event.key === "End") index = buttons.length - 1;
      else return;
      event.preventDefault();
      buttons[index].focus();
      selectTab(buttons[index].dataset.tab);
    });
    root.appendChild(tabs);
    content = element("div", {
      id: "easy-panel",
      role: "tabpanel",
      tabindex: "0",
    });
    root.appendChild(content);
    renderTab();
  }
  var loadRead = readSlot("data"),
    statusRead = readSlot("status"),
    loadGeneration = 0,
    formEdited = false,
    backgroundRefresh = false;
  function edited(event) {
    loadRead.cancel();
    if (!modal && event.target.closest("form")) formEdited = true;
  }
  root.addEventListener("input", edited);
  root.addEventListener("change", edited);
  document.addEventListener("visibilitychange", function () {
    if (document.hidden) statusRead.cancel();
  });
  function load(preserveDialog) {
    statusRead.cancel();
    return loadRead.run(function (data) {
      backgroundRefresh = false;
      state = data;
      if (preserveDialog && readerOpen) {
        renderStatus();
        renderTab();
      } else render();
      return true;
    });
  }
  var statusPending = false;
  setInterval(async function () {
    if (
      !state ||
      busy ||
      loadRead.pending ||
      (modal && !readerOpen) ||
      document.hidden ||
      statusPending
    )
      return;
    statusPending = true;
    try {
      var snapshot = state,
        generation = loadGeneration,
        statusFailed = false;
      var current = await statusRead.run().catch(function () {
        statusFailed = true;
      });
      if (
        busy ||
        loadRead.pending ||
        state !== snapshot ||
        generation !== loadGeneration ||
        document.hidden ||
        (modal && !readerOpen)
      )
        return;
      if (statusFailed) {
        state.online_error = true;
        renderStatus();
        return;
      }
      if (!current) return;
      state.running = current.running;
      state.online = current.online;
      state.online_error = current.online_error;
      if (state.recovery_pending !== current.recovery_pending) {
        state.recovery_pending = current.recovery_pending;
        if (state.recovery_pending) notice(errors.recovery_pending, true);
        else if (messageBox.textContent === errors.recovery_pending) notice("");
      }
      var previous = state.guard && state.guard.phase;
      // Lightweight disabled status omits topology. Retain the last full
      // probe until refresh; enabling always validates it again on the router.
      state.guard =
        previous === "disabled" &&
        current.guard &&
        current.guard.phase === "disabled"
          ? Object.assign({}, state.guard, current.guard)
          : current.guard;
      if (previous !== (state.guard && state.guard.phase))
        backgroundRefresh = true;
      renderStatus();
      if (
        !formEdited &&
        state.guard &&
        state.guard.phase === "pending" &&
        state.guard.token &&
        writable()
      ) {
        // A successful authenticated request from the retained management IP
        // confirms connectivity. No confirmation is sent after a failed request.
        if (!(await load(readerOpen))) return;
        if (
          state.guard &&
          state.guard.phase === "pending" &&
          state.guard.token
        ) {
          var confirmation = {
            action: "guard",
            command: "confirm",
            token: state.guard.token,
          };
          await complete(confirmation, readerOpen);
        }
      } else if (previous !== (state.guard && state.guard.phase) && !modal) {
        if (activeTab === "guard") renderTab();
      }
      // Preserve both unsaved fields and their revision. Refresh once the
      // form is left, explicitly refreshed, or successfully submitted.
      if (backgroundRefresh && !formEdited && !modal) await load();
    } catch (_) {
      /* Explicit refresh shows errors; background polling never interrupts typing. */
    } finally {
      statusPending = false;
    }
  }, 3000);
  root.textContent = t("正在读取服务配置…", "Loading service settings…");
  load().catch(function (error) {
    root.replaceChildren(
      element("div", {
        className: "easy-notice error",
        text: errorText(error),
      }),
    );
  });
})();

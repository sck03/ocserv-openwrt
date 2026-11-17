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
  var editUser = window.OcservEasyAccounts({ t, element, field, showModal, complete });
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
  var renderSettings = window.OcservEasySettings({ t, element, field, array, writable, options, errors, confirm });
  var renderGuard = window.OcservEasyGuard({ t, element, array, errors, showReader, button, mutationButton, complete, notice, errorText, confirm, writable });
  var renderExport = window.OcservEasyExport({ t, element, field, options, mutationButton, button, complete, notice, errorText });
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

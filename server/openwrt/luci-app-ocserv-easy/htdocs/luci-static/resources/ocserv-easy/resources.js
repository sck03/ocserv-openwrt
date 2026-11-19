/* SPDX-License-Identifier: GPL-3.0-or-later */
(function () {
  "use strict";
  // Optional read models have their own loading/error state and cancellation owner.
  window.OcservEasyResources = function (api, element, t, errorText) {
    var slots = { export: api.readSlot("client-status"), guard: api.readSlot("guard-status") };
    var values = new WeakMap();
    function cancel() { Object.values(slots).forEach(function (slot) { slot.cancel(); }); }
    function render(name, state, container, draw) {
      var loaded = values.get(state) || {};
      values.set(state, loaded);
      if (loaded[name]) { draw(state, container); return; }
      var guardPhase = state.guard && state.guard.phase;
      container.replaceChildren(element("p", {role: "status", text: t("正在检查功能可用性…", "Checking availability…")}));
      slots[name].run(function (data) {
        if (name === "export") {
          if (data.api_version !== 5 || data.revision !== state.revision || !data.client_trust)
            throw {code: data.revision !== state.revision ? "stale_revision" : "interface_mismatch"};
          state.client_trust = data.client_trust;
          state.certificates_ready = data.certificates_ready;
        } else if (guardPhase === (state.guard && state.guard.phase)) state.guard = data;
        loaded[name] = true;
        container.replaceChildren();
        draw(state, container);
      }).catch(function (error) {
        container.replaceChildren(element("p", {className: "easy-notice error", text: errorText(error)}),
          element("button", {type: "button", className: "cbi-button", text: t("重试", "Retry"),
            onClick: function () { render(name, state, container, draw); }}));
      });
    }
    return {render: render, cancel: cancel};
  };
})();

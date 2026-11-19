/* SPDX-License-Identifier: GPL-3.0-or-later */
(function () {
  "use strict";
  window.OcservEasyAPI = function (options) {
    async function request(path, payload, controller) {
      controller = controller || new AbortController();
      var timer = setTimeout(
        function () {
          controller.abort();
        },
        payload ? 120000 : 15000,
      );
      var settings = {
        credentials: "same-origin",
        cache: "no-store",
        signal: controller.signal,
      };
      try {
        if (payload) {
          settings.method = "POST";
          settings.headers = {
            "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
          };
          settings.body =
            "token=" +
            encodeURIComponent(options.token) +
            "&payload=" +
            encodeURIComponent(JSON.stringify(Object.assign({}, payload, { api_version: 4 })));
        }
        var response = await fetch(options.base + "/" + path, settings);
        var data;
        try {
          data = await response.json();
        } catch (_) {
          throw { code: "session_expired" };
        }
        if (!response.ok || data.ok !== true)
          throw { code: data.error || "network_error" };
        return data.data;
      } catch (error) {
        if (controller.signal.aborted)
          throw { code: payload ? "action_timeout" : "request_timeout" };
        throw error;
      } finally {
        clearTimeout(timer);
      }
    }
    // A read owns its fetch and completion. Cancellation frees the slot at once;
    // an old completion cannot clear a replacement or publish stale data/errors.
    function readSlot(path) {
      var controller = null,
        slot = { pending: null };
      slot.cancel = function () {
        var previous = controller;
        controller = null;
        slot.pending = null;
        if (previous) previous.abort();
      };
      slot.run = function (consume) {
        if (!slot.pending) {
          var active = new AbortController();
          controller = active;
          slot.pending = request(path, null, active)
            .then(function (data) {
              return controller === active
                ? consume
                  ? consume(data)
                  : data
                : false;
            })
            .catch(function (error) {
              if (controller !== active) return false;
              throw error;
            })
            .finally(function () {
              if (controller === active) {
                controller = null;
                slot.pending = null;
              }
            });
        }
        return slot.pending;
      };
      return slot;
    }
    return { request: request, readSlot: readSlot };
  };
})();

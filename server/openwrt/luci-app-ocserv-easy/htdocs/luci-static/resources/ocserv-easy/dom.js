/* SPDX-License-Identifier: GPL-3.0-or-later */
(function () {
  "use strict";
  function element(tag, attrs, children) {
    var node = document.createElement(tag);
    Object.keys(attrs || {}).forEach(function (key) {
      if (key === "className") node.className = attrs[key];
      else if (key === "text") node.textContent = attrs[key];
      else if (key.slice(0, 2) === "on")
        node.addEventListener(key.slice(2).toLowerCase(), attrs[key]);
      else if (
        key === "value" ||
        key === "checked" ||
        key === "disabled" ||
        key === "hidden" ||
        key === "required"
      )
        node[key] = attrs[key];
      else node.setAttribute(key, attrs[key]);
    });
    (children || []).forEach(function (child) {
      if (child == null || child === false) return;
      node.appendChild(
        typeof child === "string" ? document.createTextNode(child) : child,
      );
    });
    return node;
  }
  window.OcservEasyDOM = { element: element };
})();

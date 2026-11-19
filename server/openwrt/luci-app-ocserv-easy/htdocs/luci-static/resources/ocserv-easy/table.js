/* SPDX-License-Identifier: GPL-3.0-or-later */
(function () {
    'use strict';

    // Own only filtering, pagination and visible rows. The page owns data,
    // permissions and actions; view retains the user's position across refreshes.
    window.LansyraTable = function (options) {
        var size = 50, items = [], matches = [], rows = new Map(), emptyText = null;
        var view = options.view;
        function node(tag, className, text) {
            var result = document.createElement(tag);
            if (className) result.className = className;
            if (text !== undefined) result.textContent = text;
            return result;
        }
        var root = node('div', 'easy-list');
        var search = node('input');
        search.type = 'search';
        search.value = view.query;
        search.placeholder = options.labels.search;
        search.setAttribute('aria-label', options.labels.search);
        var summary = node('span', 'easy-muted');
        summary.setAttribute('role', 'status');
        var toolbar = node('div', 'easy-list-toolbar');
        toolbar.append(search, summary);
        var scroll = node('div', 'easy-scroll');
        scroll.tabIndex = 0;
        var table = node('table'), head = node('thead'), header = node('tr'), body = node('tbody');
        options.headers.forEach(function (label) {
            var cell = node('th', '', label);
            cell.scope = 'col';
            header.appendChild(cell);
        });
        head.appendChild(header);
        table.append(head, body);
        scroll.appendChild(table);
        var pager = node('div', 'easy-pager');
        function pageButton(label, delta) {
            var control = node('button', 'cbi-button cbi-button-neutral', label);
            control.type = 'button';
            control.addEventListener('click', function () { view.page += delta; render(); scroll.scrollTop = 0; });
            return control;
        }
        var previous = pageButton(options.labels.previous, -1);
        var next = pageButton(options.labels.next, 1);
        var position = node('span', 'easy-muted');
        pager.append(previous, position, next);
        root.append(toolbar, scroll, pager);

        function renderRows(visible) {
            var active = document.activeElement, focus = null, nextRows = new Map();
            var controls = 'button,input,select,textarea,a[href],[tabindex]';
            if (body.contains(active)) rows.forEach(function (entry, id) {
                if (entry.node.contains(active)) focus = {id:id, index:Array.from(entry.node.querySelectorAll(controls)).indexOf(active)};
            });
            visible.forEach(function (item) {
                var id = options.key(item), signature = JSON.stringify(item), entry = rows.get(id);
                if (!entry || entry.signature !== signature) entry = {node:options.renderRow(item), signature:signature};
                nextRows.set(id, entry);
            });
            // Keep only this page's nodes and signatures. Unchanged rows retain
            // their action closures; changed rows receive the latest record.
            rows.forEach(function (entry, id) {
                if (nextRows.get(id) !== entry) entry.node.remove();
            });
            if (emptyText !== null) { body.replaceChildren(); emptyText = null; }
            var cursor = body.firstChild;
            nextRows.forEach(function (entry) {
                if (entry.node === cursor) cursor = cursor.nextSibling;
                else body.insertBefore(entry.node, cursor);
            });
            rows = nextRows;
            if (!visible.length) {
                var text = view.query.trim() ? options.labels.noMatches : options.labels.empty;
                var row = node('tr'), cell = node('td', 'easy-empty', text);
                cell.colSpan = options.headers.length;
                row.appendChild(cell);
                body.replaceChildren(row);
                emptyText = text;
            }
            // Replacing or moving a focused row can blur its control. Restore
            // only that record's action, never an action on a different record.
            if (focus && document.activeElement !== active) {
                var retained = rows.get(focus.id);
                var target = retained && retained.node.querySelectorAll(controls)[focus.index];
                (target && !target.disabled ? target : scroll).focus({preventScroll:true});
            }
        }
        function render() {
            var pages = Math.max(1, Math.ceil(matches.length / size));
            view.page = Math.max(0, Math.min(view.page, pages - 1));
            var start = view.page * size, visible = matches.slice(start, start + size);
            if (visible.length || emptyText !== (view.query.trim() ? options.labels.noMatches : options.labels.empty)) renderRows(visible);
            function assign(target, property, value) {
                if (target[property] !== value) target[property] = value;
            }
            assign(summary, 'textContent', options.labels.count(visible.length ? start + 1 : 0,
                start + visible.length, matches.length, items.length));
            assign(position, 'textContent', options.labels.page(view.page + 1, pages));
            assign(previous, 'disabled', view.page === 0);
            assign(next, 'disabled', view.page === pages - 1);
            assign(pager, 'hidden', pages === 1);
        }
        function filter() {
            var query = view.query.trim().toLowerCase();
            matches = query ? items.filter(function (item) {
                return options.searchText(item).toLowerCase().includes(query);
            }) : items;
            render();
        }
        search.addEventListener('input', function () {
            view.query = search.value;
            view.page = 0;
            filter();
            scroll.scrollTop = 0;
        });
        return {
            element: root,
            update: function (data) { if (items === data) return; items = data; filter(); }
        };
    };
})();

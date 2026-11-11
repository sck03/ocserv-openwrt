/* SPDX-License-Identifier: GPL-3.0-or-later */
(function () {
    'use strict';

    // Own only filtering, pagination and visible rows. The page owns data,
    // permissions and actions; view retains the user's position across refreshes.
    window.LansyraTable = function (options) {
        var size = 50, items = [], matches = [], visibleKey = null;
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

        function render() {
            var pages = Math.max(1, Math.ceil(matches.length / size));
            view.page = Math.max(0, Math.min(view.page, pages - 1));
            var start = view.page * size, visible = matches.slice(start, start + size);
            var key = visible.length ? JSON.stringify(visible) :
                (view.query.trim() ? options.labels.noMatches : options.labels.empty);
            // Changes outside this page must not rebuild focused action buttons.
            if (key !== visibleKey) {
                visibleKey = key;
                if (visible.length) body.replaceChildren(...visible.map(options.renderRow));
                else {
                    var row = node('tr'), cell = node('td', 'easy-empty',
                        view.query.trim() ? options.labels.noMatches : options.labels.empty);
                    cell.colSpan = options.headers.length;
                    row.appendChild(cell);
                    body.replaceChildren(row);
                }
            }
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
        });
        return {
            element: root,
            update: function (data) { items = data; filter(); }
        };
    };
})();

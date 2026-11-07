"""Exercise browser request lifetimes with a real page and an isolated preview."""
import argparse
import json
from pathlib import Path
from urllib.parse import parse_qs

ROOT = Path(__file__).resolve().parents[1]
from playwright.sync_api import sync_playwright, expect

parser = argparse.ArgumentParser()
parser.add_argument('--url', default='http://127.0.0.1:22981/')
parser.add_argument('--browser-channel', default='chrome')
args = parser.parse_args()
checks = []
with sync_playwright() as pw:
    browser = pw.chromium.launch(channel=args.browser_channel, headless=True)
    page = browser.new_page()
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.clock.install()
    page.goto(args.url, wait_until='networkidle')
    expect(page.get_by_role('tab')).to_have_count(4)
    page.get_by_role('tab', name='VPN 专用上网', exact=True).click()
    expect(page.get_by_role('button', name='开启', exact=True)).to_be_enabled()

    # A save owns the controls until its follow-up snapshot has finished.
    page.get_by_role('tab', name='账号与在线用户', exact=True).click()
    page.get_by_role('button', name='＋ 添加账号', exact=True).click()
    dialog = page.get_by_role('dialog')
    dialog.get_by_label('登录账号', exact=True).fill('operation-fixture')
    dialog.get_by_label('密码', exact=True).fill('FixturePassword123')
    dialog.get_by_label('再次输入密码', exact=True).fill('FixturePassword123')
    pending_actions, pending_data = [], []
    page.route('**/easy/action', lambda route: pending_actions.append(route))
    page.route('**/easy/data', lambda route: pending_data.append(route))
    dialog.get_by_role('button', name='保存并生效', exact=True).click()
    expect(dialog.get_by_label('登录账号', exact=True)).to_be_disabled()
    expect(dialog.get_by_label('密码', exact=True)).to_be_disabled()
    expect(page.get_by_role('button', name='刷新', exact=True)).to_be_disabled()
    assert len(pending_actions) == 1
    checks.append('a pending save locks editable fields as well as buttons')
    pending_actions[0].fulfill(response=pending_actions[0].fetch())
    page.wait_for_function('true')
    assert len(pending_data) == 1
    expect(dialog.get_by_role('button', name='保存并生效', exact=True)).to_be_disabled()
    expect(dialog.get_by_role('button', name='取消', exact=True)).to_be_disabled()
    page.clock.run_for(12000)
    assert len(pending_actions) == len(pending_data) == 1
    checks.append('a successful save keeps controls locked while refreshing its result')
    pending_data[0].abort()
    expect(page.get_by_role('dialog')).to_have_count(0)
    expect(page.locator('#ocserv-easy .easy-notice.error')).to_contain_text('操作已成功，但状态刷新失败')
    expect(page.get_by_role('button', name='刷新', exact=True)).to_be_enabled()
    assert len(pending_actions) == 1
    saved = page.request.get(args.url + 'easy/data').json()['data']['users']
    assert sum(user['name'] == 'operation-fixture' for user in saved) == 1
    checks.append('a failed post-save refresh closes the saved form and reports success without retrying')
    page.unroute('**/easy/action')
    page.unroute('**/easy/data')

    # Rejected saves retain the form, values and original disabled constraints.
    page.get_by_role('tab', name='客户端配置', exact=True).click()
    page.get_by_label('客户端实际连接的服务器地址', exact=True).fill('retry.example.com:4443')
    page.route('**/easy/action', lambda route: route.fulfill(status=409, json={'ok': False, 'error': 'stale_revision'}))
    page.get_by_role('button', name='保存客户端设置', exact=True).click()
    expect(page.locator('#ocserv-easy .easy-notice.error')).to_contain_text('配置已被其他页面修改')
    expect(page.get_by_label('客户端实际连接的服务器地址', exact=True)).to_have_value('retry.example.com:4443')
    expect(page.get_by_label('客户端实际连接的服务器地址', exact=True)).to_be_enabled()
    page.unroute('**/easy/action')
    checks.append('a rejected save restores editable controls and preserves entered values')

    # Each read-only dialog owns and cancels its request, including on replacement.
    page.evaluate("""() => {
        const original = window.fetch;
        window.readerSignals = [];
        window.fetch = (url, options) => {
            if (url.endsWith('/guard-log') || url.endsWith('/proxy-status')) {
                window.readerSignals.push(options.signal);
                return new Promise((resolve, reject) => {
                    options.signal.addEventListener('abort', () => reject(new Error('aborted')), {once: true});
                });
            }
            return original(url, options);
        };
    }""")
    page.get_by_role('tab', name='VPN 专用上网', exact=True).click()
    page.get_by_role('button', name='查看日志', exact=True).click()
    page.get_by_role('dialog').get_by_role('button', name='刷新日志', exact=True).click()
    assert page.evaluate('window.readerSignals.length') == 1
    page.get_by_role('dialog').get_by_role('button', name='关闭', exact=True).click()
    assert page.evaluate('window.readerSignals[0].aborted'), 'closed log dialog retained its fetch'
    checks.append('closing a log dialog aborts its single pending read')
    page.get_by_role('button', name='检查代理', exact=True).click()
    page.get_by_role('dialog').get_by_role('button', name='重新检查', exact=True).click()
    assert page.evaluate('window.readerSignals.length') == 2
    page.get_by_role('dialog').get_by_role('button', name='取消', exact=True).click()
    assert page.evaluate('window.readerSignals[1].aborted'), 'closed diagnostics retained its fetch'
    checks.append('closing diagnostics aborts its single pending read')
    for index in range(50):
        label = '查看日志' if index % 2 == 0 else '检查代理'
        page.get_by_role('button', name=label, exact=True).click()
        assert page.evaluate('window.readerSignals.filter(signal => !signal.aborted).length') == 1
        page.get_by_role('dialog').press('Escape')
    assert page.evaluate('window.readerSignals.every(signal => signal.aborted)')
    checks.append('50 alternating log and diagnostic dialogs leave no live read requests')
    page.get_by_role('button', name='查看日志', exact=True).click()
    page.evaluate("() => [...document.querySelectorAll('button')].find(b => b.textContent === '刷新').click()")
    expect(page.get_by_role('dialog')).to_have_count(0)
    assert page.evaluate('window.readerSignals.every(signal => signal.aborted)')
    checks.append('replacing the whole page also cancels the removed dialog read')

    page.evaluate("window.summaryNode = document.querySelector('.easy-summary').firstChild")
    for _ in range(3):
        with page.expect_response('**/easy/status'):
            page.clock.run_for(3000)
    assert page.evaluate("window.summaryNode === document.querySelector('.easy-summary').firstChild"), 'unchanged status rebuilt the summary'
    checks.append('unchanged router status preserves existing summary controls')

    # Only changed regions should be redrawn; polling must also update actions.
    page.get_by_role('tab', name='账号与在线用户', exact=True).click()
    status = page.request.get(args.url + 'easy/status').json()
    status['data']['online'] = [{'id': '7', 'name': 'poll-fixture', 'ip': '192.0.2.1',
                                'vpn_ip': '10.77.0.7', 'since': 'now', 'state': 'connected'}]
    page.route('**/easy/status', lambda route: route.fulfill(json=status))
    page.clock.run_for(3000)
    expect(page.get_by_role('cell', name='192.0.2.1', exact=True)).to_be_visible()
    page.evaluate("window.summaryNode = document.querySelector('.easy-summary').firstChild")
    status['data']['online'][0]['ip'] = '192.0.2.2'
    page.clock.run_for(3000)
    expect(page.get_by_role('cell', name='192.0.2.2', exact=True)).to_be_visible()
    assert page.evaluate("window.summaryNode === document.querySelector('.easy-summary').firstChild"), 'online details rebuilt unchanged summary'
    checks.append('online detail changes update the list without rebuilding summary cards')

    page.evaluate("window.onlineNode = document.querySelector('#easy-online table')")
    status['data']['running'] = False
    page.clock.run_for(3000)
    expect(page.get_by_role('button', name='启动服务', exact=True)).to_be_visible()
    assert page.evaluate("window.onlineNode === document.querySelector('#easy-online table')"), 'service state rebuilt unchanged online list'
    page.get_by_role('button', name='启动服务', exact=True).click()
    expect(page.get_by_role('dialog')).to_contain_text('使用已保存的配置启动服务')
    page.get_by_role('button', name='取消', exact=True).click()
    checks.append('service state changes update the action and preserve unchanged online controls')
    page.unroute('**/easy/status')
    page.clock.run_for(3000)
    expect(page.get_by_role('button', name='停止服务', exact=True)).to_be_visible()

    # Even a failed mutation invalidates a poll started before it.
    obsolete = []
    page.route('**/easy/status', lambda route: obsolete.append(route))
    page.clock.run_for(3000)
    page.wait_for_function('true')
    assert len(obsolete) == 1
    page.route('**/easy/action', lambda route: route.fulfill(status=409, json={'ok': False, 'error': 'stale_revision'}))
    page.get_by_role('button', name='关闭开机启动', exact=True).click()
    expect(page.locator('#ocserv-easy .easy-notice.error')).to_contain_text('配置已被其他页面修改')
    obsolete[0].fulfill(json=status)
    page.wait_for_timeout(100)
    expect(page.get_by_role('button', name='停止服务', exact=True)).to_be_visible()
    expect(page.locator('.easy-summary').first).to_contain_text('运行中')
    checks.append('a delayed pre-mutation poll is discarded even when the mutation fails')
    page.unroute('**/easy/status')
    page.unroute('**/easy/action')

    # Hiding a tab while its poll is in flight must not redraw or confirm state.
    obsolete.clear()
    page.route('**/easy/status', lambda route: obsolete.append(route))
    page.clock.run_for(3000)
    page.wait_for_function('true')
    assert len(obsolete) == 1
    page.evaluate("Object.defineProperty(document, 'hidden', {configurable:true, value:true})")
    obsolete[0].fulfill(json=status)
    page.wait_for_timeout(100)
    expect(page.locator('.easy-summary').first).to_contain_text('运行中')
    checks.append('an in-flight poll does not update a tab hidden before its response')
    page.evaluate("Object.defineProperty(document, 'hidden', {configurable:true, value:false})")
    page.unroute('**/easy/status')
    page.get_by_role('tab', name='VPN 专用上网', exact=True).click()

    # A full refresh started before editing must not discard the new input.
    refreshes = []
    def delayed_refresh(route):
        refreshes.append((route, route.fetch()))
    page.route('**/easy/data', delayed_refresh)
    page.get_by_role('button', name='刷新', exact=True).click()
    page.wait_for_function('true')
    page.get_by_role('tab', name='服务设置', exact=True).click()
    page.get_by_label('每个账号最多在线设备数', exact=True).fill('3')
    refreshes[0][0].fulfill(response=refreshes[0][1])
    page.wait_for_timeout(100)
    expect(page.get_by_label('每个账号最多在线设备数', exact=True)).to_have_value('3')
    page.unroute('**/easy/data', delayed_refresh)
    checks.append('a delayed full refresh preserves input entered after the request started')

    page.route('**/easy/data', delayed_refresh)
    page.get_by_role('button', name='刷新', exact=True).click()
    page.wait_for_function('true')
    page.get_by_role('tab', name='账号与在线用户', exact=True).click()
    page.get_by_role('button', name='＋ 添加账号', exact=True).click()
    refreshes[1][0].fulfill(response=refreshes[1][1])
    page.wait_for_timeout(100)
    expect(page.get_by_role('dialog')).to_be_visible()
    page.get_by_role('button', name='取消', exact=True).click()
    page.unroute('**/easy/data', delayed_refresh)
    checks.append('a delayed full refresh preserves a dialog opened after the request started')
    page.get_by_role('tab', name='VPN 专用上网', exact=True).click()

    # A network request that never returns must be aborted, then polled again.
    requests = []
    def stalled(route):
        requests.append(route)
        if len(requests) > 1:
            route.continue_()
    page.route('**/easy/status', stalled)
    page.clock.run_for(3000)
    page.wait_for_function('true')
    assert len(requests) == 1
    page.clock.run_for(12000)
    assert len(requests) == 1, 'overlapping status requests'
    page.clock.run_for(6000)
    page.wait_for_function('true')
    assert len(requests) >= 2, 'timed-out request blocked future polling'
    expect(page.get_by_role('button', name='开启', exact=True)).to_be_enabled()
    checks.append('stalled status aborts within 15 seconds, resumes, and retains disabled-mode eligibility')
    requests[0].abort()
    page.unroute('**/easy/status', stalled)

    # Repeated refresh clicks share the same pending full-data request.
    loads = []
    page.route('**/easy/data', lambda route: loads.append(route))
    page.evaluate("""() => {
        const button = [...document.querySelectorAll('button')].find(b => b.textContent === '刷新');
        for (let i = 0; i < 50; ++i) button.click();
    }""")
    page.wait_for_function('true')
    assert len(loads) == 1, 'refresh spawned duplicate requests'
    loads[0].continue_()
    expect(page.get_by_role('button', name='开启', exact=True)).to_be_enabled()
    page.unroute('**/easy/data')
    checks.append('50 refresh clicks issue one full-data request')

    # A pre-mutation refresh must never replace the snapshot fetched after save.
    older = []
    def old_refresh(route):
        if not older:
            older.append((route, route.fetch()))
        else:
            route.continue_()
    page.route('**/easy/data', old_refresh)
    page.get_by_role('button', name='刷新', exact=True).click()
    page.get_by_role('tab', name='账号与在线用户', exact=True).click()
    page.get_by_role('button', name='＋ 添加账号', exact=True).click()
    dialog = page.get_by_role('dialog')
    dialog.get_by_label('登录账号', exact=True).fill('refresh-race')
    dialog.get_by_label('密码', exact=True).fill('FixturePassword123')
    dialog.get_by_label('再次输入密码', exact=True).fill('FixturePassword123')
    with page.expect_response('**/easy/action'):
        dialog.get_by_role('button', name='保存并生效', exact=True).click()
    older[0][0].fulfill(response=older[0][1])
    expect(page.get_by_role('dialog')).to_have_count(0)
    expect(page.get_by_role('cell', name='refresh-race', exact=True)).to_have_count(1)
    page.unroute('**/easy/data', old_refresh)
    checks.append('a delayed refresh cannot overwrite a successful mutation with old data')
    older.clear()
    page.route('**/easy/data', old_refresh)
    page.get_by_role('button', name='刷新', exact=True).click()
    page.get_by_role('button', name='＋ 添加账号', exact=True).click()
    dialog = page.get_by_role('dialog')
    dialog.get_by_label('登录账号', exact=True).fill('refresh-error-race')
    dialog.get_by_label('密码', exact=True).fill('FixturePassword123')
    dialog.get_by_label('再次输入密码', exact=True).fill('FixturePassword123')
    dialog.get_by_role('button', name='保存并生效', exact=True).click()
    expect(page.get_by_role('dialog')).to_have_count(0)
    expect(page.get_by_role('cell', name='refresh-error-race', exact=True)).to_have_count(1)
    older[0][0].abort()
    page.wait_for_timeout(100)
    expect(page.get_by_text('账号已生效，其他在线用户保持连接。', exact=True)).to_be_visible()
    page.unroute('**/easy/data', old_refresh)
    checks.append('a failed obsolete refresh cannot replace a successful mutation notice')
    page.get_by_role('tab', name='VPN 专用上网', exact=True).click()

    # A timeout while reading the response body must also clear the request.
    page.evaluate("""() => {
        const original = window.fetch;
        window.bodyAttempts = 0;
        window.fetch = (url, options) => {
            if (url.endsWith('/status') && ++window.bodyAttempts === 1)
                return Promise.resolve({ok: true, json: () => new Promise((resolve, reject) => {
                    options.signal.addEventListener('abort', () => reject(new Error('aborted')), {once: true});
                })});
            return original(url, options);
        };
    }""")
    page.clock.run_for(3000)
    page.wait_for_function('window.bodyAttempts === 1')
    page.clock.run_for(21000)
    page.wait_for_function('window.bodyAttempts >= 2')
    checks.append('stalled JSON response body also times out and releases polling')

    # Hidden tabs must not create new router queries.
    page.evaluate("Object.defineProperty(document, 'hidden', {configurable:true, value:true})")
    before = page.evaluate('window.bodyAttempts')
    page.clock.run_for(60000)
    assert page.evaluate('window.bodyAttempts') == before
    checks.append('hidden tab stops periodic router queries')

    # An uncertain mutation must release controls without silently repeating it.
    page.evaluate("Object.defineProperty(document, 'hidden', {configurable:true, value:false})")
    actions = []
    page.route('**/easy/action', lambda route: actions.append(route))
    page.evaluate("""() => [...document.querySelectorAll('button')].find(b => b.textContent.includes('开机')).click()""")
    page.wait_for_function('true')
    assert len(actions) == 1
    page.clock.run_for(123000)
    expect(page.locator('#ocserv-easy .easy-notice.error')).to_contain_text('服务端可能仍在执行')
    page.clock.run_for(60000)
    assert len(actions) == 1, 'timed-out mutation was retried'
    actions[0].abort()
    page.unroute('**/easy/action')
    checks.append('mutation timeout reports uncertain outcome and never automatically retries')

    # Background guard transitions must not erase a form edited before the poll.
    page.reload(wait_until='networkidle')
    full = page.request.get(args.url + 'easy/data').json()
    original_revision = full['data']['revision']
    status = page.request.get(args.url + 'easy/status').json()
    page.route('**/easy/status', lambda route: route.fulfill(json=status))
    background_loads, confirmations = [], []
    def background_data(route):
        background_loads.append(route)
        route.fulfill(json=full)
    page.route('**/easy/data', background_data)
    page.route('**/easy/action', lambda route: confirmations.append(route))
    page.get_by_role('tab', name='服务设置', exact=True).click()
    page.get_by_label('每个账号最多在线设备数', exact=True).fill('3')
    for phase in ('restoring', 'disabled'):
        status['data']['guard'] = {'phase': phase, 'enabled': phase != 'disabled'}
        with page.expect_response('**/easy/status'):
            page.clock.run_for(3000)
        page.wait_for_timeout(100)
    expect(page.get_by_label('每个账号最多在线设备数', exact=True)).to_have_value('3')
    assert not background_loads, 'guard recovery refreshed an unsaved form'
    checks.append('guard recovery preserves existing unsaved settings without extra full-data requests')

    status['data']['guard'] = {'phase': 'pending', 'enabled': True, 'token': 'pending-fixture'}
    full['data']['guard'] = status['data']['guard'].copy()
    full['data']['revision'] = 'new-guard-revision'
    with page.expect_response('**/easy/status'):
        page.clock.run_for(3000)
    page.wait_for_timeout(100)
    expect(page.get_by_label('每个账号最多在线设备数', exact=True)).to_have_value('3')
    assert not background_loads and not confirmations, 'automatic confirmation replaced the form revision'
    checks.append('automatic guard confirmation waits while an unsaved form owns its original revision')

    page.get_by_role('button', name='保存并应用', exact=True).click()
    with page.expect_request('**/easy/action') as submitted:
        page.get_by_role('dialog').get_by_role('button', name='确认', exact=True).click()
    payload = json.loads(parse_qs(submitted.value.post_data)['payload'][0])
    assert payload['revision'] == original_revision and payload['settings']['max_same'] == '3'
    confirmations[0].fulfill(status=409, json={'ok': False, 'error': 'stale_revision'})
    expect(page.get_by_role('dialog')).to_contain_text('配置已被其他页面修改')
    page.get_by_role('dialog').get_by_role('button', name='取消', exact=True).click()
    expect(page.get_by_label('每个账号最多在线设备数', exact=True)).to_have_value('3')
    checks.append('saving the retained form uses its original revision and preserves input on a conflict')

    page.get_by_role('tab', name='VPN 专用上网', exact=True).click()
    full = page.request.get(args.url + 'easy/data').json()
    status = page.request.get(args.url + 'easy/status').json()
    with page.expect_response('**/easy/data'):
        page.clock.run_for(3000)
    expect(page.get_by_role('button', name='开启', exact=True)).to_be_enabled()
    assert len(background_loads) == 1
    page.clock.run_for(9000)
    assert len(background_loads) == 1, 'completed deferred refresh was repeated'
    checks.append('leaving the edited form performs one deferred refresh and restores guard availability')
    page.unroute('**/easy/status')
    page.unroute('**/easy/data')
    page.unroute('**/easy/action')
    # Superseded reads release their fetch immediately, without waiting for the
    # timeout. Use abort-aware promises so no router or real clock is involved.
    def stall_reads(paths):
        page.evaluate("""paths => {
            const original = window.fetch;
            window.stalledReads = [];
            window.fetch = (url, options) => {
                if (!paths.some(path => url.endsWith('/' + path))) return original(url, options);
                window.stalledReads.push({url, signal: options.signal});
                return new Promise((resolve, reject) => {
                    options.signal.addEventListener('abort', () => reject(new Error('aborted')), {once: true});
                });
            };
        }""", paths)

    page.reload(wait_until='networkidle')
    stall_reads(['data', 'status'])
    page.clock.run_for(3000)
    page.wait_for_function('window.stalledReads.length === 1')
    page.get_by_role('button', name='刷新', exact=True).click()
    assert page.evaluate('window.stalledReads[0].signal.aborted'), 'full refresh retained the obsolete status query'
    page.route('**/easy/action', lambda route: route.fulfill(status=409, json={'ok': False, 'error': 'stale_revision'}))
    page.get_by_role('button', name='关闭开机启动', exact=True).click()
    expect(page.locator('#ocserv-easy .easy-notice.error')).to_contain_text('配置已被其他页面修改')
    assert page.evaluate('window.stalledReads.every(read => read.signal.aborted)'), 'mutation retained an obsolete read'
    checks.append('full refresh cancels polling and a mutation cancels the superseded full refresh')
    page.unroute('**/easy/action')

    page.reload(wait_until='networkidle')
    stall_reads(['data'])
    page.get_by_role('tab', name='服务设置', exact=True).click()
    for index in range(50):
        page.get_by_role('button', name='刷新', exact=True).click()
        page.get_by_label('每个账号最多在线设备数', exact=True).fill(str(index % 3 + 1))
        assert page.evaluate('window.stalledReads.every(read => read.signal.aborted)'), 'editing retained an obsolete refresh'
    assert page.evaluate('window.stalledReads.length') == 50, 'canceled refresh blocked its replacement'
    expect(page.get_by_label('每个账号最多在线设备数', exact=True)).to_have_value('2')
    checks.append('50 refresh/edit cycles retain the input and leave no obsolete fetches')
    page.get_by_role('button', name='刷新', exact=True).click()
    page.get_by_role('tab', name='账号与在线用户', exact=True).click()
    page.get_by_role('button', name='＋ 添加账号', exact=True).click()
    assert page.evaluate('window.stalledReads.every(read => read.signal.aborted)'), 'opening a form retained the old refresh'
    checks.append('opening an editing dialog cancels the obsolete full refresh')

    # Cancellation can race a response already buffered by fetch. The old
    # completion must neither publish nor release the newer request's slot.
    page.reload(wait_until='networkidle')
    full = page.request.get(args.url + 'easy/data').json()
    page.evaluate("""() => {
        const original = window.fetch;
        window.delayedReads = [];
        window.fetch = (url, options) => url.endsWith('/data')
            ? new Promise((resolve, reject) => window.delayedReads.push({resolve, reject}))
            : original(url, options);
    }""")
    page.get_by_role('tab', name='服务设置', exact=True).click()
    page.get_by_role('button', name='刷新', exact=True).click()
    page.get_by_label('每个账号最多在线设备数', exact=True).fill('3')
    page.get_by_role('button', name='刷新', exact=True).click()
    page.evaluate("window.delayedReads[0].reject(new Error('obsolete failure'))")
    page.get_by_role('button', name='刷新', exact=True).click()
    assert page.evaluate('window.delayedReads.length') == 2, 'old completion released its replacement'
    expect(page.locator('#ocserv-easy > .easy-notice.error')).to_have_count(0)
    page.evaluate("data => window.delayedReads[1].resolve({ok:true,json:async () => data})", full)
    expect(page.get_by_label('每个账号最多在线设备数', exact=True)).to_have_value(full['data']['settings']['max_same'])
    checks.append('a late canceled failure cannot publish an error or release the newer read slot')

    page.reload(wait_until='networkidle')
    stall_reads(['status'])
    page.clock.run_for(3000)
    page.wait_for_function('window.stalledReads.length === 1')
    page.evaluate("""() => {
        Object.defineProperty(document, 'hidden', {configurable:true, value:true});
        document.dispatchEvent(new Event('visibilitychange'));
    }""")
    assert page.evaluate('window.stalledReads[0].signal.aborted'), 'hidden page retained a status fetch'
    page.clock.run_for(60000)
    assert page.evaluate('window.stalledReads.length') == 1
    page.evaluate("""() => {
        Object.defineProperty(document, 'hidden', {configurable:true, value:false});
        document.dispatchEvent(new Event('visibilitychange'));
    }""")
    page.clock.run_for(3000)
    page.wait_for_function('window.stalledReads.length === 2')
    checks.append('hiding a page aborts polling immediately and showing it resumes one query')
    assert not errors, errors
    browser.close()
report = {'passed': len(checks), 'checks': checks, 'browser_errors': errors}
output = ROOT / 'test-results/server-polling.json'
output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))

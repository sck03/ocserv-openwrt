"""Exercise bounded DOM creation and list state with the real management page."""
import argparse
import copy
import json
from pathlib import Path
from urllib.parse import parse_qs

from playwright.sync_api import expect, sync_playwright

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--url', default='http://127.0.0.1:22981/')
parser.add_argument('--browser-channel', default='chrome')
parser.add_argument('--output', type=Path, default=ROOT / 'test-results/server-tables')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
checks, errors = [], []
with sync_playwright() as pw:
    browser = pw.chromium.launch(channel=args.browser_channel, headless=True)
    page = browser.new_page(viewport={'width': 1280, 'height': 1080})
    page.set_default_timeout(10000)
    page.on('pageerror', lambda error: errors.append(str(error)))
    snapshot = page.request.get(args.url + 'easy/data').json()
    snapshot['data']['users'] = [
        {'id': 'user' + str(i), 'name': f'account-{i:04}', 'group': '*',
         'enabled': i != 54, 'needs_password': i == 55} for i in range(128)]
    snapshot['data']['online'] = [
        {'id': str(i + 1), 'name': f'account-{i:04}', 'ip': f'2001:db8::{i + 1:x}',
         'vpn_ip': f'fd77::{i + 1:x}', 'since': '2026-11-11 10:00', 'state': 'connected'}
        for i in range(4096)]
    status = {'ok': True, 'data': {key: copy.deepcopy(snapshot['data'].get(key, False))
                                 for key in ('running', 'online', 'online_error', 'guard')}}
    page.route('**/easy/data', lambda route: route.fulfill(json=snapshot))
    page.route('**/easy/status', lambda route: route.fulfill(json=status))
    page.add_init_script("""(() => {
        window.createdRows = 0;
        const create = document.createElement.bind(document);
        document.createElement = function(tag, ...args) {
            if (tag.toLowerCase() === 'tr') window.createdRows++;
            return create(tag, ...args);
        };
    })();""")
    page.clock.install()
    page.goto(args.url, wait_until='networkidle')
    accounts, online = page.locator('#easy-accounts'), page.locator('#easy-online')
    expect(online.locator('tbody tr')).to_have_count(50)
    expect(accounts.locator('tbody tr')).to_have_count(50)
    # Count allocations as well as attached nodes: slicing already-created rows
    # would hide the resource regression while still passing the DOM count.
    assert page.evaluate('window.createdRows') == 102
    checks.append('4096 sessions and 128 accounts create only 100 data rows and two headers')
    expect(accounts.get_by_role('columnheader', name='账号状态', exact=True)).to_be_visible()
    expect(accounts.get_by_role('columnheader', name='在线状态', exact=True)).to_be_visible()
    expect(accounts.locator('.easy-presence').first).to_have_text('在线')
    accounts.get_by_role('button', name='修改', exact=True).first.focus()
    page.evaluate('window.accountButton = document.querySelector("#easy-accounts tbody button"); window.rowsBefore = createdRows')
    status['data']['online'][1000]['name'] = 'account-0000'
    page.clock.run_for(3000)
    expect(accounts.locator('.easy-presence').first).to_have_text('在线 (2)')
    assert page.evaluate('accountButton === document.activeElement && rowsBefore === createdRows')
    status['data']['online'][1000]['name'] = 'account-1000'
    page.clock.run_for(3000)
    expect(accounts.locator('.easy-presence').first).to_have_text('在线')
    checks.append('account presence counts multiple sessions without rebuilding rows or losing action focus')
    online.get_by_role('button', name='下一页', exact=True).click()
    expect(online.get_by_role('cell', name='account-0050', exact=True)).to_be_visible()
    online.get_by_role('button', name='断开', exact=True).first.focus()
    page.evaluate('window.keptRow = document.querySelector("#easy-online tbody tr"); window.rowsBefore = createdRows')
    status['data']['online'][1000]['ip'] = '2001:db8::ffff'
    page.clock.run_for(3000)
    page.wait_for_function('true')
    expect(online.get_by_role('button', name='断开', exact=True).first).to_be_focused()
    assert page.evaluate('keptRow === document.querySelector("#easy-online tbody tr") && rowsBefore === createdRows')
    expect(online.locator('.easy-pager')).to_contain_text('第 2 / 82 页')
    checks.append('off-page polling changes retain the visible DOM, focus and current page')
    status['data']['online'][50]['ip'] = '2001:db8::abcd'
    page.clock.run_for(3000)
    expect(online.get_by_role('cell', name='2001:db8::abcd', exact=True)).to_be_visible()
    checks.append('visible connection changes update the current page')
    online.get_by_role('button', name='断开', exact=True).first.click()
    dialog = page.get_by_role('dialog')
    expect(dialog).to_contain_text('account-0050')
    actions = []
    def reject_action(route):
        actions.append(json.loads(parse_qs(route.request.post_data)['payload'][0]))
        route.fulfill(status=409, json={'ok': False, 'error': 'stale_revision'})
    page.route('**/easy/action', reject_action)
    dialog.get_by_role('button', name='确认', exact=True).click()
    expect(dialog).to_contain_text('配置已被其他页面修改')
    assert actions[0]['id'] == '51'
    dialog.get_by_role('button', name='取消', exact=True).click()
    expect(online.get_by_role('button', name='断开', exact=True).first).to_be_focused()
    checks.append('paged actions target the correct session and closing returns keyboard focus')
    search = online.get_by_role('searchbox')
    search.fill(' FD77::1000 ')
    expect(online.locator('tbody tr')).to_have_count(1)
    expect(online.get_by_role('cell', name='account-4095', exact=True)).to_be_visible()
    expect(online.locator('.easy-pager')).to_be_hidden()
    search.fill('missing-account')
    expect(online).to_contain_text('没有匹配的记录')
    search.fill('account-00')
    online.get_by_role('button', name='下一页', exact=True).click()
    accounts.get_by_role('searchbox').fill('ACCOUNT-0055')
    expect(accounts.get_by_role('button', name='停用', exact=True)).to_be_disabled()
    expect(accounts.get_by_role('cell', name='需重设密码', exact=True)).to_be_visible()
    expect(accounts.locator('.easy-presence')).to_have_text('在线')
    accounts.get_by_role('searchbox').fill('account-0054')
    expect(accounts.get_by_role('cell', name='已停用', exact=True)).to_be_visible()
    expect(accounts.locator('.easy-presence')).to_have_text('在线')
    accounts.get_by_role('searchbox').fill('ACCOUNT-0055')
    checks.append('enabled, disabled and invalid-password account states remain separate from online presence')
    page.get_by_role('tab', name='服务设置', exact=True).click()
    page.get_by_role('tab', name='账号与在线用户', exact=True).click()
    expect(online.get_by_role('searchbox')).to_have_value('account-00')
    expect(online.locator('.easy-pager')).to_contain_text('第 2 / 2 页')
    page.get_by_role('button', name='刷新', exact=True).click()
    expect(accounts.get_by_role('searchbox')).to_have_value('ACCOUNT-0055')
    expect(online.locator('.easy-pager')).to_contain_text('第 2 / 2 页')
    checks.append('case-insensitive account/IP filtering, empty results and view state survive tabs and refresh')
    status['data']['online'] = status['data']['online'][:2]
    page.clock.run_for(3000)
    expect(online.locator('tbody tr')).to_have_count(2)
    expect(online.locator('.easy-pager')).to_be_hidden()
    expect(accounts.locator('.easy-presence')).to_have_text('离线')
    checks.append('departing sessions clamp a now-empty final page to the available range')
    status['data']['online_error'] = True
    page.clock.run_for(3000)
    expect(online.locator('.easy-online-error')).to_be_visible()
    expect(online.get_by_role('button', name='断开', exact=True)).to_have_count(0)
    expect(accounts.locator('.easy-presence')).to_have_text('未知')
    status['data']['online_error'] = False
    page.clock.run_for(3000)
    expect(online.get_by_role('button', name='断开', exact=True)).to_have_count(2)
    expect(accounts.locator('.easy-presence')).to_have_text('离线')
    checks.append('query failures show an error without stale actionable sessions and recover on success')
    page.route('**/easy/status', lambda route: route.abort())
    page.clock.run_for(3000)
    expect(accounts.locator('.easy-presence')).to_have_text('未知')
    expect(online.get_by_role('button', name='断开', exact=True)).to_have_count(0)
    page.unroute('**/easy/status')
    page.route('**/easy/status', lambda route: route.fulfill(json=status))
    page.clock.run_for(3000)
    expect(accounts.locator('.easy-presence')).to_have_text('离线')
    checks.append('failed HTTP polling marks presence unknown and recovers on the next successful snapshot')
    status['data']['online'] = [dict(status['data']['online'][0], name='ACCOUNT-0055')]
    page.clock.run_for(3000)
    expect(online.locator('tbody tr')).to_have_count(1)
    expect(accounts.locator('.easy-presence')).to_have_text('离线')
    status['data']['online'][0]['name'] = 'account-0055'
    page.clock.run_for(3000)
    expect(accounts.locator('.easy-presence')).to_have_text('在线')
    checks.append('presence matches exact login names rather than UCI ids or case-insensitive search text')
    status['data']['online'] = []
    page.clock.run_for(3000)
    expect(accounts.locator('.easy-presence')).to_have_text('离线')
    expect(online).to_contain_text('没有匹配的记录')
    search.fill('')
    expect(online.get_by_role('cell', name='暂无记录', exact=True)).to_be_visible()
    search.fill('account-00')
    expect(online).to_contain_text('没有匹配的记录')
    checks.append('empty snapshots mark accounts offline and empty-list guidance follows the current search')
    accounts.get_by_role('button', name='重设密码', exact=True).click()
    dialog = page.get_by_role('dialog')
    dialog.get_by_label('登录账号', exact=True).focus()
    page.keyboard.press('Shift+Tab')
    expect(dialog.get_by_role('button', name='保存并生效', exact=True)).to_be_focused()
    page.keyboard.press('Tab')
    expect(dialog.get_by_label('登录账号', exact=True)).to_be_focused()
    page.keyboard.press('Escape')
    expect(accounts.get_by_role('button', name='重设密码', exact=True)).to_be_focused()
    checks.append('modal focus wraps visible controls and Escape restores its opener')
    # Restore the full fixture and capture desktop/mobile layouts for visual QA.
    page.get_by_role('button', name='刷新', exact=True).click()
    expect(online.locator('tbody tr')).to_have_count(50)
    accounts.get_by_role('searchbox').fill('')
    page.screenshot(path=str(args.output / 'accounts-desktop.png'), full_page=True)
    page.set_viewport_size({'width': 390, 'height': 844})
    page.screenshot(path=str(args.output / 'accounts-mobile.png'), full_page=True)
    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth + 1')
    checks.append('mobile tables scroll locally without widening the page')
    snapshot['data']['supported'] = False
    page.get_by_role('button', name='刷新', exact=True).click()
    expect(online.get_by_role('button', name='断开', exact=True).first).to_be_disabled()
    expect(accounts.get_by_role('button', name='修改', exact=True).first).to_be_disabled()
    online.get_by_role('button', name='下一页', exact=True).click()
    expect(online.get_by_role('button', name='断开', exact=True).first).to_be_disabled()
    checks.append('pagination preserves mutation permissions after a full capability refresh')
    page.goto(args.url + '?lang=en', wait_until='networkidle')
    expect(page.get_by_role('searchbox', name='Search connections by account or IP')).to_be_visible()
    expect(page.locator('#easy-online .easy-pager')).to_contain_text('Page 1 of 82')
    expect(page.locator('#easy-accounts .easy-presence').first).to_have_text('Online')
    checks.append('list controls and counts render in English')
    browser.close()
assert not errors, errors
report = {'passed': len(checks), 'checks': checks, 'browser_errors': errors}
(args.output / 'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))

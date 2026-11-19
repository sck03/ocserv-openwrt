"""Exercise v3 summary/list demand and stale-response ownership in a real browser."""
import argparse
import json
from pathlib import Path

from playwright.sync_api import expect, sync_playwright
from server_browser_support import online_payload, summary_payload

parser = argparse.ArgumentParser()
parser.add_argument('--url', required=True)
parser.add_argument('--browser-channel', default='chrome')
args = parser.parse_args()
checks = []

with sync_playwright() as pw:
    browser = pw.chromium.launch(channel=args.browser_channel, headless=True)
    page = browser.new_page()
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    snapshot = page.request.get(args.url + 'easy/data').json()
    rows = [{'id': '7', 'name': 'online-fixture', 'ip': '192.0.2.1', 'vpn_ip': '10.77.0.7', 'since': 'now', 'state': 'connected'}]
    snapshot['data']['online'] = rows
    status = {'ok': True, 'data': {key: snapshot['data'].get(key, False)
                                  for key in ('running', 'online', 'online_error', 'guard', 'recovery_pending')}}
    requests = []

    def online(route):
        requests.append(route.request.url)
        route.fulfill(json=online_payload(status))

    page.route('**/easy/data', lambda route: route.fulfill(json=online_payload(snapshot)))
    page.route('**/easy/status', lambda route: route.fulfill(json=summary_payload(status)))
    page.route('**/easy/online', online)
    page.clock.install()
    page.goto(args.url, wait_until='networkidle')
    expect(page.get_by_role('cell', name='192.0.2.1', exact=True)).to_be_visible()
    for _ in range(5):
        with page.expect_response('**/easy/status'):
            page.clock.run_for(3000)
    assert not requests
    checks.append('unchanged summary generations do not transfer online rows again')

    rows[0]['ip'] = '192.0.2.2'
    page.clock.run_for(3000)
    expect(page.get_by_role('cell', name='192.0.2.2', exact=True)).to_be_visible()
    assert len(requests) == 1
    checks.append('a new summary fetches one owned online snapshot')

    page.get_by_role('tab', name='服务设置', exact=True).click()
    rows[0]['ip'] = '192.0.2.3'
    for _ in range(3):
        with page.expect_response('**/easy/status'):
            page.clock.run_for(3000)
    assert len(requests) == 1
    page.get_by_role('tab', name='账号与在线用户', exact=True).click()
    expect(page.get_by_role('cell', name='192.0.2.3', exact=True)).to_be_visible()
    assert len(requests) == 2
    checks.append('other tabs only poll summaries and returning loads the current list')

    page.evaluate('''() => {
        window.normalFetch = window.fetch;
        window.onlineReads = [];
        window.fetch = (url, options) => url.endsWith('/online')
            ? new Promise((resolve, reject) => onlineReads.push({resolve, reject, signal: options.signal}))
            : normalFetch(url, options);
    }''')

    def resolve(index, payload):
        page.evaluate('''([index, payload]) => onlineReads[index].resolve({ok:true, json:async () => payload})''', [index, payload])

    rows[0]['ip'] = '192.0.2.4'
    old = online_payload(status)
    old = json.loads(json.dumps(old))
    page.clock.run_for(3000)
    page.wait_for_function('onlineReads.length === 1')
    page.get_by_role('tab', name='服务设置', exact=True).click()
    assert page.evaluate('onlineReads[0].signal.aborted')
    rows[0]['ip'] = '192.0.2.5'
    page.get_by_role('tab', name='账号与在线用户', exact=True).click()
    page.wait_for_function('onlineReads.length === 2')
    resolve(0, old)
    assert not page.evaluate('onlineReads[1].signal.aborted')
    resolve(1, online_payload(status))
    expect(page.get_by_role('cell', name='192.0.2.5', exact=True)).to_be_visible()
    checks.append('leaving cancels a list read and its late completion cannot own the replacement')

    page.get_by_role('tab', name='服务设置', exact=True).click()
    rows[0]['ip'] = '192.0.2.6'
    with page.expect_response('**/easy/status'):
        page.clock.run_for(3000)
    stale = json.loads(json.dumps(online_payload(status)))
    page.get_by_role('tab', name='账号与在线用户', exact=True).click()
    page.wait_for_function('onlineReads.length === 3')
    rows[0]['ip'] = '192.0.2.7'
    with page.expect_response('**/easy/status'):
        page.clock.run_for(3000)
    resolve(2, stale)
    page.wait_for_function('onlineReads.length === 4')
    expect(page.get_by_role('cell', name='192.0.2.6', exact=True)).to_have_count(0)
    resolve(3, online_payload(status))
    expect(page.get_by_role('cell', name='192.0.2.7', exact=True)).to_be_visible()
    checks.append('an older list cannot roll back a newer summary and one refetch reconciles it')

    rows[0]['ip'] = '192.0.2.8'
    page.clock.run_for(3000)
    page.wait_for_function('onlineReads.length === 5')
    page.evaluate('onlineReads[4].reject(new Error("fixture unavailable"))')
    expect(page.locator('.easy-online-error')).to_be_visible()
    expect(page.locator('#easy-online tbody button')).to_have_count(0)
    page.evaluate('window.fetch = normalFetch')
    page.clock.run_for(3000)
    expect(page.get_by_role('cell', name='192.0.2.8', exact=True)).to_be_visible()
    checks.append('failed list requests remove stale actions and recover from the next summary')
    assert not errors, errors
    browser.close()

destination = Path(__file__).resolve().parents[1] / 'test-results/server-online.json'
destination.parent.mkdir(exist_ok=True)
destination.write_text(json.dumps({'passed': len(checks), 'checks': checks, 'browser_errors': errors}, indent=2) + '\n')
print(json.dumps({'passed': len(checks), 'checks': checks, 'browser_errors': errors}))

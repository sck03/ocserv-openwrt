"""Exercise browser request lifetimes with a real page and an isolated preview."""
import argparse
import json
from pathlib import Path

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

    page.evaluate("window.summaryNode = document.querySelector('.easy-summary').firstChild")
    for _ in range(3):
        with page.expect_response('**/easy/status'):
            page.clock.run_for(3000)
    assert page.evaluate("window.summaryNode === document.querySelector('.easy-summary').firstChild"), 'unchanged status rebuilt the summary'
    checks.append('unchanged router status preserves existing summary controls')

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
    assert not errors, errors
    browser.close()
report = {'passed': len(checks), 'checks': checks, 'browser_errors': errors}
output = ROOT / 'test-results/server-polling.json'
output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))

"""Check the real management page against the isolated Lua preview backend."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.tools/python-test'))
from playwright.sync_api import sync_playwright, expect

parser=argparse.ArgumentParser()
parser.add_argument('--url',default='http://127.0.0.1:22981/')
parser.add_argument('--output',type=Path,default=ROOT/'test-results/server-ui-v040')
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
checks,errors=[],[]
with sync_playwright() as pw:
    browser=pw.chromium.launch(channel='chrome',headless=True)
    page=browser.new_page(viewport={'width':1280,'height':940})
    page.on('pageerror',lambda e:errors.append(str(e)))
    page.set_default_timeout(12000)
    try:
        page.goto(args.url,wait_until='networkidle')
        expect(page.get_by_role('tab')).to_have_count(4)
        page.get_by_role('tab',name='VPN 专用上网',exact=True).click()
        expect(page.get_by_role('button',name='开启',exact=True)).to_be_enabled()
        expect(page.get_by_text('已关闭',exact=True)).to_be_visible()
        page.screenshot(path=str(args.output/'vpn-only-disabled.png'),full_page=True)
        checks.append('optional guard starts disabled with detected LAN information')
        page.get_by_role('button',name='开启',exact=True).click()
        dialog=page.get_by_role('dialog')
        expect(dialog).to_contain_text('保留当前电脑的管理访问')
        dialog.get_by_role('button',name='确认',exact=True).click()
        expect(page.get_by_text('已开启',exact=True)).to_be_visible()
        expect(page.get_by_role('button',name='关闭并恢复',exact=True)).to_be_enabled()
        page.screenshot(path=str(args.output/'vpn-only-enabled.png'),full_page=True)
        checks.append('enable confirms connectivity automatically using the refreshed configuration revision')
        page.get_by_role('tab',name='服务设置',exact=True).click()
        expect(page.get_by_role('button',name='保存并应用',exact=True)).to_be_disabled()
        page.get_by_role('tab',name='账号与在线用户',exact=True).click()
        expect(page.get_by_role('button',name='＋ 添加账号',exact=True)).to_be_enabled()
        checks.append('network changes are locked while account management remains available')
        page.get_by_role('tab',name='VPN 专用上网',exact=True).click()
        page.get_by_role('button',name='关闭并恢复',exact=True).click()
        expect(page.get_by_text('已关闭',exact=True)).to_be_visible()
        page.get_by_role('tab',name='服务设置',exact=True).click()
        expect(page.get_by_role('button',name='保存并应用',exact=True)).to_be_enabled()
        checks.append('disable restores settings and re-enables network editing')
        page.get_by_role('tab',name='客户端配置',exact=True).click()
        page.get_by_label('客户端实际连接的服务器地址',exact=True).fill('192.168.19.253:4443')
        with page.expect_download() as pending:
            page.get_by_role('button',name='下载 .bvpn 连接配置',exact=True).click()
        pending.value.save_as(str(args.output/'address.bvpn'))
        assert (args.output/'address.bvpn').read_text()=='[VPN]\nServer=https://192.168.19.253:4443\n'
        page.get_by_label('证书验证方式',exact=True).select_option('profile')
        with page.expect_download() as pending:
            page.get_by_role('button',name='下载 .bvpn 连接配置',exact=True).click()
        pending.value.save_as(str(args.output/'ca.bvpn'))
        assert 'CABase64=' in (args.output/'ca.bvpn').read_text()
        page.screenshot(path=str(args.output/'profile-export.png'),full_page=True)
        checks.append('address and CA profiles download through the real export handler')
        page.get_by_role('tab',name='VPN 专用上网',exact=True).click()
        page.set_viewport_size({'width':390,'height':844})
        page.screenshot(path=str(args.output/'vpn-only-mobile.png'),full_page=True)
        assert page.evaluate('document.documentElement.scrollWidth <= window.innerWidth + 1')
        checks.append('mobile layout fits the viewport')
        page.goto(args.url+'?lang=en',wait_until='networkidle')
        page.set_viewport_size({'width':1280,'height':940})
        page.get_by_role('tab',name='VPN-only access',exact=True).click()
        expect(page.get_by_role('button',name='Enable',exact=True)).to_be_enabled()
        page.screenshot(path=str(args.output/'vpn-only-en.png'),full_page=True)
        checks.append('English labels and controls render')
    finally:
        page.screenshot(path=str(args.output/'last-state.png'),full_page=True)
        browser.close()
        report={'passed':len(checks),'checks':checks,'browser_errors':errors}
        (args.output/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False))
    assert not errors,errors

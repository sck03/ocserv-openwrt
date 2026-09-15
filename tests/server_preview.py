"""Serve the real management UI against the regression suite's in-memory backend."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, HTTPServer
import base64
import json
import sys
import argparse
from urllib.parse import parse_qs, urlsplit

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.tools/python-test'))
from lupa.lua51 import LuaRuntime, lua_type
lua=LuaRuntime(unpack_returned_tuples=True)
source=ROOT/'server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy'
for path in source.glob('*.lua'):
    lua.execute('package.preload[...] = assert(loadstring(select(2,...)))','luci.model.ocserv_easy.'+path.stem,path.read_text(encoding='utf-8'))
lua.globals().TEST_TEMPLATE=(ROOT/'server/openwrt/ocserv/files/ocserv.conf.template').read_text(encoding='utf-8')
lua.globals().TEST_BASE64=lambda value:base64.b64encode(value.encode()).decode()
lua.globals().TEST_KEEP_SANDBOX=True
lua.execute((ROOT/'tests/server_logic_tests.lua').read_text(encoding='utf-8'))
fixture=lua.globals().TEST_PREVIEW
fixture.reset()
backend=fixture.backend
safe_call=lua.eval('function(fn,...) local ok,result=pcall(fn,...);return ok,result end')
guard_lua=LuaRuntime(unpack_returned_tuples=True)
for path in source.glob('*.lua'):
    guard_lua.execute('package.preload[...] = assert(loadstring(select(2,...)))','luci.model.ocserv_easy.'+path.stem,path.read_text(encoding='utf-8'))
guard_lua.globals().TEST_TEMPLATE=lua.globals().TEST_TEMPLATE
guard_lua.globals().TEST_GUARD=(ROOT/'server/openwrt/luci-app-ocserv-easy/root/usr/share/ocserv-easy/guard.nft.in').read_text(encoding='utf-8')
guard_lua.execute((ROOT/'tests/guard_transaction_tests.lua').read_text(encoding='utf-8'))
guard_fixture=guard_lua.globals().TEST_GUARD_PREVIEW
guard_fixture.reset()
guard_fixture.state().preview=True
guard=guard_fixture.guard
guard_revision=0
guard_worker=None
new_worker=guard_lua.eval('''function()
    local co=coroutine.create(function() require("luci.model.ocserv_easy.guard").work(false) end)
    return function() local ok,err=coroutine.resume(co); if not ok then error(err) end end
end''')

def to_lua(value):
    if isinstance(value,dict):return lua.table_from({k:to_lua(v) for k,v in value.items()})
    if isinstance(value,list):return lua.table_from([to_lua(v) for v in value])
    return value

def to_python(value):
    if lua_type(value)!='table':return value
    keys=list(value.keys())
    if keys and all(isinstance(k,(int,float)) for k in keys):return [to_python(value[i]) for i in range(1,len(keys)+1)]
    return {k:to_python(v) for k,v in value.items()}

resources=ROOT/'server/openwrt/luci-app-ocserv-easy/htdocs/luci-static/resources/ocserv-easy'
def data(status=False):
    value=to_python(backend.status() if status else backend.data())
    value['guard']=to_python(guard.status('192.168.19.2'))
    if not status:value['revision']+='-guard-'+str(guard_revision)
    return value

class Handler(BaseHTTPRequestHandler):
    def send(self,body,mime='application/json',status=200):
        raw=body if isinstance(body,bytes) else body.encode()
        self.send_response(status);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(raw)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(raw)
    def do_GET(self):
        parts=urlsplit(self.path)
        if parts.path in ('/app.js','/style.css'):
            self.send((resources/parts.path[1:]).read_bytes(),'text/javascript; charset=utf-8' if parts.path.endswith('.js') else 'text/css; charset=utf-8');return
        if parts.path in ('/easy/data','/easy/status'):
            self.send(json.dumps({'ok':True,'data':data(status=parts.path.endswith('/status'))}));return
        if parts.path!='/':self.send('{}',status=404);return
        lang=parse_qs(parts.query).get('lang',['zh-cn'])[0]
        opts=json.dumps({'base':'/easy','token':'local-preview-only','language':lang,'writable':True})
        self.send('''<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>布利杰VPN 管理页预览</title>
<style>body{margin:0;background:#f5f7fa;font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#24354b}nav{background:#243850;color:white;padding:15px 32px}main{padding:22px 32px}button{padding:6px 13px;border:1px solid #c8d2df;background:#fff;color:#294666;font:inherit}input,select,textarea{border:1px solid #c8d2df;border-radius:5px;padding:7px 9px;background:#fff;color:#24354b;font:inherit}.cbi-button-apply,.cbi-button-add{background:#275eaa;color:white;border-color:#275eaa}.cbi-button-remove{color:#a13929}.demo{float:right;color:#d4e4fb;font-size:13px}summary{cursor:pointer;margin:24px 0 18px;font-weight:600}</style>
<link rel="stylesheet" href="/style.css"><nav>OpenWrt · VPN / OpenConnect VPN <span class="demo">界面预览 · 模拟数据 / Preview · Simulated data</span></nav><main><div id="ocserv-easy"></div></main>
<script id="ocserv-easy-options" type="application/json">'''+opts+'''</script><script src="/app.js" defer></script></html>''','text/html; charset=utf-8')
    def do_POST(self):
        global guard_revision,guard_worker
        size=int(self.headers.get('Content-Length','0'))
        if size>65536:self.send('{}',status=400);return
        fields=parse_qs(self.rfile.read(size).decode())
        if fields.get('token')!=['local-preview-only']:self.send('{}',status=403);return
        if self.path=='/easy/download':
            try:
                content,filename=backend.export(fields.get('kind',[''])[0],fields.get('server',[''])[0])
                raw=content.encode()
                self.send_response(200)
                self.send_header('Content-Type','application/octet-stream')
                self.send_header('Content-Disposition','attachment; filename="'+filename+'"')
                self.send_header('Content-Length',str(len(raw)))
                self.send_header('Cache-Control','no-store')
                self.end_headers();self.wfile.write(raw)
            except Exception:
                self.send('Invalid export','text/plain',400)
            return
        if self.path!='/easy/action':self.send('{}',status=404);return
        payload=json.loads(fields.get('payload',['{}'])[0])
        if payload.get('revision')!=data()['revision']:
            self.send(json.dumps({'ok':False,'error':'stale_revision'}),status=409);return
        if payload.get('action')=='guard':
            command=payload.get('command')
            try:
                result=to_python(guard.begin(command,'192.168.19.2',payload.get('token')))
                if command in ('enable','disable'):
                    guard_worker=new_worker();guard_worker();guard_revision+=1
                elif command=='confirm' and guard_worker:
                    guard_worker();guard_worker=None
                self.send(json.dumps({'ok':True,'data':result}))
            except Exception:
                self.send(json.dumps({'ok':False,'error':'internal_error'}),status=400)
            return
        payload['revision']=backend.data()['revision']
        ok,result=safe_call(backend.action,to_lua(payload))
        result=to_python(result)
        self.send(json.dumps({'ok':True,'data':result} if ok else {'ok':False,'error':result.get('code','internal_error')}),status=200 if ok else 400)
    def log_message(self,*args):pass

parser=argparse.ArgumentParser()
parser.add_argument('--port',type=int,default=22881)
HTTPServer(('127.0.0.1',parser.parse_args().port),Handler).serve_forever()

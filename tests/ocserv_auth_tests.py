"""Authenticate with a real OpenConnect client against ocserv in a disposable OpenWrt rootfs.

Run as root on Linux after openwrt_runtime_tests.lua. The marker guard prevents
accidental execution against a router or the host filesystem. No VPN tunnel is
created; the test stops at cookie authentication, matching the reported failure.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', required=True, type=Path)
parser.add_argument('--output', type=Path, default=Path('test-results/server-real-auth.json'))
args = parser.parse_args()
root = args.root.resolve()
if os.name != 'posix' or os.geteuid() != 0 or root == Path('/') or not (root/'tmp/ocserv-easy-test-root').is_file():
    raise SystemExit('Run as root against a disposable, explicitly marked OpenWrt fixture only.')
fixture = root / 'tmp/ocserv-auth-fixture'
fixture.mkdir(mode=0o700, exist_ok=False)
os.chmod(fixture, 0o755)
certificate, key = fixture/'cert.pem', fixture/'key.pem'
subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','2',
                '-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1',
                '-keyout',str(key),'-out',str(certificate)],check=True,capture_output=True)
key.chmod(0o600)
with socket.socket() as reserved:
    reserved.bind(('127.0.0.1',0))
    port=reserved.getsockname()[1]
configuration=f'''auth = "plain[passwd=/var/etc/ocpasswd]"
listen-host = 127.0.0.1
tcp-port = {port}
run-as-user = nobody
run-as-group = nogroup
server-cert = /tmp/ocserv-auth-fixture/cert.pem
server-key = /tmp/ocserv-auth-fixture/key.pem
socket-file = /tmp/ocserv-auth-fixture/ocserv-socket
pid-file = /tmp/ocserv-auth-fixture/ocserv.pid
occtl-socket-file = /tmp/ocserv-auth-fixture/occtl.socket
use-occtl = true
isolate-workers = false
device = fixturevpn
ipv4-network = 10.78.0.0
ipv4-netmask = 255.255.255.0
dns = 1.1.1.1
max-clients = 8
max-same-clients = 1
max-ban-score = 0
cisco-client-compat = true
'''
(fixture/'ocserv.conf').write_text(configuration)
check=subprocess.run(['chroot',str(root),'/usr/sbin/ocserv','--test-config','-c','/tmp/ocserv-auth-fixture/ocserv.conf'],capture_output=True,text=True)
if check.returncode:
    raise RuntimeError('Fixture configuration rejected: '+check.stderr[-1500:])
checks=[]
account='runtime-fixture'
original="Fixture:pass' $6$ 2026"
replacement='Changed fixture password 2026!'

def login(password,success):
    result=subprocess.run(['openconnect','--protocol=anyconnect','--authenticate','--non-inter',
                           '--user',account,'--passwd-on-stdin','--cafile',str(certificate),
                           f'https://127.0.0.1:{port}'],input=password+'\n',capture_output=True,text=True,timeout=60)
    accepted=result.returncode==0 and 'COOKIE=' in result.stdout
    if accepted!=success:
        # Never include the client's stdout, which contains the session cookie.
        raise AssertionError('Unexpected authentication result: '+result.stderr[-1800:])

def mutate(payload):
    code='''local json=require"luci.jsonc";local b=require"luci.model.ocserv_easy.backend"
local r=json.parse(io.read("*a"));r.revision=b.data().revision
local ok,result=pcall(b.action,r)
if not ok then io.stderr:write(type(result)=="table" and result.code or "internal_error");os.exit(1)end'''
    result=subprocess.run(['chroot',str(root),'/usr/bin/lua','-e',code],input=json.dumps(payload),capture_output=True,text=True,timeout=90)
    if result.returncode:raise AssertionError('Account operation failed: '+result.stderr)

with (fixture/'server.log').open('w') as log:
    server=subprocess.Popen(['chroot',str(root),'/usr/sbin/ocserv','-f','-c','/tmp/ocserv-auth-fixture/ocserv.conf'],stdout=log,stderr=log,start_new_session=True)
    try:
        for _ in range(100):
            if server.poll() is not None:raise RuntimeError('ocserv exited: '+(fixture/'server.log').read_text()[-1800:])
            try:
                with socket.create_connection(('127.0.0.1',port),timeout=0.5):break
            except OSError:time.sleep(0.2)
        else:raise RuntimeError('ocserv did not start listening')
        login(original,True);checks.append('OpenConnect accepts the password created through the real management backend')
        login('Incorrect fixture password',False);checks.append('OpenConnect rejects a wrong password')
        code='''local b=require"luci.model.ocserv_easy.backend";for _,u in ipairs(b.data().users)do if u.name=="runtime-fixture" then print(u.id)end end'''
        identifier=subprocess.check_output(['chroot',str(root),'/usr/bin/lua','-e',code],text=True).strip()
        assert identifier and '\n' not in identifier
        base={'action':'save_user','id':identifier,'name':account,'group':'*','enabled':True}
        mutate(dict(base,password=replacement))
        login(replacement,True);checks.append('a changed password works immediately without restarting ocserv')
        login(original,False);checks.append('the old password stops working after a reset')
        mutate(dict(base,password='',enabled=False))
        login(replacement,False);checks.append('a disabled account is rejected by the real server')
        mutate(dict(base,password=''))
        login(replacement,True);checks.append('reenabling restores the unchanged password')
        mutate({'action':'delete_user','id':identifier})
        login(replacement,False);checks.append('a deleted account can no longer authenticate')
    finally:
        server.terminate()
        try:server.wait(timeout=10)
        except subprocess.TimeoutExpired:os.killpg(server.pid,signal.SIGKILL);server.wait(timeout=10)

args.output.parent.mkdir(parents=True,exist_ok=True)
report={'passed':len(checks),'checks':checks,'boundary':'Real OpenConnect and APK ocserv authentication in an isolated OpenWrt rootfs; no VPN tunnel or user router modified.'}
args.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))

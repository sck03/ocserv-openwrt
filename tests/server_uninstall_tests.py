"""Run the actual uninstaller with Linux file IO and isolated APK/UCI/services."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
if os.name == 'nt':
    raise SystemExit('Run uninstall tests on Linux.')
import fcntl

MOCK = r'''#!/usr/bin/env python3
import fcntl, json, os, pathlib, signal, sys
root=pathlib.Path(os.environ['UNINSTALL_ROOT'])
name=pathlib.Path(sys.argv[0]).name
args=sys.argv[1:]
with (root/'commands').open('a') as log:log.write(name+' '+' '.join(args)+'\n')
def failure(key):
    path=root/key
    if path.exists():path.unlink();sys.exit(1)
if name=='id':print('0')
elif name=='lua':
    if args[0]=='-e':
        failure('fail_recovery')
        (root/'etc/ocserv/easy-transaction/state.json').unlink(missing_ok=True)
    else:
        lock=root/'var/run/ocserv-easy/lock'
        lock.parent.mkdir(exist_ok=True)
        fd=os.open(lock,os.O_WRONLY|os.O_CREAT,0o600)
        try:fcntl.lockf(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:sys.exit(1)
        os.set_inheritable(fd,True)
        os.environ['OCSERV_EASY_LOCK_OWNER']=str(os.getpid())
        os.execv(args[1],args[1:])
elif name=='apk':
    path=root/'installed';installed=json.loads(path.read_text())
    if args[:2]==['info','-e']:sys.exit(0 if args[2] in installed else 1)
    assert args[0]=='del'
    if '--simulate' in args:failure('fail_plan');sys.exit(0)
    failure('fail_apk')
    path.write_text(json.dumps([p for p in installed if p not in args[1:]]))
    # Package removal really removes the original command and support files.
    (root/'bundle/uninstall.sh').unlink(missing_ok=True)
    (root/'bundle/with-lock.lua').unlink(missing_ok=True)
    (root/'bundle/lease.lua').unlink(missing_ok=True)
elif name=='uci':
    directory=root/'etc/config';delta=root/'delta'
    while args and args[0].startswith('-'):
        flag=args.pop(0)
        if flag=='-c':directory=pathlib.Path(args.pop(0))
        elif flag=='-t':delta=pathlib.Path(args.pop(0))/'pending'
    operation=args[0];package=args[1].split('.')[0]
    if operation=='changes':
        if os.environ.get('PENDING')==package:print('pending edit')
        sys.exit(0)
    path=directory/package
    data=json.loads((delta if delta.exists() else path).read_text())
    if operation=='export':print(json.dumps(data))
    elif operation=='get':
        key=args[1].split('.',1)[1]
        if key not in data:sys.exit(1)
        print(data[key])
    elif operation=='delete':
        key=args[1].split('.',1)[1];data.pop(key)
        delta.write_text(json.dumps(data))
    elif operation=='commit':
        failure('fail_uci')
        path.write_text(json.dumps(data));delta.unlink(missing_ok=True)
elif name=='ocserv-easy-guard' and args==['disable'] and 'libexec' in sys.argv[0]:
    failure('fail_guard')
    (root/'etc/ocserv/easy-guard/state.json').unlink(missing_ok=True)
elif name in ('ocserv','ocserv-easy-guard','nikki','momo'):
    running=root/(name+'-running');enabled=root/(name+'-enabled')
    if args==['status']:sys.exit(0 if running.exists() else 1)
    if args==['stop']:failure('fail_stop');running.unlink(missing_ok=True)
    if args==['disable']:enabled.unlink(missing_ok=True)
    if args==['restart']:failure('fail_proxy');running.touch()
elif name=='fw4':
    if os.environ.get('INTERRUPT'):os.kill(int(os.environ['OCSERV_EASY_LOCK_OWNER']),signal.SIGKILL)
    failure('fail_fw4')
elif name=='firewall':failure('fail_reload')
'''

checks = []
with tempfile.TemporaryDirectory(prefix='lansyra-uninstall-') as temporary:
    base = Path(temporary)
    runtime = ROOT / 'server/openwrt/luci-app-ocserv-easy/root'

    def fixture(name):
        root = base / name
        for directory in ('bin', 'bundle', 'etc/config', 'etc/ocserv', 'etc/init.d',
                          'etc/nikki/ucode', 'root', 'tmp', 'var/run/ocserv-easy', 'var/etc', 'usr/libexec'):
            (root / directory).mkdir(parents=True)
        for name in ('id', 'lua', 'apk', 'uci', 'fw4'):
            path=root/'bin'/name;path.write_text(MOCK);path.chmod(0o755)
        for name in ('ocserv', 'ocserv-easy-guard', 'firewall', 'nikki', 'momo'):
            path=root/'etc/init.d'/name;path.write_text(MOCK);path.chmod(0o755)
        path=root/'usr/libexec/ocserv-easy-guard';path.write_text(MOCK);path.chmod(0o755)
        (root/'installed').write_text(json.dumps(['ocserv','luci-app-ocserv-easy','luci-app-ocserv','openclash','nikki']))
        (root/'etc/config/firewall').write_text(json.dumps(dict(lan='zone',administrator='rule',ocserv_vpn='zone',
            ocserv_vpn_to_lan='forwarding',ocserv_vpn_nat='nat',ocserv_vpn_entry='rule')))
        (root/'etc/config/nikki').write_text(json.dumps(dict(lansyra_vpn='lan_access_control',subscription='keep',lan='keep')))
        (root/'etc/config/ocserv').write_text('account configuration')
        (root/'etc/ocserv/server-key.pem').write_text('synthetic private key')
        (root/'etc/config/dhcp').write_text('unchanged DNS')
        (root/'etc/config/openclash').write_text('unchanged subscription')
        (root/'etc/nikki/ucode/hijack.ut').write_text('before\nconst lan_inbound_device = ["vpns*"]; /* Lansyra VPN ingress */\nafter\n')
        for name in ('ocserv-running','ocserv-enabled','ocserv-easy-guard-running','ocserv-easy-guard-enabled','nikki-running'):
            (root/name).touch()
        source=(runtime/'usr/sbin/lansyra-uninstall').read_text()
        source=re.sub(r'/(etc|root|tmp|usr|var)/',lambda m:str(root/m[1])+'/',source)
        (root/'bundle/uninstall.sh').write_text(source)
        (root/'bundle/with-lock.lua').write_text((runtime/'usr/share/ocserv-easy/with-lock.lua').read_text())
        (root/'bundle/lease.lua').write_text('lease fixture')
        return root

    def run(root, mode='--yes', script=None, **options):
        env=dict(os.environ,UNINSTALL_ROOT=str(root),PATH=str(root/'bin')+os.pathsep+os.environ['PATH'],**options)
        result=subprocess.run(['sh',str(script or root/'bundle/uninstall.sh'),mode],input='',env=env,
                              capture_output=True,text=True,timeout=40)
        assert (root/'etc/config/dhcp').read_text()=='unchanged DNS'
        assert (root/'etc/config/openclash').read_text()=='unchanged subscription'
        return result

    root=fixture('success')
    result=run(root)
    assert result.returncode==0,result.stdout+result.stderr
    assert json.loads((root/'installed').read_text())==['openclash','nikki']
    assert json.loads((root/'etc/config/firewall').read_text())==dict(lan='zone',administrator='rule')
    assert json.loads((root/'etc/config/nikki').read_text())==dict(subscription='keep',lan='keep')
    assert (root/'etc/nikki/ucode/hijack.ut').read_text()=='before\nconst lan_inbound_device = [];\nafter\n'
    assert not (root/'etc/config/ocserv').exists() and not (root/'etc/ocserv').exists()
    assert not (root/'ocserv-running').exists() and not (root/'ocserv-easy-guard-enabled').exists()
    backup=next((root/'root').iterdir())
    assert (backup/'ocserv/server-key.pem').read_text()=='synthetic private key'
    assert backup.stat().st_mode&0o777==0o700
    assert (root/'var/run/ocserv-easy/lock').is_file()
    assert not (root/'etc/lansyra-uninstall').exists()
    checks.append('complete removal preserves subscriptions, other rules, private backup and the shared lock inode')
    result=run(root,script=backup/'uninstall.sh')
    assert result.returncode==0,result.stdout+result.stderr
    checks.append('the backup command remains runnable after its package has removed the original runtime')

    for mode in ('--dry-run','--unknown',''):
        root=fixture('mode'+(mode or 'prompt'))
        before=(root/'etc/config/firewall').read_bytes()
        result=run(root,mode)
        assert (result.returncode==0)==(mode=='--dry-run')
        assert (root/'ocserv-running').exists() and not list((root/'root').iterdir())
        assert (root/'etc/config/firewall').read_bytes()==before
    checks.append('preview, invalid arguments and unconfirmed stdin never stop services or delete data')

    for fault in ('fail_plan','fail_stop','fail_uci','fail_fw4','fail_reload','fail_proxy','fail_apk'):
        root=fixture(fault)
        before={name:(root/'etc/config'/name).read_bytes() for name in ('firewall','nikki','ocserv')}
        (root/fault).touch()
        result=run(root)
        assert result.returncode!=0,fault
        assert before=={name:(root/'etc/config'/name).read_bytes() for name in before},fault
        assert (root/'etc/ocserv/server-key.pem').exists(),fault
        result=run(root)
        assert result.returncode==0,fault+result.stdout+result.stderr
        checks.append(fault+': preserves credentials, restores changed network files and supports retry')

    root=fixture('busy')
    with (root/'var/run/ocserv-easy/lock').open('w') as lock:
        fcntl.lockf(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        assert run(root).returncode!=0
    assert (root/'ocserv-running').exists() and not list((root/'root').iterdir())
    checks.append('a shared maintenance lock prevents concurrent package or configuration changes')
    root=fixture('pending')
    assert run(root,PENDING='nikki').returncode!=0 and (root/'ocserv-running').exists()
    checks.append('uncommitted proxy edits block removal before services stop')

    for kind in ('guard','transaction'):
        root=fixture('recovery-'+kind)
        record=root/('etc/ocserv/easy-'+kind+'/state.json');record.parent.mkdir();record.touch()
        assert run(root).returncode==0
    checks.append('pending ordinary and isolation transactions are restored through the installed runtime')
    root=fixture('failed-recovery')
    record=root/'etc/ocserv/easy-guard/state.json';record.parent.mkdir();record.write_text('unreadable record')
    (root/'fail_guard').touch()
    assert run(root).returncode!=0 and record.exists() and (root/'ocserv-running').exists()
    checks.append('failed isolation recovery preserves the record and installed service')

    for edited in (False,True):
        root=fixture('interrupted'+str(edited))
        assert run(root,INTERRUPT='1').returncode!=0
        assert (root/'etc/lansyra-uninstall/prepared').exists()
        if edited:
            (root/'etc/config/firewall').write_text('later administrator edit')
            assert run(root).returncode!=0
            assert (root/'etc/config/firewall').read_text()=='later administrator edit'
            assert (root/'etc/lansyra-uninstall/prepared').exists()
        else:
            result=run(root)
            assert result.returncode==0,result.stdout+result.stderr
    checks.append('process termination recovers on retry without overwriting later administrator edits')

output=ROOT/'test-results/server-uninstall.json';output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps({'passed':len(checks),'checks':checks,
    'boundary':'Actual shell and Linux filesystem; simulated APK/UCI/services and lock launcher. No router changed.'},indent=2)+'\n')
print(json.dumps({'passed':len(checks),'report':str(output)}))

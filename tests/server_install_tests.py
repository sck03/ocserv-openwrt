"""Exercise install.sh in a temporary filesystem with simulated APK/UCI/services."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
if os.name == 'nt':
    raise SystemExit('Run installer tests on Linux.')
import fcntl

MOCK = r'''#!/usr/bin/env python3
import fcntl, json, os, pathlib, shlex, sys
root=pathlib.Path(os.environ['INSTALL_TEST_ROOT'])
name=pathlib.Path(sys.argv[0]).name
args=sys.argv[1:]
with (root/'commands').open('a') as log: log.write(name+' '+' '.join(args)+'\n')
def fail(key):
    marker=root/key
    if marker.exists(): marker.unlink(); return True
    return False
if name=='lua':
    if args[:1]==['-e']:
        if 'easy-guard/state.json' in args[1]:
            try:
                state=json.loads((root/'etc/ocserv/easy-guard/state.json').read_text())
                valid=state.get('version')==1 and state.get('phase')=='enabled' and isinstance(state.get('ops'),list) and isinstance(state.get('info'),dict)
            except (ValueError,AttributeError):valid=False
            sys.exit(0 if valid else 1)
        sys.exit(1 if os.environ.get('INSTALL_MISSING_LOCK_RUNTIME') and not (root/'lock-runtime').exists() else 0)
    from lupa.lua51 import LuaRuntime
    runtime=LuaRuntime(unpack_returned_tuples=True)
    def open_file(path,flags,mode):
        try:
            fd=os.open(path,os.O_WRONLY|os.O_CREAT,int(mode,8))
            os.set_inheritable(fd,True)
            return fd
        except OSError: return None
    def lock_file(fd,operation):
        try:
            fcntl.lockf(fd,fcntl.LOCK_UN if operation=='ulock' else fcntl.LOCK_EX|fcntl.LOCK_NB)
            return True
        except OSError: return False
    def mkdir(path,mode):
        try: os.mkdir(path,int(mode,8));return True
        except OSError: return False
    def chmod(path,mode):
        try: os.chmod(path,int(mode,8));return True
        except OSError: return False
    def setenv(key,value): os.environ[key]=value;return True
    bindings=runtime.globals()
    for key,value in {'OPEN':open_file,'LOCK':lock_file,'CLOSE':os.close,'STAT':os.path.exists,
                      'MKDIR':mkdir,'CHMOD':chmod,'SETENV':setenv,'PID':os.getpid,
                      'EXEC':lambda *command:os.execv(command[0],command)}.items():bindings[key]=value
    runtime.execute("""
        package.loaded.nixio={
            open_flags=function(...)return table.concat({...}," ")end,
            open=function(path,flags,mode)
                local fd=OPEN(path,flags,mode)
                if not fd then return nil end
                return {lock=function(_,operation)return LOCK(fd,operation)end,close=function()CLOSE(fd)end}
            end,
            setenv=SETENV,getpid=PID,exec=EXEC
        }
        package.loaded['nixio.fs']={stat=STAT,mkdir=MKDIR,chmod=CHMOD}
    """)
    command=runtime.table_from(args[1:]);command[0]=args[0];bindings.arg=command
    runtime.execute(pathlib.Path(args[0]).read_text())
elif name=='id': print('0')
elif name=='ubus': print(json.dumps({'rootfs_type':'btrfs','release':{'version':os.environ.get('INSTALL_RELEASE','25.12-SNAPSHOT')}}))
elif name=='jsonfilter': print(json.load(sys.stdin)['release']['version'])
elif name=='df':
    if os.environ.get('INSTALL_DF_FAIL') or '/etc' in args:
        print("df: /etc: can't find mount point",file=sys.stderr);sys.exit(1)
    print('Filesystem 1024-blocks Used Available Capacity Mounted on\n/dev/root 2000000 600000 1400000 30% /')
elif name=='apk':
    if args==['--print-arch']: print(os.environ.get('INSTALL_ARCH','aarch64_generic'))
    elif args[:2]==['info','-e']:
        packages=json.loads((root/'installed').read_text()) if (root/'installed').exists() else []
        sys.exit(0 if args[2] in packages else 1)
    elif args and args[0]=='add':
        if args[-1]=='luci-lib-nixio':
            if '--simulate' in args and os.environ.get('INSTALL_KERNEL'):print('(1/1) Installing kmod-tun (6.12)')
            if '--simulate' not in args:(root/'lock-runtime').touch()
            sys.exit(0)
        assert '--upgrade' in args
        if '--simulate' in args:
            print('(1/2) Installing '+('kmod-tun (6.12)' if os.environ.get('INSTALL_KERNEL') else 'ocserv (1.6.0-r3)'))
        else:
            if fail('fail_apk'): sys.exit(1)
            packages=json.loads((root/'installed').read_text()) if (root/'installed').exists() else []
            (root/'installed').write_text(json.dumps(sorted(set(packages+['ocserv','luci-app-ocserv-easy']))))
            config=root/'etc/config/ocserv'
            if not config.exists(): config.write_text('fresh ocserv configuration\n')
            if os.environ.get('INSTALL_POSTSTART'):(root/'running').touch()
    elif args and args[0]=='del':
        assert 'ocserv' not in args[1:] and 'luci-app-ocserv-easy' not in args[1:]
        packages=json.loads((root/'installed').read_text())
        (root/'installed').write_text(json.dumps([p for p in packages if p not in args[1:]]))
elif name=='uci':
    args=[arg for arg in args if arg!='-q']
    if os.environ.get('INSTALL_ACCOUNT'):
        while args and args[0]=='-t':args=args[2:]
        configuration=root/'etc/config/ocserv'
        staging=root/'account-staging'
        accounts=json.loads(configuration.read_text())
        if args[0] in ('add','set','commit'):
            with (root/'var/run/ocserv-easy/lock').open('w') as descriptor:
                try: fcntl.lockf(descriptor,fcntl.LOCK_EX|fcntl.LOCK_NB)
                except BlockingIOError: pass
                else: raise AssertionError('UCI mutation escaped the maintenance lease')
            if staging.exists(): accounts=json.loads(staging.read_text())
        if args[0]=='changes':
            if os.environ.get('INSTALL_ACCOUNT_PENDING'):print('ocserv.employee.name=pending')
        elif args[0]=='get':
            package,section,key=args[1].split('.',2)
            value=accounts.get(section,{}).get(key)
            if value is None:sys.exit(1)
            print(value)
        elif args[0]=='show':
            for section in accounts:print('ocserv.'+section+'=ocservusers')
        elif args[0]=='add':
            section='added'+str(len(accounts)+1);accounts[section]={};print(section)
            staging.write_text(json.dumps(accounts))
        elif args[0]=='set':
            key,value=args[1].split('=',1);package,section,key=key.split('.',2)
            accounts[section][key]=value;staging.write_text(json.dumps(accounts))
        elif args[0]=='commit':configuration.write_text(json.dumps(accounts));staging.unlink(missing_ok=True)
        sys.exit(0)
    configuration=root/'etc/config'
    staging=root/'uci-staging'
    while args and args[0] in ('-c','-t'):
        if args[0]=='-c': configuration=pathlib.Path(args[1])
        else: staging=pathlib.Path(args[1])/'uci-staging'
        args=args[2:]
    firewall=configuration/'firewall'
    if args[0]=='changes':
        if os.environ.get('INSTALL_PENDING'): print('firewall.user.pending=1')
    elif args[0]=='get':
        if args[1].startswith('ocserv.'):
            if os.environ.get('INSTALL_MANUAL'):
                defaults={'zone':'ocvpn','ipaddr':'10.77.0.0','netmask':'255.255.255.0','port':'4443','proxy_arp':'0'}
                print(defaults.get(args[1].split('.')[-1],''))
                sys.exit(0)
            sys.exit(1)
        key=args[1].removeprefix('firewall.')
        value=json.loads(firewall.read_text()).get(key)
        if value is None: sys.exit(1)
        print(value)
    elif args[0]=='show': print(firewall.read_text())
    elif args[0]=='revert': staging.unlink(missing_ok=True)
    elif args[0]=='batch':
        changes=json.loads(firewall.read_text())
        for line in sys.stdin:
            parts=shlex.split(line)
            if not parts: continue
            if parts[0]=='commit':
                if fail('fail_uci'): sys.exit(1)
                firewall.write_text(json.dumps(changes,sort_keys=True))
                staging.unlink(missing_ok=True)
            else:
                key,value=parts[1].split('=',1)
                changes[key.removeprefix('firewall.')]=value
                staging.write_text(json.dumps(changes))
elif name=='fw4':
    if os.environ.get('INSTALL_INTERRUPT_NETWORK'):
        import signal
        os.kill(os.getppid(),signal.SIGKILL)
        sys.exit(1)
    if fail('fail_fw4'): sys.exit(1)
elif name=='sync':
    if os.environ.get('INSTALL_INTERRUPT_COMPLETE') and (root/'etc/ocserv/easy-install-network/complete').exists():
        import signal
        os.kill(os.getppid(),signal.SIGKILL)
        sys.exit(1)
elif name=='ocpasswd':
    assert args[0]=='-c' and len(args)==3
    lock=root/'var/run/ocserv-easy/lock';lock.parent.mkdir(exist_ok=True)
    with lock.open('w') as descriptor:fcntl.lockf(descriptor,fcntl.LOCK_EX|fcntl.LOCK_NB)
    if fail('fail_password'):sys.exit(1)
    pathlib.Path(args[1]).write_text(args[2]+':*:$6$fixture$'+'x'*86+'\n')
elif name=='ocserv' and args==['initcerts']:
    if fail('fail_cert'): sys.exit(1)
elif name=='ocserv' and args==['status']:
    sys.exit(0 if (root/'running').exists() else 1)
elif name=='ocserv' and args==['stop']:
    if fail('fail_stop'): sys.exit(1)
    (root/'running').unlink(missing_ok=True)
elif name=='ocserv' and args==['start']:
    if fail('fail_start'): sys.exit(1)
    (root/'running').touch()
elif name=='ocserv' and args==['enabled']:
    sys.exit(0 if (root/'autostart').exists() else 1)
elif name=='ocserv' and args==['enable']:
    (root/'autostart').touch()
elif name=='ocserv' and args==['disable']:
    (root/'autostart').unlink(missing_ok=True)
elif name=='firewall':
    if fail('fail_reload'): sys.exit(1)
'''

checks = []
with tempfile.TemporaryDirectory(prefix='lansyra-install-test-') as temporary:
    base = Path(temporary)

    def fixture(name, existing=False, architectures='aarch64_generic\naarch64\n', package_arch=None):
        root = base / name
        for folder in ('bin', 'bundle', 'etc/config', 'etc/init.d', 'etc/ocserv', 'etc/apk', 'usr/libexec', 'root', 'tmp', 'var/run'):
            (root / folder).mkdir(parents=True)
        for command in ('apk', 'uci', 'fw4', 'id', 'ubus', 'jsonfilter', 'df', 'sync', 'lua', 'ocpasswd'):
            path = root / 'bin' / command
            path.write_text(MOCK, encoding='utf-8')
            path.chmod(0o755)
        for service in ('ocserv', 'firewall', 'rpcd', 'ocserv-easy-guard'):
            path = root / 'etc/init.d' / service
            path.write_text(MOCK, encoding='utf-8')
            path.chmod(0o755)
        (root / 'etc/config/firewall').write_text(json.dumps({'lan': 'zone', 'lan.name': 'lan'}))
        (root / 'etc/config/openclash').write_text('existing subscription and rules\n')
        (root / 'etc/config/dhcp').write_text('existing dns settings\n')
        if package_arch is not None:(root/'etc/apk/arch').write_text(package_arch+'\n')
        if existing:
            (root / 'installed').write_text(json.dumps(['ocserv','luci-app-ocserv','luci-app-ocserv-easy','luci-i18n-ocserv-zh-cn']))
            (root / 'etc/config/ocserv').write_text('existing account hashes and custom settings\n')
            (root / 'running').touch()
            (root / 'autostart').touch()
        script = (ROOT / 'server/tools/install.sh').read_text(encoding='utf-8')
        script = re.sub(r'/(etc|root|tmp|usr|var)/', lambda match: str(root / match[1]) + '/', script)
        (root / 'bundle/install.sh').write_text(script, encoding='utf-8')
        (root / 'bundle/with-lock.lua').write_text((ROOT / 'server/tools/with-lock.lua').read_text(encoding='utf-8'), encoding='utf-8')
        lease=(ROOT / 'server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy/lease.lua').read_text(encoding='utf-8')
        (root / 'bundle/lease.lua').write_text(lease.replace('/var/run/',str(root/'var/run')+'/'),encoding='utf-8')
        preflight=(ROOT/'server/tools/preflight.sh').read_text(encoding='utf-8')
        preflight=re.sub(r'/(etc|root|tmp|usr)/',lambda match:str(root/match[1])+'/',preflight)
        preflight=preflight.replace('[ -c /dev/net/tun ]','[ -f "'+str(root/'tun.available')+'" ]')
        (root/'tun.available').touch()
        (root / 'bundle/preflight.sh').write_text(preflight,encoding='utf-8')
        (root / 'bundle/APK-ARCHITECTURES').write_text(architectures)
        for package in ('ocserv-1.6.0-r3.apk', 'luci-app-ocserv-easy-0.4.1-r1.apk'):
            (root / 'bundle' / package).write_bytes(b'local test APK')
        manifest = ''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n'
                           for path in (root / 'bundle').iterdir())
        (root / 'bundle/SHA256SUMS').write_text(manifest)
        return root

    def run(root, **options):
        before = {name: (root / 'etc/config' / name).read_bytes() for name in ('openclash', 'dhcp')}
        env = dict(os.environ, INSTALL_TEST_ROOT=str(root), PATH=str(root / 'bin') + os.pathsep + os.environ['PATH'], **options)
        result = subprocess.run(['sh', str(root / 'bundle/install.sh')], env=env, capture_output=True, text=True, timeout=30)
        assert before == {name: (root / 'etc/config' / name).read_bytes() for name in before}
        return result

    root = fixture('fresh')
    result = run(root)
    assert result.returncode == 0, result.stdout + result.stderr
    firewall = json.loads((root / 'etc/config/firewall').read_text())
    assert firewall['ocserv_vpn.name'] == 'ocvpn' and firewall['ocserv_vpn_nat.src_ip'] == '10.77.0.0/24'
    assert firewall['ocserv_vpn_entry.dest_port'] == '4443'
    assert not (root / 'etc/ocserv/easy-install-pending').exists()
    assert len(list((root / 'root').glob('lansyra-before-install-*'))) == 1
    checks.append('fresh install prepares VPN forwarding and keeps OpenClash/DNS settings')
    with (root/'var/run/ocserv-easy/lock').open('w') as lease:
        fcntl.lockf(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
    checks.append('the maintenance descriptor is released when the exec-based installer exits')
    previous = (root / 'etc/config/firewall').read_bytes()
    assert run(root).returncode == 0 and (root / 'etc/config/firewall').read_bytes() == previous
    checks.append('repeating a completed install does not duplicate firewall sections')

    root=fixture('held_lease',existing=True)
    (root/'var/run/ocserv-easy').mkdir(mode=0o700)
    before={name:(root/'etc/config'/name).read_bytes() for name in ('ocserv','firewall')}
    with (root/'var/run/ocserv-easy/lock').open('w') as lease:
        fcntl.lockf(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
        result=run(root)
    assert result.returncode!=0 and '正在执行其他维护操作' in result.stderr
    assert before=={name:(root/'etc/config'/name).read_bytes() for name in before}
    assert 'apk update' not in (root/'commands').read_text() and not list((root/'root').iterdir())
    checks.append('a POSIX lease held by another editor rejects installation before mutable maintenance')

    root=fixture('bootstrap_lock_runtime')
    result=run(root,INSTALL_MISSING_LOCK_RUNTIME='1')
    assert result.returncode==0,result.stdout+result.stderr
    assert (root/'lock-runtime').exists()
    checks.append('a fresh installation bootstraps the Lua record-lock runtime before VPN changes')

    root=fixture('bootstrap_kernel_plan')
    assert run(root,INSTALL_MISSING_LOCK_RUNTIME='1',INSTALL_KERNEL='1').returncode!=0
    assert not (root/'lock-runtime').exists() and not (root/'installed').exists()
    checks.append('lock-runtime bootstrapping also rejects a package plan involving kernel modules')

    root=fixture('pending_management_transaction',existing=True)
    (root/'etc/ocserv/easy-transaction').mkdir()
    (root/'etc/ocserv/easy-transaction/state.json').write_text('pending recovery')
    result=run(root)
    assert result.returncode!=0 and '待恢复的管理页事务' in result.stderr
    assert 'apk update' not in (root/'commands').read_text() and not list((root/'root').iterdir())
    checks.append('a pending management transaction is preserved for recovery before an upgrade')

    for phase in ('queued','applying','pending','restoring','recovery_failed','invalid'):
        root=fixture('guard_'+phase,existing=True)
        (root/'etc/ocserv/easy-guard').mkdir()
        (root/'etc/ocserv/easy-guard/state.json').write_text('invalid' if phase=='invalid' else json.dumps({'version':1,'phase':phase,'ops':[],'info':{}}))
        result=run(root)
        assert result.returncode!=0 and 'APK 版本未回滚' not in result.stdout
        assert 'apk update' not in (root/'commands').read_text() and not list((root/'root').iterdir())
    checks.append('an unconfirmed, interrupted or invalid guard transaction blocks upgrades before service changes')

    root=fixture('guard_enabled',existing=True)
    (root/'etc/ocserv/easy-guard').mkdir()
    record=json.dumps({'version':1,'phase':'enabled','ops':[],'info':{}})
    (root/'etc/ocserv/easy-guard/state.json').write_text(record)
    result=run(root)
    assert result.returncode==0,result.stdout+result.stderr
    assert (root/'etc/ocserv/easy-guard/state.json').read_text()==record
    checks.append('upgrades retain an already confirmed VPN-only guard')

    root=fixture('interrupted_network')
    assert run(root,INSTALL_INTERRUPT_NETWORK='1').returncode!=0
    journal=root/'etc/ocserv/easy-install-network'
    assert (journal/'prepared').exists() and (root/'etc/ocserv/easy-install-pending').exists()
    result=run(root)
    assert result.returncode==0,result.stdout+result.stderr
    assert not journal.exists() and not (root/'etc/ocserv/easy-install-pending').exists()
    checks.append('an interrupted network commit is recovered before retrying the first installation')

    root=fixture('interrupted_complete')
    assert run(root,INSTALL_INTERRUPT_COMPLETE='1').returncode!=0
    previous=(root/'etc/config/firewall').read_bytes()
    result=run(root)
    assert result.returncode==0,result.stdout+result.stderr
    assert (root/'etc/config/firewall').read_bytes()==previous
    assert not (root/'etc/ocserv/easy-install-network').exists()
    checks.append('a verified installation interrupted before final cleanup resumes as an upgrade')

    root=fixture('interrupted_then_edited')
    assert run(root,INSTALL_INTERRUPT_NETWORK='1').returncode!=0
    firewall=json.loads((root/'etc/config/firewall').read_text());firewall['administrator.change']='preserved'
    (root/'etc/config/firewall').write_text(json.dumps(firewall))
    previous=(root/'etc/config/firewall').read_bytes()
    assert run(root).returncode!=0
    assert (root/'etc/config/firewall').read_bytes()==previous
    assert (root/'etc/ocserv/easy-install-network/before').is_file()
    checks.append('interrupted installation preserves later administrator edits and recovery records')

    root = fixture('existing', existing=True)
    previous = {name: (root / 'etc/config' / name).read_bytes() for name in ('ocserv', 'firewall')}
    assert run(root).returncode == 0
    assert previous == {name: (root / 'etc/config' / name).read_bytes() for name in previous}
    checks.append('upgrades preserve existing account and network configuration')
    assert set(json.loads((root/'installed').read_text()))=={'ocserv','luci-app-ocserv-easy'}
    assert (root/'running').exists()
    assert (root/'autostart').exists()
    commands=(root/'commands').read_text()
    assert 'ocserv status' in commands and 'ocserv stop' in commands and 'ocserv start' in commands
    checks.append('in-place upgrades stop and restore a previously running VPN service')
    checks.append('upgrade retires duplicate upstream pages after installing the replacement')

    root=fixture('stopped_upgrade',existing=True)
    (root/'running').unlink()
    (root/'autostart').unlink()
    result=run(root,INSTALL_POSTSTART='1')
    assert result.returncode==0,result.stdout+result.stderr
    assert not (root/'running').exists() and not (root/'autostart').exists()
    assert '将执行首次网络初始化' not in result.stdout
    checks.append('an in-place upgrade preserves stopped and autostart-disabled states despite package hooks')

    root=fixture('aarch64')
    result=run(root,INSTALL_ARCH='aarch64')
    assert result.returncode==0,result.stdout+result.stderr
    checks.append('OPL APK architecture aarch64 is accepted')

    root=fixture('firmware_generic',package_arch='aarch64_generic')
    result=run(root,INSTALL_ARCH='aarch64')
    assert result.returncode==0,result.stdout+result.stderr
    assert 'Firmware package architecture: aarch64_generic' in result.stdout
    checks.append('the firmware aarch64_generic override takes precedence over the APK aarch64 build alias')

    root=fixture('wrong_a53',architectures='aarch64_cortex-a53\n',package_arch='aarch64_generic')
    result=run(root,INSTALL_ARCH='aarch64')
    assert result.returncode!=0 and not (root/'installed').exists()
    assert 'openwrt-25.12-aarch64_generic' in result.stdout
    checks.append('an N1 with generic firmware rejects the A53 bundle and names the correct download')

    root=fixture('firmware_a53',architectures='aarch64_cortex-a53\n',package_arch='aarch64_cortex-a53')
    result=run(root,INSTALL_ARCH='aarch64')
    assert result.returncode==0,result.stdout+result.stderr
    checks.append('firmware explicitly configured for aarch64_cortex-a53 accepts its matching bundle')

    root=fixture('invalid_arch',package_arch='aarch64 generic')
    assert run(root).returncode!=0 and not (root/'installed').exists()
    checks.append('an invalid architecture override fails before package or network changes')

    for architecture in ('x86_64', 'aarch64_cortex-a53', 'arm_cortex-a15_neon-vfpv4', 'mipsel_24kc', 'mips_24kc'):
        root = fixture(architecture, architectures=architecture + '\n')
        result = run(root, INSTALL_ARCH=architecture)
        assert result.returncode == 0, result.stdout + result.stderr
        checks.append(architecture + ': matching bundle installs')
    root = fixture('missing_arch_manifest')
    (root / 'bundle/APK-ARCHITECTURES').unlink()
    assert run(root).returncode != 0 and not (root / 'installed').exists()

    root=fixture('btrfs_df')
    result=run(root,INSTALL_DF_FAIL='1')
    assert result.returncode==0,result.stdout+result.stderr
    assert 'storage details are unavailable' in result.stdout
    checks.append('real preflight continues when BusyBox df cannot resolve a btrfs mount')

    root=fixture('manual',existing=True)
    previous=(root/'etc/config/ocserv').read_bytes()
    network=(root/'etc/config/firewall').read_bytes()
    result=run(root,INSTALL_MANUAL='1')
    assert result.returncode==0,result.stdout+result.stderr
    assert (root/'etc/config/ocserv').read_bytes()==previous
    assert (root/'etc/config/firewall').read_bytes()==network
    checks.append('existing APK configuration is not inferred to be a fresh installation')

    root=fixture('mixed_bundle')
    (root/'bundle/luci-app-ocserv-1.apk').write_bytes(b'stale upstream package')
    assert run(root).returncode!=0 and not (root/'installed').exists()
    checks.append('a mixture of old and new APKs is rejected before installing')

    for fault in ('fail_apk', 'fail_cert', 'fail_uci', 'fail_fw4', 'fail_reload'):
        root = fixture(fault)
        previous = (root / 'etc/config/firewall').read_bytes()
        (root / fault).touch()
        result = run(root)
        assert result.returncode != 0, fault
        assert (root / 'etc/config/firewall').read_bytes() == previous, fault
        assert (root / 'etc/ocserv/easy-install-pending').exists(), fault
        assert not (root / 'uci-staging').exists(), fault
        result = run(root)
        assert result.returncode == 0, fault + result.stdout + result.stderr
        assert not (root / 'etc/ocserv/easy-install-pending').exists(), fault
        assert 'ocserv_vpn' in json.loads((root / 'etc/config/firewall').read_text()), fault
        checks.append(fault + ': failure preserves network and a retry finishes fresh setup')

    root = fixture('upgrade_failure', existing=True)
    (root / 'fail_apk').touch()
    result = run(root)
    assert result.returncode != 0 and (root / 'running').exists()
    checks.append('an interrupted in-place upgrade restores the previously running service')

    root = fixture('upgrade_preflight_failure', existing=True)
    result = run(root, INSTALL_KERNEL='1')
    assert result.returncode != 0 and (root / 'running').exists()
    commands=(root/'commands').read_text()
    assert 'ocserv stop' not in commands and 'ocserv start' not in commands
    checks.append('a preflight failure leaves an already running service untouched')

    for name, options in [('kernel', {'INSTALL_KERNEL': '1'}), ('pending', {'INSTALL_PENDING': '1'}), ('wrong_arch', {'INSTALL_ARCH': 'x86_64'}), ('wrong_release', {'INSTALL_RELEASE':'24.10.5'})]:
        root = fixture(name)
        assert run(root, **options).returncode != 0
        assert not (root / 'installed').exists()
        assert not list((root / 'root').iterdir())
        checks.append(name + ': rejected before package installation or configuration changes')

    root=fixture('no_tun');(root/'tun.available').unlink()
    assert run(root).returncode!=0 and not (root/'installed').exists()
    checks.append('missing firmware TUN support fails before changes')

    root = fixture('checksum')
    (root / 'bundle/ocserv-1.6.0-r3.apk').write_bytes(b'corrupted')
    assert run(root).returncode != 0 and not (root / 'installed').exists()
    checks.append('a corrupted bundle is rejected before installing packages')

    def account_fixture(name, duplicate=False):
        root=fixture(name,existing=True)
        (root/'lib').mkdir()
        (root/'lib/functions.sh').write_text('''config_load() { :; }
config_get() {
    local value
    value=$(uci -q get "ocserv.$2.$3" 2>/dev/null || true)
    export "$1=$value"
}
config_foreach() {
    local section
    for section in $(uci -q show ocserv | sed 's/^ocserv[.]//;s/=.*//'); do "$1" "$section"; done
}
''')
        accounts={'employee':{'name':'employee01','group':'*','password':'unchanged hash'}}
        if duplicate:accounts['duplicate']=dict(accounts['employee'])
        (root/'etc/config/ocserv').write_text(json.dumps(accounts))
        source=(ROOT/'server/tools/add-user.sh').read_text(encoding='utf-8')
        source=re.sub(r'/(etc|root|tmp|usr|var|lib)/',lambda match:str(root/match[1])+'/',source)
        (root/'bundle/add-user.sh').write_text(source,encoding='utf-8')
        return root

    def add_user(root,name='employee01',**options):
        env=dict(os.environ,INSTALL_TEST_ROOT=str(root),INSTALL_ACCOUNT='1',
                 PATH=str(root/'bin')+os.pathsep+os.environ['PATH'],**options)
        result=subprocess.run(['sh',str(root/'bundle/add-user.sh'),name],env=env,capture_output=True,text=True,timeout=15)
        assert not list((root/'tmp').glob('lansyra-ocpasswd.*'))
        return result

    root=account_fixture('account_update')
    result=add_user(root)
    assert result.returncode==0,result.stdout+result.stderr
    accounts=json.loads((root/'etc/config/ocserv').read_text())
    assert len(accounts)==1 and accounts['employee']['password']=='$6$fixture$'+'x'*86
    checks.append('interactive account updates prompt outside the lease and hold it throughout UCI mutations')

    root=account_fixture('account_create')
    result=add_user(root,'second-user')
    assert result.returncode==0,result.stdout+result.stderr
    accounts=json.loads((root/'etc/config/ocserv').read_text())
    assert len(accounts)==2 and accounts['employee']['password']=='unchanged hash'
    checks.append('command-line account creation preserves all other account hashes')

    root=account_fixture('account_busy')
    (root/'var/run/ocserv-easy').mkdir(mode=0o700)
    # Password input is outside the lock, so simulate contention only after
    # ocpasswd has finished, just before launching the actual shared lease.
    source=(root/'bin/lua').read_text()
    source=source.replace("from lupa.lua51 import LuaRuntime", """descriptor=os.open(root/'var/run/ocserv-easy/lock',os.O_WRONLY|os.O_CREAT,0o600)
    if os.fork()==0:
        fcntl.lockf(descriptor,fcntl.LOCK_EX)
        (root/'held-account-lease').touch()
        import time
        time.sleep(2)
        os._exit(0)
    import time
    while not (root/'held-account-lease').exists():time.sleep(0.01)
    from lupa.lua51 import LuaRuntime""")
    (root/'bin/lua').write_text(source)
    before=(root/'etc/config/ocserv').read_bytes()
    result=add_user(root)
    assert result.returncode!=0 and (root/'etc/config/ocserv').read_bytes()==before
    checks.append('an account save racing another maintenance operation fails without committing any changes')

    for name,options in (('account_pending',{'INSTALL_ACCOUNT_PENDING':'1'}),('account_duplicate',{}),('account_recovery',{})):
        root=account_fixture(name,duplicate=name=='account_duplicate')
        if name=='account_recovery':
            (root/'etc/ocserv/easy-transaction').mkdir()
            (root/'etc/ocserv/easy-transaction/state.json').touch()
        before=(root/'etc/config/ocserv').read_bytes()
        assert add_user(root,**options).returncode!=0
        assert (root/'etc/config/ocserv').read_bytes()==before
        checks.append(name+': existing edits and recovery records remain intact')

    root=account_fixture('account_option')
    assert add_user(root,'-option').returncode!=0 and 'ocpasswd ' not in (root/'commands').read_text()
    checks.append('option-like account names are rejected before invoking ocpasswd')

report = {'passed': len(checks), 'checks': checks,
          'boundary': 'Real install.sh and Linux file operations; simulated APK/UCI/services, no N1 changed.'}
output = ROOT / 'test-results/server-install.json'
output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'passed': len(checks), 'report': str(output)}))

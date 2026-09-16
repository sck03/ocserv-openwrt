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

MOCK = r'''#!/usr/bin/env python3
import json, os, pathlib, shlex, sys
root=pathlib.Path(os.environ['INSTALL_TEST_ROOT'])
name=pathlib.Path(sys.argv[0]).name
args=sys.argv[1:]
with (root/'commands').open('a') as log: log.write(name+' '+' '.join(args)+'\n')
def fail(key):
    marker=root/key
    if marker.exists(): marker.unlink(); return True
    return False
if name=='id': print('0')
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
        if '--simulate' in args:
            print('(1/2) Installing '+('kmod-tun (6.12)' if os.environ.get('INSTALL_KERNEL') else 'ocserv (1.5.0-r3)'))
        else:
            if fail('fail_apk'): sys.exit(1)
            packages=json.loads((root/'installed').read_text()) if (root/'installed').exists() else []
            (root/'installed').write_text(json.dumps(sorted(set(packages+['ocserv','luci-app-ocserv-easy']))))
            config=root/'etc/config/ocserv'
            if not config.exists(): config.write_text('fresh ocserv configuration\n')
    elif args and args[0]=='del':
        assert 'ocserv' not in args[1:] and 'luci-app-ocserv-easy' not in args[1:]
        packages=json.loads((root/'installed').read_text())
        (root/'installed').write_text(json.dumps([p for p in packages if p not in args[1:]]))
elif name=='uci':
    args=[arg for arg in args if arg!='-q']
    firewall=root/'etc/config/firewall'
    staging=root/'uci-staging'
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
    if fail('fail_fw4'): sys.exit(1)
elif name=='ocserv' and args==['initcerts']:
    if fail('fail_cert'): sys.exit(1)
elif name=='firewall':
    if fail('fail_reload'): sys.exit(1)
elif name=='ocserv-easy-repair-users':
    if fail('fail_accounts'): sys.exit(1)
'''

checks = []
with tempfile.TemporaryDirectory(prefix='bulijie-install-test-') as temporary:
    base = Path(temporary)

    def fixture(name, existing=False):
        root = base / name
        for folder in ('bin', 'bundle', 'etc/config', 'etc/init.d', 'etc/ocserv', 'usr/libexec', 'root', 'tmp'):
            (root / folder).mkdir(parents=True)
        for command in ('apk', 'uci', 'fw4', 'id', 'ubus', 'jsonfilter', 'df'):
            path = root / 'bin' / command
            path.write_text(MOCK, encoding='utf-8')
            path.chmod(0o755)
        for service in ('ocserv', 'firewall', 'rpcd', 'ocserv-easy-guard'):
            path = root / 'etc/init.d' / service
            path.write_text(MOCK, encoding='utf-8')
            path.chmod(0o755)
        repair=root/'usr/libexec/ocserv-easy-repair-users'
        repair.write_text(MOCK,encoding='utf-8');repair.chmod(0o755)
        (root / 'etc/config/firewall').write_text(json.dumps({'lan': 'zone', 'lan.name': 'lan'}))
        (root / 'etc/config/openclash').write_text('existing subscription and rules\n')
        (root / 'etc/config/dhcp').write_text('existing dns settings\n')
        if existing:
            (root / 'installed').write_text(json.dumps(['ocserv','luci-app-ocserv','luci-app-ocserv-easy','luci-i18n-ocserv-zh-cn']))
            (root / 'etc/config/ocserv').write_text('existing account hashes and custom settings\n')
        script = (ROOT / 'server/tools/install.sh').read_text(encoding='utf-8')
        script = re.sub(r'/(etc|root|tmp|usr)/', lambda match: str(root / match[1]) + '/', script)
        (root / 'bundle/install.sh').write_text(script, encoding='utf-8')
        preflight=(ROOT/'server/tools/preflight-n1.sh').read_text(encoding='utf-8')
        preflight=re.sub(r'/(etc|root|tmp|usr)/',lambda match:str(root/match[1])+'/',preflight)
        preflight=preflight.replace('[ -c /dev/net/tun ]','[ -f "'+str(root/'tun.available')+'" ]')
        (root/'tun.available').touch()
        (root / 'bundle/preflight-n1.sh').write_text(preflight,encoding='utf-8')
        for package in ('ocserv-1.5.0-r3.apk', 'luci-app-ocserv-easy-0.4.1-r1.apk'):
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
    assert len(list((root / 'root').glob('bulijie-before-install-*'))) == 1
    checks.append('fresh install prepares VPN forwarding and keeps OpenClash/DNS settings')
    previous = (root / 'etc/config/firewall').read_bytes()
    assert run(root).returncode == 0 and (root / 'etc/config/firewall').read_bytes() == previous
    checks.append('repeating a completed install does not duplicate firewall sections')

    root = fixture('existing', existing=True)
    previous = {name: (root / 'etc/config' / name).read_bytes() for name in ('ocserv', 'firewall')}
    assert run(root).returncode == 0
    assert previous == {name: (root / 'etc/config' / name).read_bytes() for name in previous}
    checks.append('upgrades preserve existing account and network configuration')
    assert set(json.loads((root/'installed').read_text()))=={'ocserv','luci-app-ocserv-easy'}
    checks.append('upgrade retires duplicate upstream pages after installing the replacement')

    root=fixture('aarch64')
    result=run(root,INSTALL_ARCH='aarch64')
    assert result.returncode==0,result.stdout+result.stderr
    checks.append('OPL APK architecture aarch64 is accepted')

    root=fixture('btrfs_df')
    result=run(root,INSTALL_DF_FAIL='1')
    assert result.returncode==0,result.stdout+result.stderr
    assert 'storage details are unavailable' in result.stdout
    checks.append('real preflight continues when BusyBox df cannot resolve a btrfs mount')

    root=fixture('manual',existing=True)
    previous=(root/'etc/config/ocserv').read_bytes()
    result=run(root,INSTALL_MANUAL='1')
    assert result.returncode==0,result.stdout+result.stderr
    assert (root/'etc/config/ocserv').read_bytes()==previous
    assert 'ocserv_vpn' in json.loads((root/'etc/config/firewall').read_text())
    checks.append('retry after manual APK installation finishes default VPN networking and retains accounts')

    root=fixture('mixed_bundle')
    (root/'bundle/luci-app-ocserv-1.apk').write_bytes(b'stale upstream package')
    assert run(root).returncode!=0 and not (root/'installed').exists()
    checks.append('a mixture of old and new APKs is rejected before installing')

    for fault in ('fail_apk', 'fail_accounts', 'fail_cert', 'fail_uci', 'fail_fw4', 'fail_reload'):
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
    (root / 'bundle/ocserv-1.5.0-r3.apk').write_bytes(b'corrupted')
    assert run(root).returncode != 0 and not (root / 'installed').exists()
    checks.append('a corrupted bundle is rejected before installing packages')

report = {'passed': len(checks), 'checks': checks,
          'boundary': 'Real install.sh and Linux file operations; simulated APK/UCI/services, no N1 changed.'}
output = ROOT / 'test-results/server-install.json'
output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'passed': len(checks), 'report': str(output)}))

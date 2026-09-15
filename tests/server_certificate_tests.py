"""Run the packaged certificate initialization function with real certtool in a temporary directory."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
if os.name=='nt':
    raise SystemExit('Run this certificate test on Linux with certtool and openssl.')
for tool in ('certtool','openssl','sh'):
    if not shutil.which(tool):raise SystemExit('Missing test dependency: '+tool)
source=(ROOT/'server/openwrt/ocserv/files/ocserv.init').read_text(encoding='utf-8')
function=re.search(r'(?ms)^initcerts\(\) \(\n.*?^\)\n',source).group(0)
checks=[]
with tempfile.TemporaryDirectory(prefix='bulijie-cert-test-') as temporary:
    base=Path(temporary)
    def initialize(folder):
        script=base/'initialize.sh'
        text='''#!/bin/sh
uci() { printf '%s\\n' OPLForN1; }
network_get_ipaddr() { lan_address=192.168.19.253; }
logger() { :; }
'''+function.replace('/etc/ocserv',str(folder))+'\ninitcerts\n'
        script.write_text(text,encoding='utf-8')
        return subprocess.run(['sh',str(script)],capture_output=True,text=True,timeout=120)
    folder=base/'ocserv'
    result=initialize(folder)
    assert result.returncode==0,result.stderr
    checks.append('fresh initialization generates CA and server key/certificate pairs')
    for name in ('ca-key.pem','server-key.pem'):
        assert stat.S_IMODE((folder/name).stat().st_mode)==0o600
    assert stat.S_IMODE((folder/'pki').stat().st_mode)==0o700
    checks.append('private keys and working templates have restrictive permissions')
    subprocess.run(['openssl','verify','-CAfile',str(folder/'ca.pem'),'-verify_ip','192.168.19.253',str(folder/'server-cert.pem')],check=True,capture_output=True)
    subprocess.run(['openssl','verify','-CAfile',str(folder/'ca.pem'),'-verify_hostname','OPLForN1',str(folder/'server-cert.pem')],check=True,capture_output=True)
    checks.append('the generated certificate verifies for the N1 LAN IP and hostname')
    subprocess.run(['openssl','verify','-CAfile',str(folder/'ca.pem'),'-purpose','sslserver',str(folder/'server-cert.pem')],check=True,capture_output=True)
    checks.append('the generated leaf has TLS server certificate usage')
    digests={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.glob('*.pem')}
    assert initialize(folder).returncode==0
    assert digests=={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.glob('*.pem')}
    checks.append('repeated initialization keeps all existing keys and certificates')
    (folder/'ca-key.pem').unlink()
    assert initialize(folder).returncode==0 and not (folder/'ca-key.pem').exists()
    assert hashlib.sha256((folder/'server-cert.pem').read_bytes()).hexdigest()==digests['server-cert.pem']
    checks.append('an existing server pair does not cause an offline CA key to be replaced')
    orphan=base/'orphan'
    orphan.mkdir()
    shutil.copyfile(folder/'ca.pem',orphan/'ca.pem')
    assert initialize(orphan).returncode!=0 and not (orphan/'ca-key.pem').exists()
    checks.append('an incomplete CA is not silently replaced with an unrelated private key')
output=ROOT/'test-results/server-certificates.json'
output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps({'passed':len(checks),'checks':checks,'boundary':'Real GnuTLS certtool/OpenSSL; temporary Linux filesystem, not the N1.'},indent=2)+'\n',encoding='utf-8')
print(json.dumps({'passed':len(checks),'report':str(output)}))

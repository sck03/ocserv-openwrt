"""Run the packaged certificate initialization function with real certtool in a temporary directory."""
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
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
    def initialize(folder, certificate=None, private_key=None):
        script=base/'initialize.sh'
        text='''#!/bin/sh
uci() { printf '%s\\n' OPLForN1; }
config_load() { :; }
config_get() {
    case "$1" in
        certificate) certificate=${TEST_CERTIFICATE:-$4};;
        private_key) private_key=${TEST_PRIVATE_KEY:-$4};;
    esac
}
network_get_ipaddr() { lan_address=192.168.19.253; }
logger() { :; }
'''+function.replace('/etc/ocserv',str(folder))+'\n'
        text+='TEST_CERTIFICATE='+shlex.quote(str(certificate or ''))+'\n'
        text+='TEST_PRIVATE_KEY='+shlex.quote(str(private_key or ''))+'\ninitcerts\n'
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
    public=subprocess.run(['openssl','x509','-in',str(folder/'server-cert.pem'),'-pubkey','-noout'],check=True,capture_output=True).stdout
    spki=subprocess.run(['openssl','pkey','-pubin','-outform','DER'],input=public,check=True,capture_output=True).stdout
    expected_pin='pin-sha256:'+base64.b64encode(hashlib.sha256(spki).digest()).decode('ascii')
    key_info=subprocess.run(['certtool','--pubkey-info','--load-certificate',str(folder/'server-cert.pem')],check=True,capture_output=True,text=True)
    assert expected_pin in key_info.stdout+key_info.stderr
    checks.append('exported certtool pin matches the OpenConnect SHA-256 SPKI fingerprint, not the whole certificate')
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
    external=base/'external'
    assert initialize(external,folder/'server-cert.pem',folder/'server-key.pem').returncode==0
    assert not external.exists()
    assert initialize(orphan,folder/'server-cert.pem',folder/'server-key.pem').returncode==0
    assert not (orphan/'ca-key.pem').exists()
    checks.append('configured public certificates work without bootstrap files or a local CA key')
    assert initialize(external,folder/'server-cert.pem',external/'missing-key.pem').returncode!=0
    assert not external.exists()
    checks.append('a missing external private key fails without creating unrelated bootstrap credentials')
    assert hashlib.sha256((folder/'server-cert.pem').read_bytes()).hexdigest()==digests['server-cert.pem']
    assert hashlib.sha256((folder/'server-key.pem').read_bytes()).hexdigest()==digests['server-key.pem']
    checks.append('external certificate initialization leaves the supplied key pair unchanged')
output=ROOT/'test-results/server-certificates.json'
output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps({'passed':len(checks),'checks':checks,'boundary':'Real GnuTLS certtool/OpenSSL; temporary Linux filesystem, not the N1.'},indent=2)+'\n',encoding='utf-8')
print(json.dumps({'passed':len(checks),'report':str(output)}))

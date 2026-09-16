"""Exercise the actual init-script account writer with hostile and legacy records."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
if os.name=='nt':raise SystemExit('Run init-script tests on Linux.')
source=(ROOT/'server/openwrt/ocserv/files/ocserv.init').read_text(encoding='utf-8')
function=re.search(r'(?ms)^setup_users\(\) \{\n.*?^\}\n',source).group(0)
password_hash='$6$fixtureSalt$'+'a'*86
cases=[
    ('valid hash','employee','*',password_hash,'employee:*:'+password_hash+'\n'),
    ('missing group','employee','',password_hash,'employee:*:'+password_hash+'\n'),
    ('legacy plaintext','employee','*','test','employee:*:!\n'),
    ('disabled hash','employee','*','!'+password_hash,'employee:*:!'+password_hash+'\n'),
    ('password delimiter','employee','*',password_hash+':injected','employee:*:!\n'),
    ('password newline','employee','*',password_hash+'\nother:*:secret','employee:*:!\n'),
    ('invalid username','bad:name','*',password_hash,''),
    ('invalid group','employee','bad:group',password_hash,''),
    ('anonymous placeholder','','','', ''),
]
checks=[]
with tempfile.TemporaryDirectory(prefix='ocserv-userfile-') as temporary:
    folder=Path(temporary)
    script=folder/'writer.sh'
    script.write_text('''#!/bin/sh
config_get() {
    case "$3" in
        name) name=$TEST_NAME;;
        group) group=$TEST_GROUP;;
        password) password=$TEST_PASSWORD;;
    esac
}
logger() { :; }
'''+function+'\nocpasswd_new=$1\nsetup_users fixture\n',encoding='utf-8')
    for label,name,group,password,expected in cases:
        target=folder/'ocpasswd'
        target.write_text('')
        env=dict(os.environ,TEST_NAME=name,TEST_GROUP=group,TEST_PASSWORD=password)
        subprocess.run(['sh',str(script),str(target)],env=env,check=True,capture_output=True)
        assert target.read_text()==expected,label
        checks.append(label)
report={'passed':len(checks),'checks':checks,'boundary':'Actual setup_users shell function; synthetic records in a temporary filesystem.'}
output=ROOT/'test-results/server-userfile.json'
output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'passed':len(checks),'report':str(output)}))

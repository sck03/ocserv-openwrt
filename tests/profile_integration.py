"""Import/export tests use isolated files, never the user's live VPN profile."""
import argparse
import base64
import json
from pathlib import Path
import subprocess
import ssl

p=argparse.ArgumentParser()
p.add_argument('--client', type=Path, required=True)
p.add_argument('--certificate', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
args=p.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
encoded=base64.b64encode(args.certificate.read_bytes()).decode('ascii')
cases=[
    ('ca_bundle', f'[VPN]\nServer=https://192.168.19.254:4443\nCABase64={encoded}\n', True),
    ('pin_bundle', '[VPN]\nServer=https://vpn.example.com\nServerPin=pin-sha256:'+'A'*43+'=\n', True),
    ('untrusted_mixed_modes', f'[VPN]\nServer=https://vpn.example.com\nCABase64={encoded}\nServerPin=pin-sha256:'+'A'*43+'=\n', False),
    ('plaintext_url', f'[VPN]\nServer=http://vpn.example.com\nCABase64={encoded}\n', False),
    ('address_only_profile', '[VPN]\nServer=https://vpn.example.com\n', True),
    ('truncated_pin', '[VPN]\nServer=https://vpn.example.com\nServerPin=pin-sha256:AAAA\n', False),
    ('private_key', '[VPN]\nServer=https://vpn.example.com\nCABase64='+base64.b64encode(b'-----BEGIN PRIVATE KEY-----\nprivate\n-----END PRIVATE KEY-----').decode()+'\n', False),
    ('invalid_reconnect', '[VPN]\nServer=https://vpn.example.com\nReconnectSeconds=301\n', False),
    ('invalid_boolean', '[VPN]\nServer=https://vpn.example.com\nProtectDNS=yes\n', False),
]
results=[]
for name, contents, expected in cases:
    directory=args.output/name
    directory.mkdir(exist_ok=True)
    source=directory/'input.bvpn'
    source.write_text(contents,encoding='utf-16')
    destination=directory/'connection.ini'
    before=destination.read_bytes() if destination.exists() else None
    run=subprocess.run([str(args.client.resolve()),'--import-profile',str(source.resolve()),str(destination.resolve())],capture_output=True,text=True,timeout=10)
    passed=((run.returncode==0)==expected)
    if expected and run.returncode==0:
        text=destination.read_text(encoding='utf-16')
        passed=passed and 'LockServer=1' not in text and 'password' not in text.lower()
    elif not expected:
        passed=passed and (destination.read_bytes() if destination.exists() else None)==before
    result={'test':name,'passed':passed,'exit_code':run.returncode}
    results.append(result)
    print(json.dumps(result))
for name, source in [('direct_ca_pem',args.certificate),('direct_ca_der',args.output/'ca.cer')]:
    if name=='direct_ca_der':
        source.write_bytes(ssl.PEM_cert_to_DER_cert(args.certificate.read_text(encoding='ascii')))
    directory=args.output/name
    directory.mkdir(exist_ok=True)
    run=subprocess.run([str(args.client.resolve()),'--import-profile',str(source.resolve()),str((directory/'connection.ini').resolve())],capture_output=True,text=True,timeout=10)
    result={'test':name,'passed':run.returncode==0,'exit_code':run.returncode}
    results.append(result)
    print(json.dumps(result))
import hashlib
pin='pin-sha256:'+'A'*43+'='
private_ca=args.output/'private-ca.pem'
private_ca.write_bytes(args.certificate.read_bytes()+b'\n-----BEGIN PRIVATE KEY-----\nfixture\n-----END PRIVATE KEY-----\n')
for name,trust,edited,saved_pin,expected in [
    ('export_address','-','vpn.example.com:4443',None,True),
    ('export_ca','ca:'+str(args.certificate.resolve()),'https://vpn.example.com:4443',None,True),
    ('export_pin',pin,'https://vpn.example.com:4443',None,True),
    ('export_remembered_pin','-','https://vpn.example.com:4443',pin,True),
    ('export_other_host',pin,'https://other.example.com:4443',None,True),
    ('export_other_port','ca:'+str(args.certificate.resolve()),'https://vpn.example.com:5443',None,True),
    ('export_private_key','ca:'+str(private_ca.resolve()),'https://vpn.example.com:4443',None,False),
    ('export_bad_pin','pin-sha256:AAAA','https://vpn.example.com:4443',None,False),
]:
    directory=args.output/name
    directory.mkdir(exist_ok=True)
    origin='https://vpn.example.com:4443'
    if saved_pin:
        digest=hashlib.sha256(origin.encode()).hexdigest()
        (directory/f'trusted-{digest}.ini').write_text(f'[Server]\nOrigin={origin}\nPin={saved_pin}\n',encoding='utf-16')
    exported=directory/'exported.bvpn'
    exported.write_text('existing file must survive rejection',encoding='utf-16')
    before=exported.read_bytes()
    run=subprocess.run([str(args.client.resolve()),'--export-profile',origin,trust,edited,str(directory.resolve()),str(exported.resolve())],capture_output=True,text=True,timeout=10)
    passed=(run.returncode==0)==expected
    if expected and run.returncode==0:
        contents=exported.read_text(encoding='utf-16')
        assert 'Password=' not in contents and 'Username=' not in contents and 'PRIVATE KEY' not in contents and 'CAFile=' not in contents
        has_ca=name=='export_ca'
        has_pin=name in ('export_pin','export_remembered_pin')
        passed=passed and ('CABase64=' in contents)==has_ca and ('ServerPin='+pin in contents)==has_pin
        restored=directory/'connection.ini'
        imported=subprocess.run([str(args.client.resolve()),'--import-profile',str(exported.resolve()),str(restored.resolve())],capture_output=True,text=True,timeout=10)
        passed=passed and imported.returncode==0
        if imported.returncode==0:
            restored_text=restored.read_text(encoding='utf-16')
            passed=passed and 'PreferUDP=0' in restored_text and 'ReconnectSeconds=120' in restored_text
            passed=passed and ('AuthGroup=Fixture group' in restored_text)==(name not in ('export_other_host','export_other_port'))
            passed=passed and ('ServerPin='+pin in restored_text)==has_pin
            if has_ca:
                ca_name=next(line.removeprefix('CAFile=') for line in restored_text.splitlines() if line.startswith('CAFile='))
                passed=passed and ssl.PEM_cert_to_DER_cert((directory/ca_name).read_text())==ssl.PEM_cert_to_DER_cert(args.certificate.read_text())
    elif not expected:
        passed=passed and exported.read_bytes()==before
    result={'test':name,'passed':passed,'exit_code':run.returncode}
    results.append(result)
    print(json.dumps(result))
(args.output/'profile-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
raise SystemExit(0 if all(r['passed'] for r in results) else 1)

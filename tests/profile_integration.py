"""Import tests use an isolated output directory, never the user's live VPN profile."""
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
    ('missing_trust', '[VPN]\nServer=https://vpn.example.com\n', False),
    ('truncated_pin', '[VPN]\nServer=https://vpn.example.com\nServerPin=pin-sha256:AAAA\n', False),
    ('private_key', '[VPN]\nServer=https://vpn.example.com\nCABase64='+base64.b64encode(b'-----BEGIN PRIVATE KEY-----\nprivate\n-----END PRIVATE KEY-----').decode()+'\n', False),
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
    if expected:
        text=destination.read_text(encoding='utf-16')
        passed=passed and 'LockServer=1' in text and 'password' not in text.lower()
    else:
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
(args.output/'profile-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
raise SystemExit(0 if all(r['passed'] for r in results) else 1)

"""Opt-in VPN-only isolation acceptance using an already configured proxy."""
import argparse
import json
import os
from pathlib import Path
import secrets
import socket
import struct
import subprocess
import sys
import time
import urllib.parse

from router_api_tests import APIError, RouterAPI


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--username', default='root')
    parser.add_argument('--exercise', action='store_true')
    parser.add_argument('--unconfirmed-recovery', action='store_true', help='Also wait for the real confirmation deadline and automatic rollback')
    parser.add_argument('--output', type=Path, default=Path('test-results/router-guard.json'))
    args = parser.parse_args()
    password = os.environ.get('LANSYRA_ROUTER_PASSWORD')
    if not args.exercise or password is None:
        parser.error('--exercise and LANSYRA_ROUTER_PASSWORD are required')
    host = urllib.parse.urlsplit(args.url).hostname
    api = RouterAPI(args.url, args.username, password)
    before = api.data()
    if not before['guard']['available'] or before['guard']['phase'] != 'disabled' or not before['running']:
        raise RuntimeError('A running VPN and configured proxy must pass the isolation preflight')
    checks, errors = [], []

    def passed(label):
        checks.append(label)
        print('PASS ' + label, flush=True)

    def tcp(port):
        try:
            with socket.create_connection((host, port), timeout=2):
                return True
        except OSError:
            return False

    def dns():
        identity = secrets.randbelow(65536)
        query = struct.pack('!6H', identity, 0x100, 1, 0, 0, 0) + b'\x07example\x03com\0\0\x01\0\x01'
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.settimeout(2)
            client.sendto(query, (host, 53))
            try:
                reply = client.recv(4096)
                return len(reply) >= 12 and struct.unpack('!H', reply[:2])[0] == identity
            except OSError:
                return False

    def wait(phases, seconds=150):
        deadline, previous = time.monotonic() + seconds, None
        while time.monotonic() < deadline:
            data = api.data()
            state = data['guard']
            phase = state['phase']
            if phase != previous:
                print('Isolation phase: ' + phase, flush=True)
                previous = phase
            if phase in phases:
                return data
            if phase == 'recovery_failed':
                raise RuntimeError('Isolation recovery failed; inspect the router journal')
            time.sleep(1)
        raise TimeoutError('Isolation did not reach ' + ','.join(phases))

    proxy = api.api('proxy-status')
    redirect = proxy['targets']['lan_redirect']['ports'][0]['port']
    baseline_tcp, baseline_dns = tcp(redirect), dns()
    assert baseline_tcp and baseline_dns, 'The direct LAN proxy/DNS baseline must be reachable'
    try:
        api.action(action='guard', command='enable')
        state = wait({'pending', 'enabled'})['guard']
        if state['phase'] == 'pending':
            api.action(action='guard', command='confirm', token=state['token'])
        assert wait({'enabled'})['guard']['enabled']
        passed('isolation applies with the configured proxy and authenticated management confirmation')
        assert not tcp(redirect) and not dns()
        assert api.api('status')['running']
        passed('plain LAN proxy and DNS access are blocked while LuCI and VPN remain reachable')
        try:
            api.settings(before['settings'])
        except APIError as error:
            assert str(error) == 'guard_settings_locked'
        else:
            raise AssertionError('Network settings were writable during isolation')
        passed('network settings are locked while isolation is active')
        traffic = args.output.with_name(args.output.stem + '-vpn.json')
        subprocess.run([sys.executable, '-X', 'utf8', '-u', str(Path(__file__).with_name('router_vpn_tests.py')),
                        '--host', host, '--username', args.username, '--exercise', '--guard-enabled',
                        '--output', str(traffic)], check=True, timeout=180)
        passed('real VPN traffic and account/session revocation work during isolation')
        api.action(action='guard', command='disable')
        after = wait({'disabled'})
        assert after['settings'] == before['settings']
        assert tcp(redirect) == baseline_tcp and dns() == baseline_dns
        passed('disable restores service settings and ordinary LAN proxy/DNS access')
        if args.unconfirmed_recovery:
            api.action(action='guard', command='enable')
            pending = wait({'pending', 'enabled'})
            assert pending['guard']['phase'] == 'pending', 'Another management client confirmed the test'
            after = wait({'disabled'}, seconds=180)
            assert after['settings'] == before['settings']
            assert tcp(redirect) == baseline_tcp and dns() == baseline_dns
            passed('an unconfirmed management change automatically rolls back after its real deadline')
    except Exception as error:
        errors.append(type(error).__name__ + ': ' + str(error))
        raise
    finally:
        try:
            if api.data()['guard']['phase'] != 'disabled':
                wait({'pending', 'enabled', 'disabled'})
                api.action(action='guard', command='disable')
                wait({'disabled'})
            assert api.data()['settings'] == before['settings']
        except Exception as error:
            errors.append('Cleanup: ' + str(error))
            print(errors[-1], flush=True)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({'passed': len(checks), 'checks': checks, 'errors': errors,
            'proxy': proxy['proxy'], 'boundary': 'Real router, LAN rejection and isolated VPN client traffic.'},
            ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if errors:
        raise SystemExit(1)


if __name__ == '__main__':
    main()

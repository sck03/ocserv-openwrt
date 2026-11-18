"""Opt-in real VPN traffic test in an isolated OpenWrt network namespace."""
import argparse
import configparser
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import time

import paramiko
from router_api_tests import RouterAPI


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--username', default='root')
    parser.add_argument('--exercise', action='store_true')
    parser.add_argument('--guard-enabled', action='store_true', help='Test an already confirmed VPN-only isolation session')
    parser.add_argument('--output', type=Path, default=Path('test-results/router-vpn.json'))
    args = parser.parse_args()
    password = os.environ.get('LANSYRA_ROUTER_PASSWORD')
    if not args.exercise or password is None:
        parser.error('--exercise and LANSYRA_ROUTER_PASSWORD are required; use a disposable LAN router')
    ssh = paramiko.SSHClient()
    ssh.load_system_host_keys()
    # Testing an explicitly selected private router may precede its first SSH use.
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(args.host, username=args.username, password=password, look_for_keys=False, allow_agent=False, timeout=12)
    api = RouterAPI('http://' + args.host, args.username, password)
    original = api.data()
    expected_guard = 'enabled' if args.guard_enabled else 'disabled'
    if not original['running'] or original['guard']['phase'] != expected_guard:
        raise RuntimeError('VPN must be running and isolation must be ' + expected_guard)
    token = secrets.token_hex(3)
    ns, host_if, peer_if = 'lansyra-qa-' + token, 'lq' + token + 'h', 'lq' + token + 'p'
    account, secret = 'lansyra_vpn_' + token, secrets.token_urlsafe(24)
    marker = 'lansyra-qa-' + token
    work, user_id, created_ns, dns_directory = None, None, False, False
    gateway = str(ipaddress.IPv4Network((original['settings']['ipaddr'], original['settings']['netmask'])).network_address + 1)
    dns = str(ipaddress.ip_address(original['settings']['dns'][0]))
    checks, errors = [], []

    def command(text, input_text=None, timeout=35, check=True):
        stdin, stdout, stderr = ssh.exec_command(text, timeout=timeout)
        if input_text is not None:
            stdin.write(input_text)
            stdin.flush()
        stdin.channel.shutdown_write()
        output = stdout.read().decode('utf-8', 'replace') + stderr.read().decode('utf-8', 'replace')
        status = stdout.channel.recv_exit_status()
        if check and status:
            raise RuntimeError('Remote command failed (' + str(status) + '): ' + output[-1600:])
        return status, output

    def passed(label):
        checks.append(label)
        print('PASS ' + label, flush=True)

    def online():
        return [row for row in api.data()['online'] or [] if row['name'] == account]

    def stop_client():
        if work:
            command('if [ -s ' + shlex.quote(work + '/client.pid') + ' ]; then '
                    'pid=$(cat ' + shlex.quote(work + '/client.pid') + '); '
                    'case "$(readlink /proc/$pid/exe)" in */openconnect) kill -INT "$pid" 2>/dev/null || true;; esac; fi', check=False)

    def connect(dtls):
        stop_client()
        command('rm -f ' + shlex.quote(work + '/ready') + ' ' + shlex.quote(work + '/client.pid'))
        argv = ['ip', 'netns', 'exec', ns, 'openconnect', '--protocol=anyconnect', '--non-inter',
                '--passwd-on-stdin', '--user=' + account, '--servercert=' + pin,
                '--script=' + work + '/vpnc.sh', '--interface=lansyra-qa', '--background',
                '--pid-file=' + work + '/client.pid', '--no-proxy']
        if not dtls:
            argv.append('--no-dtls')
        argv.append('https://198.18.253.1:' + original['settings']['port'])
        command(shlex.join(argv) + ' >' + shlex.quote(work + '/client.log') + ' 2>&1', secret + '\n', timeout=40)
        for _ in range(15):
            if online():
                return
            time.sleep(0.2)
        _, log = command('tail -n 20 ' + shlex.quote(work + '/client.log'))
        raise RuntimeError('No visible VPN session: ' + log)

    try:
        command('command -v openconnect >/dev/null && ip netns list >/dev/null')
        status, routes = command('ip route show 198.18.253.0/30')
        if routes.strip():
            raise RuntimeError('The reserved test transport subnet is already in use')
        _, work = command('umask 077; mktemp -d /tmp/lansyra-vpn-test-XXXXXX')
        work = work.strip()
        script = '''#!/bin/sh
set -eu
case "$reason" in
    connect|reconnect)
        ip link set "$TUNDEV" up
        ip addr replace "$INTERNAL_IP4_ADDRESS/32" dev "$TUNDEV"
        ip route replace default dev "$TUNDEV"
        : > "WORK/ready"
        ;;
esac
'''.replace('WORK', work)
        sftp = ssh.open_sftp()
        with sftp.file(work + '/vpnc.sh', 'w') as destination:
            destination.write(script)
        sftp.chmod(work + '/vpnc.sh', 0o700)
        sftp.close()
        api.action(action='save_user', name=account, group='*', password=secret, enabled=True)
        user_id = next(row['id'] for row in api.data()['users'] if row['name'] == account)
        status, content = api.download('pin', 'https://' + args.host + ':' + original['settings']['port'])
        assert status == 200
        profile = configparser.ConfigParser(interpolation=None)
        profile.read_string(content.decode())
        pin = profile['VPN']['ServerPin']
        command(shlex.join(['ip', 'netns', 'add', ns]))
        created_ns = True
        command('mkdir -p /etc/netns && mkdir -m 700 ' + shlex.quote('/etc/netns/' + ns))
        dns_directory = True
        command('printf %s ' + shlex.quote('nameserver ' + dns + '\n') + ' >' + shlex.quote('/etc/netns/' + ns + '/resolv.conf'))
        setup = [
            ['ip', 'link', 'add', host_if, 'type', 'veth', 'peer', 'name', peer_if],
            ['ip', 'link', 'set', peer_if, 'netns', ns],
            ['ip', 'addr', 'add', '198.18.253.1/30', 'dev', host_if],
            ['ip', 'link', 'set', host_if, 'up'],
            ['ip', '-n', ns, 'link', 'set', 'lo', 'up'],
            ['ip', '-n', ns, 'addr', 'add', '198.18.253.2/30', 'dev', peer_if],
            ['ip', '-n', ns, 'link', 'set', peer_if, 'up'],
        ]
        for argv in setup:
            command(shlex.join(argv))
        command('nft insert rule inet fw4 input iifname "' + host_if + '" accept comment "' + marker + '"')
        passed('temporary client namespace and restricted transport are ready')
        connect(False)
        passed('real OpenConnect authentication and TLS tunnel appear in online sessions')
        prefix = ['ip', 'netns', 'exec', ns]
        command(shlex.join(prefix + ['ping', '-c', '3', '-W', '2', gateway]))
        passed('ICMP packets cross the encrypted tunnel to the VPN gateway')
        command(shlex.join(prefix + ['nslookup', 'example.com', dns]), timeout=25)
        _, http = command(shlex.join(prefix + ['curl', '-sS', '-o', '/dev/null', '-w', '%{http_code}',
                                                '--connect-timeout', '10', '--max-time', '20', 'https://example.com']), timeout=25)
        assert http.strip() == '200', http
        passed('DNS and public HTTPS traffic cross VPN forwarding and NAT')
        api.action(action='save_user', id=user_id, name=account, group='*', password='', enabled=False)
        assert not online()
        passed('disabling the account revokes its active tunnel')
        api.action(action='save_user', id=user_id, name=account, group='*', password='', enabled=True)
        connect(True)
        command(shlex.join(prefix + ['ping', '-c', '3', '-W', '2', gateway]))
        _, log = command('cat ' + shlex.quote(work + '/client.log'))
        assert 'DTLS' in log and ('Established DTLS' in log or 'DTLS connected' in log), log
        passed('re-enabled account reconnects with DTLS and exchanges packets')
        session = online()[0]
        api.action(action='disconnect', id=str(session['id']))
        assert not online()
        passed('disconnect-by-session removes the live VPN connection')
    except Exception as error:
        errors.append(type(error).__name__ + ': ' + str(error))
        raise
    finally:
        try:
            stop_client()
            if user_id:
                api.action(action='delete_user', id=user_id)
            _, rules = command('nft -a list chain inet fw4 input', check=False)
            for line in rules.splitlines():
                if 'comment "' + marker + '"' in line:
                    handle = re.search(r'# handle (\d+)', line)
                    if handle:
                        command('nft delete rule inet fw4 input handle ' + handle[1])
            if created_ns:
                command(shlex.join(['ip', 'link', 'del', host_if]), check=False)
                command(shlex.join(['ip', 'netns', 'delete', ns]))
            if dns_directory:
                command('rm -f ' + shlex.quote('/etc/netns/' + ns + '/resolv.conf') + ' && rmdir ' + shlex.quote('/etc/netns/' + ns))
            if work and re.fullmatch(r'/tmp/lansyra-vpn-test-[A-Za-z0-9]+', work):
                command('rm -rf -- ' + shlex.quote(work))
            passed('test account, client, namespace, firewall rule and temporary files removed')
        except Exception as error:
            errors.append('Cleanup: ' + str(error))
            print(errors[-1], flush=True)
        ssh.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({'passed': len(checks), 'checks': checks, 'errors': errors,
            'boundary': 'Real OpenWrt services and VPN packets; client runs in a separate router network namespace.'},
            ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if errors:
        raise SystemExit(1)


if __name__ == '__main__':
    main()

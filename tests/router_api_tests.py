"""Opt-in API acceptance on a disposable router; restores settings and removes its own accounts."""
import argparse
import configparser
import copy
import http.cookiejar
import json
import os
from pathlib import Path
import re
import secrets
import statistics
import time
import urllib.error
import urllib.parse
import urllib.request


class APIError(RuntimeError):
    pass


class RouterAPI:
    def __init__(self, url, username, password):
        self.url = url.rstrip('/')
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        self.request('/cgi-bin/luci/', {'luci_username': username, 'luci_password': password})
        _, page = self.request('/cgi-bin/luci/admin/vpn/ocserv')
        options = re.search(r'<script id="ocserv-easy-options"[^>]*>(.*?)</script>', page.decode(), re.S)
        if not options:
            raise RuntimeError('LuCI login or management page failed')
        self.options = json.loads(options[1])

    def request(self, path, values=None):
        body = urllib.parse.urlencode(values).encode() if values is not None else None
        try:
            response = self.opener.open(self.url + path, body, timeout=130 if body else 90 if path.endswith(('/guard-status', '/proxy-status')) else 30)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, response.read()

    def api(self, path, payload=None):
        values = None if payload is None else {
            'token': self.options['token'], 'payload': json.dumps(payload)}
        status, raw = self.request(self.options['base'] + '/' + path, values)
        value = json.loads(raw)
        if status != 200 or value.get('ok') is not True:
            raise APIError(value.get('error', 'http_' + str(status)))
        return value['data']

    def data(self):
        return self.api('data')

    def action(self, **payload):
        return self.api('action', dict(api_version=5, revision=self.data()['revision'], **payload))

    def settings(self, values):
        values = copy.deepcopy(values)
        values['routes'] = values['routes'] or []
        return self.action(action='settings', settings=values, allow_restart=True)

    def download(self, kind, server, platform='windows'):
        return self.request(self.options['base'] + '/download', dict(
            token=self.options['token'], kind=kind, server=server, platform=platform))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--username', default='root')
    parser.add_argument('--exercise', action='store_true', help='Permit account/settings changes and VPN restarts')
    parser.add_argument('--output', type=Path, default=Path('test-results/router-api.json'))
    args = parser.parse_args()
    if not args.exercise:
        parser.error('--exercise is required: use a non-production router over LAN')
    password = os.environ.get('LANSYRA_ROUTER_PASSWORD')
    if password is None:
        parser.error('Set LANSYRA_ROUTER_PASSWORD without putting credentials in command arguments')
    api = RouterAPI(args.url, args.username, password)
    baseline = api.data()
    if baseline['guard']['phase'] != 'disabled' or baseline['recovery_pending']:
        raise RuntimeError('Finish existing maintenance or isolation before acceptance tests')
    settings = copy.deepcopy(baseline['settings'])
    host = urllib.parse.urlsplit(args.url).hostname
    authority = '[' + host + ']' if ':' in host else host
    server = f'https://{authority}:{settings["port"]}'
    name = 'lansyra_qa_' + secrets.token_hex(4)
    owned = {name, name + '_renamed'}
    secret = secrets.token_urlsafe(24)
    checks, errors = [], []

    def passed(label):
        checks.append(label)
        print('PASS ' + label, flush=True)

    def reject(expected, action):
        try:
            action()
        except APIError as error:
            assert str(error) == expected, str(error)
        else:
            raise AssertionError('Expected ' + expected)

    def find_user():
        return next(user for user in (api.data()['users'] or []) if user['name'] in owned)

    try:
        assert baseline['api_version'] == 5 and baseline['supported']
        trust = api.api('client-status')
        assert trust['api_version'] == 5 and trust['revision'] == baseline['revision']
        assert trust['certificates_ready'] and not baseline['installation_pending']
        passed('installed versions, certificate readiness and API contract')
        reject('interface_mismatch', lambda: api.api('action', {'api_version': 1, 'action': 'service'}))
        reject('stale_revision', lambda: api.api('action', {
            'api_version': 5, 'action': 'service', 'command': 'start', 'revision': 'stale'}))
        status, _ = api.request(api.options['base'] + '/action', {
            'token': 'invalid', 'payload': json.dumps({'api_version': 5, 'action': 'service'})})
        assert status == 403, status
        passed('API version, stale revision and CSRF rejection')

        api.action(action='save_user', name=name, group='*', password=secret, enabled=True)
        user = find_user()
        assert user['enabled'] and 'password' not in user and secret not in json.dumps(user)
        passed('create account without exposing password or hash')
        result = api.action(action='save_user', id=user['id'], name=name, group='*', password='', enabled=True)
        assert result['effect'] == 'unchanged'
        reject('duplicate_user', lambda: api.action(action='save_user', name=name, group='*', password=secret, enabled=True))
        passed('unchanged account is a no-op; duplicate account is rejected')
        for enabled in (False, True):
            api.action(action='save_user', id=user['id'], name=name, group='*', password='', enabled=enabled)
            assert find_user()['enabled'] == enabled
        api.action(action='save_user', id=user['id'], name=name + '_renamed', group='qa', password=secrets.token_urlsafe(24), enabled=True)
        assert find_user()['name'] == name + '_renamed' and find_user()['group'] == 'qa'
        passed('disable, enable, rename, group and password changes')

        for command, expected in (('stop', False), ('start', True), ('restart', True)):
            api.action(action='service', command=command, allow_restart=True)
            assert api.api('status')['running'] == expected
        for command, expected in (('disable', False), ('enable', True)):
            api.action(action='service', command=command)
            assert api.data()['autostart'] == expected
        passed('stop, start, restart and boot enable/disable')

        result = api.settings(settings)
        assert result['effect'] == 'unchanged', result
        invalid = dict(settings, port='0')
        reject('invalid_port', lambda: api.settings(invalid))
        reject('bad_request', lambda: api.api('action', {
            'api_version': 5, 'action': 'settings', 'revision': api.data()['revision'],
            'settings': dict(settings, dns=['1.1.1.1', None])}))
        passed('unchanged settings avoid writes; invalid port and JSON null are rejected')
        changed = dict(settings, port='4444' if settings['port'] != '4444' else '4445',
                       max_same='2', max_clients='33', udp='0')
        assert api.settings(changed)['effect'] == 'restarted'
        actual = api.data()['settings']
        assert all(actual[key] == changed[key] for key in ('port', 'max_same', 'max_clients', 'udp'))
        api.settings(settings)
        passed('listener, UDP and client limits apply and restore with service restart')
        split = dict(settings, route_mode='split', routes=['192.168.19.0/24', '192.168.19.0/255.255.255.0'],
                     dns=['1.1.1.1', '8.8.8.8'])
        api.settings(split)
        assert len(api.data()['settings']['routes']) == 1
        api.settings(settings)
        passed('DNS, split routing and route deduplication')
        reject('config_check_failed', lambda: api.settings(dict(settings, server_cert='/tmp/lansyra-missing-qa.pem')))
        assert api.api('status')['running'] and not api.data()['recovery_pending']
        passed('missing certificate rejects candidate and preserves running service')

        pins = []
        for target in baseline['client_targets']:
            for mode in target['modes']:
                status, raw = api.download(mode, server, target['id'])
                assert status == 200, (target['id'], mode, status)
                profile = configparser.ConfigParser(interpolation=None)
                profile.read_string(raw.decode())
                assert profile['VPN']['Version'] == '1' and profile['VPN']['Protocol'] == 'anyconnect'
                assert 'PRIVATE KEY' not in raw.decode() and secret not in raw.decode()
                if mode == 'pin': pins.append(profile['VPN']['ServerPin'])
        assert len(set(pins)) == 1
        assert api.download('pin', server, 'linux')[0] == 400
        assert api.download('profile', server, 'ios')[0] == 400
        status, ca = api.download('ca', server)
        assert status == 200 and b'BEGIN CERTIFICATE' in ca and b'PRIVATE KEY' not in ca
        api.action(action='initcerts')
        assert pins[0].encode() in api.download('pin', server)[1]
        passed('all five client targets, trust modes, public CA and idempotent certificate initialization')

        api.action(action='client_profile', server=server, cert_mode='pin', platform='windows')
        assert api.data()['client_profile'] == {'cert_mode': 'pin', 'platform': 'windows'}
        reject('invalid_settings', lambda: api.settings(dict(settings, split_dns='1', default_domain='qa.invalid')))
        assert api.download('pin', server, 'android')[0] == 200
        assert api.download('pin', server, 'windows')[0] == 200
        api.settings(settings)
        passed('saved export settings and removed split-DNS field rejection')

        if not baseline['guard']['available'] and baseline['guard'].get('reason') == 'guard_requires_proxy':
            reject('guard_requires_proxy', lambda: api.action(action='guard', command='enable'))
            reject('guard_requires_proxy', lambda: api.api('proxy-status'))
            assert api.data()['guard']['phase'] == 'disabled'
            passed('unconfigured proxy blocks isolation without changing network state')
        assert 'entries' in api.api('guard-log')
        samples = []
        for _ in range(20):
            start = time.perf_counter()
            assert api.api('status')['running']
            samples.append(round((time.perf_counter() - start) * 1000, 2))
        passed('20 consecutive status requests return a running service')
    except Exception as error:
        errors.append(type(error).__name__ + ': ' + str(error))
        raise
    finally:
        try:
            for user in api.data()['users'] or []:
                if user['name'] in owned:
                    api.action(action='delete_user', id=user['id'])
            profile = baseline['client_profile']
            if api.data()['client_profile'] != profile:
                api.action(action='client_profile', server=settings['easy_public_url'] or server,
                           cert_mode=profile['cert_mode'], platform=profile['platform'])
            if api.data()['settings'] != settings:
                api.settings(settings)
            for field, yes, no in (('running', 'start', 'stop'), ('autostart', 'enable', 'disable')):
                if api.data()[field] != baseline[field]:
                    api.action(action='service', command=yes if baseline[field] else no, allow_restart=True)
            passed('owned test accounts removed; original settings and service state restored')
        except Exception as error:
            errors.append('Cleanup: ' + type(error).__name__ + ': ' + str(error))
            print(errors[-1], flush=True)
        report = {'passed': len(checks), 'checks': checks, 'errors': errors,
                  'boundary': 'Real LuCI HTTP API and installed router services; no browser automation or VPN client traffic.'}
        if 'samples' in locals():
            report['status_latency_ms'] = {'median': statistics.median(samples), 'max': max(samples), 'samples': samples}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if errors:
        raise SystemExit(1)


if __name__ == '__main__':
    main()

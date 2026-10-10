// Execute the shipped JScript in a fake Windows Script Host. No commands run on the host.
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../vendor/vpnc-script-win.js'), 'utf8');
let passed = 0;
function execute(overrides = {}, failure = () => 0, storage = { text: '' }, routes = {}) {
    const stateFiles = storage.stateFiles ??= new Map();
    const environment = {
        reason: 'connect', TUNIDX: '42', TUNDEV: 'Synthetic VPN', VPNGATEWAY: '203.0.113.7',
        INTERNAL_IP4_ADDRESS: '198.18.0.2', INTERNAL_IP4_NETMASK: '255.255.255.0',
        INTERNAL_IP4_MTU: '1400', INTERNAL_IP4_DNS: '198.18.0.1',
        CISCO_SPLIT_INC: '1', CISCO_SPLIT_INC_0_ADDR: '198.18.0.0',
        CISCO_SPLIT_INC_0_MASK: '255.255.255.0', CISCO_SPLIT_INC_0_MASKLEN: '24', ...overrides
    };
    const commands = [], logs = [], files = [];
    let exitCode;
    const shell = {
        Environment: () => name => environment[name] || '',
        ExpandEnvironmentStrings: () => 'C:\\Windows\\System32\\cmd.exe',
        Exec(command) {
            commands.push(command);
            return {
                StdIn: { Close() {} }, ExitCode: failure(command),
                StdOut: { ReadAll: () => command.includes('route print')
                    ? (routes.ipv4 ?? '0.0.0.0 0.0.0.0 192.0.2.1 192.0.2.2 25\n')
                    : command.includes('ipv6 show route') ? (routes.ipv6 ?? '::/0 12 fe80::1\n') : '' }
            };
        }
    };
    const fileSystem = {
        GetSpecialFolder: () => 'C:\\Temp',
        FileExists: file => file.endsWith('.routes') ? stateFiles.has(file) : true,
        GetFile: file => ({ Size: file.endsWith('.routes') ? (stateFiles.get(file) || '').length : storage.text.length * 2 }),
        DeleteFile: file => stateFiles.delete(file),
        OpenTextFile(...args) {
            files.push(args);
            if (args[0].endsWith('.routes')) {
                if (args[1] === 2) stateFiles.set(args[0], '');
                return { ReadAll: () => stateFiles.get(args[0]), Close() {},
                    WriteLine: text => stateFiles.set(args[0], stateFiles.get(args[0]) + text + '\r\n') };
            }
            return { WriteLine(text) { logs.push(text); storage.text += text + '\r\n'; }, Close() {} };
        }
    };
    vm.runInNewContext(source, { WScript: {
        CreateObject: name => name === 'WScript.Shell' ? shell : fileSystem,
        echo: text => logs.push(text), Quit: code => { exitCode = code; }
    } }, { timeout: 2000 });
    return { exitCode, commands, logs, files };
}
function test(name, check) {
    check(); ++passed;
    console.log('PASS ' + name);
}
test('connect configures address, routes and DNS without changing the default route', () => {
    const result = execute();
    assert.equal(result.exitCode, 0);
    assert(result.commands.some(c => c.includes('route add 198.18.0.0 mask 255.255.255.0')));
    assert(result.commands.some(c => c.includes('add dnsservers 42 198.18.0.1 validate=no')));
    assert(!result.commands.some(c => c.includes('gwmetric=1') || c.includes('route add 0.0.0.0')));
});
test('empty DNS and WINS cleanup is nonfatal', () => {
    assert.equal(execute({}, c => /delete (dnsservers|winsservers)/.test(c) ? 1 : 0).exitCode, 0);
});
test('an unavailable IPv6 stack does not break an IPv4 connection', () => {
    assert.equal(execute({}, c => c.includes('ipv6 show route') ? 1 : 0).exitCode, 0);
});
test('loopback gateways never change a host route', () => {
    for (const reason of ['connect', 'disconnect']) {
        const result = execute({ reason, VPNGATEWAY: '127.0.0.1' });
        assert(!result.commands.some(c => /route (add|delete) 127\./.test(c)));
    }
});
for (const [name, failedCommand] of [
    ['address', 'set address'], ['DNS', 'add dnsservers'], ['MTU', 'set subinterface'],
    ['route', 'route add 198.18.0.0']
]) {
    test(name + ' failure survives later successful commands', () => {
        assert.equal(execute({}, c => c.includes(failedCommand) ? 1 : 0).exitCode, 1);
    });
}
test('large exit codes cannot wrap into success', () => {
    assert.equal(execute({}, c => c.includes('route add') ? 2147483648 : 0).exitCode, 1);
});
test('pre-init and attempt-reconnect notifications do not reapply routes', () => {
    for (const reason of ['pre-init', 'attempt-reconnect']) {
        const result = execute({ reason });
        assert.equal(result.exitCode, 0);
        assert(!result.commands.some(c => /netsh|route (add|delete)/.test(c)));
    }
});
test('disconnect releases the gateway route and tunnel address', () => {
    const storage = { text: '' };
    execute({}, () => 0, storage);
    const result = execute({ reason: 'disconnect' }, () => 0, storage);
    assert.equal(result.exitCode, 0);
    assert(result.commands.some(c => c.includes('route delete 203.0.113.7')));
    assert(result.commands.some(c => c.includes('delete address 42 198.18.0.2')));
});
test('IPv6 DNS uses the IPv6 netsh command', () => {
    const result = execute({ INTERNAL_IP4_DNS: '198.18.0.1 2001:db8::53' });
    assert(result.commands.some(c => c.includes('ipv6 add dnsservers 42 2001:db8::53 validate=no')));
});
test('an IPv6-only uplink carries the IPv4 tunnel without invalid IPv4 exclusions', () => {
    const storage = { text: '' };
    for (const reason of ['connect', 'disconnect']) {
        const result = execute({ reason, VPNGATEWAY: '2001:db8::7',
            CISCO_SPLIT_EXC: '1', CISCO_SPLIT_EXC_0_ADDR: '192.168.19.0',
            CISCO_SPLIT_EXC_0_MASK: '255.255.255.0', CISCO_SPLIT_EXC_0_MASKLEN: '24'
        }, () => 0, storage, { ipv4: '' });
        assert.equal(result.exitCode, 0);
        assert(result.commands.some(c => c.includes('2001:db8::7/128 12 fe80::1')));
        assert(!result.commands.some(c => /route (add|delete) 192\.168\.19\.0/.test(c)));
    }
});
test('DDNS prepares each new address before reconnect and cleans obsolete routes', () => {
    const storage = { text: '' };
    execute({}, () => 0, storage);
    const changed = { ipv4: '0.0.0.0 0.0.0.0 198.18.0.2 198.18.0.2 1\n0.0.0.0 0.0.0.0 192.0.2.9 192.0.2.10 30\n' };
    const prepare = execute({ reason: 'prepare-connect', VPNGATEWAY: '203.0.113.9' }, () => 0, storage, changed);
    assert.equal(prepare.exitCode, 0);
    assert(prepare.commands.some(c => c.includes('route add 203.0.113.9 mask 255.255.255.255 192.0.2.9')));
    assert(!prepare.commands.some(c => /set address|dnsservers/.test(c)));
    const connected = execute({ reason: 'reconnect', VPNGATEWAY: '203.0.113.9' }, () => 0, storage, changed);
    assert.equal(connected.exitCode, 0);
    assert(connected.commands.some(c => c.includes('route delete 203.0.113.7 mask 255.255.255.255 192.0.2.1')));
    assert(!connected.commands.some(c => /set address|dnsservers/.test(c)));
    const disconnected = execute({ reason: 'disconnect', VPNGATEWAY: '203.0.113.9' }, () => 0, storage, { ipv4: '', ipv6: '' });
    assert(disconnected.commands.some(c => c.includes('route delete 203.0.113.9 mask 255.255.255.255 192.0.2.9')));
    assert.equal(storage.stateFiles.size, 0);
});
test('IPv6 routes are temporary and cleanup uses the saved interface after a network change', () => {
    const storage = { text: '' };
    const options = { VPNGATEWAY: '2001:db8::7', INTERNAL_IP6_ADDRESS: 'fd77::2',
        CISCO_IPV6_SPLIT_EXC: '1', CISCO_IPV6_SPLIT_EXC_0_ADDR: 'fd19::', CISCO_IPV6_SPLIT_EXC_0_MASKLEN: '64' };
    const connected = execute(options, () => 0, storage);
    assert.equal(connected.exitCode, 0);
    for (const route of ['2001:db8::7/128 12 fe80::1', 'fd19::/64 12 fe80::1', '::/1 42', '8000::/1 42'])
        assert(connected.commands.some(c => c.includes('ipv6 add route ' + route + ' store=active')));
    const disconnected = execute({ ...options, reason: 'disconnect' }, () => 0, storage, { ipv6: '::/0 25 fe80::9\n' });
    assert(disconnected.commands.some(c => c.includes('ipv6 delete route 2001:db8::7/128 12 fe80::1 store=active')));
    assert(disconnected.commands.some(c => c.includes('ipv6 delete route fd19::/64 12 fe80::1 store=active')));
    assert(!disconnected.commands.some(c => c.includes('25 fe80::9')));
});
test('pre-existing bypass routes remain untouched, including after configuration failure', () => {
    const storage = { text: '' };
    const routes = { ipv4: '0.0.0.0 0.0.0.0 192.0.2.1 192.0.2.2 25\n203.0.113.7 255.255.255.255 192.0.2.1 192.0.2.2 1\n' };
    const connected = execute({}, c => c.includes('set address') ? 1 : 0, storage, routes);
    assert.equal(connected.exitCode, 1);
    assert(!connected.commands.some(c => /route (add|delete) 203\.0\.113\.7/.test(c)));
    const disconnected = execute({ reason: 'disconnect' }, () => 0, storage, routes);
    assert(!disconnected.commands.some(c => c.includes('route delete 203.0.113.7')));
});
test('failed connection setup rolls back the external routes it created', () => {
    const storage = { text: '' };
    const result = execute({}, c => c.includes('set address') ? 1 : 0, storage);
    assert.equal(result.exitCode, 1);
    assert(result.commands.some(c => c.includes('route delete 203.0.113.7 mask 255.255.255.255 192.0.2.1')));
    assert.equal(storage.stateFiles.size, 0);
});
test('session logs use UTF-16 and the application-selected path', () => {
    const result = execute({ BULIJIE_SCRIPT_LOG: 'C:\\Temp\\中文-session.log' });
    assert.deepEqual(result.files[0], ['C:\\Temp\\中文-session.log', 8, true, -1]);
    assert(result.logs.length > 0);
});
test('error-only logging stays at level zero', () => {
    const result = execute({ LOG_LEVEL: '0' });
    assert.equal(result.logs.length, 0);
});
test('repeated script invocations share one bounded log budget', () => {
    const storage = { text: 'x'.repeat(1024 * 1024 - 256) };
    for (let i = 0; i < 1000; ++i) {
        const result = execute({ BULIJIE_SCRIPT_LOG: 'C:\\Temp\\session.log' }, () => 0, storage);
        assert.equal(result.exitCode, 0);
        assert(storage.text.length <= 1024 * 1024);
    }
    // The client's drain resets the budget; new diagnostics must be retained.
    storage.text = '';
    execute({ BULIJIE_SCRIPT_LOG: 'C:\\Temp\\session.log' }, () => 0, storage);
    assert(storage.text.length > 0);
});
test('huge banner and script exception cannot exceed the log budget', () => {
    const storage = { text: '' };
    const result = execute({ BULIJIE_SCRIPT_LOG: 'C:\\Temp\\session.log', CISCO_BANNER: 'x'.repeat(2 * 1024 * 1024) },
        command => { if (command.includes('route print')) throw new Error('fixture'); return 0; }, storage);
    assert.equal(result.exitCode, 1);
    assert(storage.text.length <= 1024 * 1024);
});
test('a Windows Script Host exception is explicitly reported as failure', () => {
    const result = execute({}, () => { throw new Error('Synthetic WSH error'); });
    assert.equal(result.exitCode, 1);
    assert(result.logs.some(line => line.includes('Synthetic WSH error')));
});
console.log(JSON.stringify({ passed, boundary: 'Shipped JScript with mocked Windows commands; no host network changes' }));

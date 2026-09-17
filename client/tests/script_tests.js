// Execute the shipped JScript in a fake Windows Script Host. No commands run on the host.
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../vendor/vpnc-script-win.js'), 'utf8');
let passed = 0;
function execute(overrides = {}, failure = () => 0) {
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
                    ? '0.0.0.0 0.0.0.0 192.0.2.1 192.0.2.2 25\n'
                    : command.includes('ipv6 show route') ? '::/0 12 fe80::1\n' : '' }
            };
        }
    };
    const fileSystem = {
        GetSpecialFolder: () => 'C:\\Temp',
        OpenTextFile(...args) { files.push(args); return { WriteLine: text => logs.push(text), Close() {} }; }
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
test('pre-init and reconnect notifications do not reapply routes', () => {
    for (const reason of ['pre-init', 'attempt-reconnect', 'reconnect']) {
        const result = execute({ reason });
        assert.equal(result.exitCode, 0);
        assert(!result.commands.some(c => /netsh|route (add|delete)/.test(c)));
    }
});
test('disconnect releases the gateway route and tunnel address', () => {
    const result = execute({ reason: 'disconnect' });
    assert.equal(result.exitCode, 0);
    assert(result.commands.some(c => c.includes('route delete 203.0.113.7')));
    assert(result.commands.some(c => c.includes('delete address 42 198.18.0.2')));
});
test('IPv6 DNS uses the IPv6 netsh command', () => {
    const result = execute({ INTERNAL_IP4_DNS: '198.18.0.1 2001:db8::53' });
    assert(result.commands.some(c => c.includes('ipv6 add dnsservers 42 2001:db8::53 validate=no')));
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
console.log(JSON.stringify({ passed, boundary: 'Shipped JScript with mocked Windows commands; no host network changes' }));

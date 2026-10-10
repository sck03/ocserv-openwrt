// vpnc-script-win.js
//
// Originally part of vpnc source code:
// © 2007-2008 Maurice Massar, Jörg Mayer, Antonio Borneo, et al.
// © 2014 David Woodhouse <dwmw2@infradead.org>
// © 2020-2022 Daniel Lenski <dlenski@gmail.com> et al.
//
// Routing, IP, and DNS configuration script for OpenConnect.
//
// Microsoft's "JScript" is what we're actually using here.  It's
// based on a truly ancient version of JavaScript (ECMAScript 3.0
// according to a Microsoft engineer, see
// https://stackoverflow.com/a/28331933) so it doesn't include any
// modern features:
//   - no String.prototype.trim       (ECMAScript 5.0)
//   - no Date.prototype.toISOString  (ECMAScript 5.1)
//   - no 'const'                     (ECMAScript 6.0)

// --------------------------------------------------------------
// Initial setup
// --------------------------------------------------------------

// WSH can exit with status 0 after an unhandled JScript exception.
// Keep the upstream helper inside an explicit failure boundary.
var failureLog = null;
var failureCleanup = null;
function configureNetwork() {
var accumulatedExitCode = 0;
var lastExitCode = 0;
var ws = WScript.CreateObject("WScript.Shell");
var env = ws.Environment("Process");
var comspec = ws.ExpandEnvironmentStrings("%comspec%");
var fs = WScript.CreateObject("Scripting.FileSystemObject");

var ERROR = 0, INFO = 1, DEBUG = 2, TRACE = 3;
var logLevel = env("LOG_LEVEL") === "" ? INFO : parseInt(env("LOG_LEVEL"));
if (isNaN(logLevel)) logLevel = INFO;
// BulijieVPN: isolate each session's log and preserve Chinese Windows output.
var applicationLogPath = env("BULIJIE_SCRIPT_LOG");
var logToFile = applicationLogPath || env("LOG2FILE");
var loggedCharacters = 0;
var statePath = applicationLogPath ? applicationLogPath + ".routes" :
    fs.GetSpecialFolder(2) + "\\vpnc-" + env("VPNPID") + ".routes";
var ownedRoutes = null, routes4 = "", routes6 = "";

// --------------------------------------------------------------
// Utilities
// --------------------------------------------------------------

function echo(level, msg)
{
    if (logLevel < level)
        return;

    if (logToFile) {
        var remaining = 1024 * 1024 - loggedCharacters - 2;
        if (remaining >= 0) {
            var written = msg.substring(0, remaining);
            log.WriteLine(written);
            loggedCharacters += written.length + 2;
        }
    } else {
        WScript.echo(msg);
    }
}

function run(cmd, optional)
{
    var fullCmd = comspec + " /C \"" + cmd + "\" 2>&1";
    echo(DEBUG, "-> " + fullCmd);
    var oExec = ws.Exec(fullCmd);
    oExec.StdIn.Close();

    var s = oExec.StdOut.ReadAll();
    while (oExec.Status === 0) WScript.Sleep(10);

    var exitCode = lastExitCode = oExec.ExitCode;
    if (exitCode != 0) {
        echo(optional ? INFO : ERROR, "\"" + cmd + "\" returned non-zero exit status: " + exitCode);
        // netsh returns an error when deleting an already empty DNS/WINS list.
        // Required configuration failures must remain failures even if later commands succeed.
        if (!optional) accumulatedExitCode = 1;
    }
    echo((exitCode != 0 ? (optional ? INFO : ERROR) : TRACE), "   stdout+stderr dump: " + s);

    return s;
}

function getDefaultGateway4()
{
    routes4 = run("route print", true);
    var rows = routes4.split(/\r?\n/), gateway = "", metric = Infinity;
    for (var i = 0; i < rows.length; i++) {
        var match = rows[i].match(/^\s*0\.0\.0\.0\s+0\.0\.0\.0\s+([0-9.]+)\s+\S+\s+(\d+)/);
        if (match && match[1] !== env("INTERNAL_IP4_ADDRESS") && Number(match[2]) < metric) {
            gateway = match[1]; metric = Number(match[2]);
        }
    }
    return gateway;
}

function getDefaultGateway6()
{
    routes6 = run("netsh interface ipv6 show route", true);
    var rows = routes6.split(/\r?\n/), gateway = "", metric = Infinity;
    for (var i = 0; i < rows.length; i++) {
        var match = rows[i].match(/::\/0\s+(\d+)\s+([0-9a-f:]+)/i);
        var cost = match && rows[i].substring(0, match.index).match(/(\d+)\s*$/);
        var value = cost ? Number(cost[1]) : 0;
        if (match && match[1] !== env("TUNIDX") && value < metric) {
            gateway = match[1] + " " + match[2]; metric = value;
        }
    }
    return gateway;
}

function isLoopback(address)
{
    return /^127\./.test(address) || address === "::1";
}

function routeCommand(action, route) {
    if (route[0] === "6")
        return "netsh interface ipv6 " + action + " route " + route[1] + " " + route[2] + " store=active";
    var parts = route[1].split("/");
    return "route " + action + " " + parts[0] + " mask " + parts[1] + " " + route[2];
}
function loadRoutes() {
    if (ownedRoutes !== null) return;
    ownedRoutes = [];
    if (!fs.FileExists(statePath)) return;
    if (fs.GetFile(statePath).Size > 32768) throw new Error("Invalid route state size");
    var file = fs.OpenTextFile(statePath, 1, false, 0);
    var rows = file.ReadAll().split(/\r?\n/); file.Close();
    for (var i = 0; i < rows.length; i++) {
        if (!rows[i]) continue;
        var r = rows[i].split("\t");
        if (r.length !== 4 || !/^[ge]$/.test(r[3]) ||
            !(r[0] === "4" && /^[0-9.]+\/[0-9.]+$/.test(r[1]) && /^[0-9.]+$/.test(r[2]) ||
              r[0] === "6" && /^[0-9a-f:]+\/\d+$/i.test(r[1]) && /^\d+ [0-9a-f:]+$/i.test(r[2])))
            throw new Error("Invalid route state");
        ownedRoutes.push(r);
    }
}
function saveRoutes() {
    var file = fs.OpenTextFile(statePath, 2, true, 0);
    for (var i = 0; i < ownedRoutes.length; i++) file.WriteLine(ownedRoutes[i].join("\t"));
    file.Close();
}
function addOwnedRoute(family, prefix, gateway, kind) {
    if (!gateway) return; // An on-link/specific route can work without a default gateway.
    loadRoutes();
    var r = [family, prefix, gateway, kind];
    var pattern = family === "4" ? prefix.replace("/", "\\s+") : prefix;
    pattern = pattern.replace(/\./g, "\\.") + "\\s+" + gateway.replace(/ /g, "\\s+").replace(/\./g, "\\.");
    if (new RegExp("(?:^|\\s)" + pattern + "(?:\\s|$)", "i").test(family === "4" ? routes4 : routes6)) return;
    if (ownedRoutes.length >= 256) throw new Error("Too many pending VPN routes");
    run(routeCommand("add", r));
    if (lastExitCode) return;
    var known = false;
    for (var i = 0; i < ownedRoutes.length; i++)
        if (ownedRoutes[i].join("\t") === r.join("\t")) known = true;
    if (!known) ownedRoutes.push(r);
    try { saveRoutes(); } catch (error) { run(routeCommand("delete", r), true); throw error; }
}
function releaseRoutes(keep) {
    loadRoutes();
    var remaining = [];
    for (var i = 0; i < ownedRoutes.length; i++) {
        var r = ownedRoutes[i];
        if (keep && keep(r)) remaining.push(r);
        else run(routeCommand("delete", r), true);
    }
    ownedRoutes = remaining;
    if (ownedRoutes.length) saveRoutes();
    else if (fs.FileExists(statePath)) fs.DeleteFile(statePath);
}
function gatewayRoute(gateway, gw4, gw6) {
    if (!gateway || isLoopback(gateway)) return;
    var ipv6 = gateway.indexOf(":") !== -1;
    addOwnedRoute(ipv6 ? "6" : "4", gateway + (ipv6 ? "/128" : "/255.255.255.255"), ipv6 ? gw6 : gw4, "g");
}
function excludedRoutes(gw4, gw6) {
    for (var family = 4; family <= 6; family += 2) {
        var prefix = family === 4 ? "CISCO_SPLIT_EXC" : "CISCO_IPV6_SPLIT_EXC";
        for (var i = 0; i < Number(env(prefix)); i++)
            addOwnedRoute(String(family), env(prefix + "_" + i + "_ADDR") + "/" +
                env(prefix + "_" + i + (family === 4 ? "_MASK" : "_MASKLEN")), family === 4 ? gw4 : gw6, "e");
    }
}
failureCleanup = function() { releaseRoutes(); };

// --------------------------------------------------------------
// Script starts here
// --------------------------------------------------------------

if (logToFile) {
	var tmpdir = fs.GetSpecialFolder(2)+"\\";
	// Include earlier reconnect attempts in the limit. The client drains this
	// file after setup/reconnect/cleanup; failed attempts can run in between.
	if (applicationLogPath && fs.FileExists(applicationLogPath))
		loggedCharacters = Math.max(1, Math.ceil(fs.GetFile(applicationLogPath).Size / 2));
	else if (applicationLogPath) loggedCharacters = 1; // UTF-16 BOM
	var log = fs.OpenTextFile(applicationLogPath || tmpdir + "vpnc.log", 8, true, applicationLogPath ? -1 : 0);
	failureLog = { WriteLine: function(message) { echo(ERROR, message); }, Close: function() { log.Close(); } };
}

switch (env("reason")) {
case "pre-init":
    break;
case "prepare-connect":
case "reconnect":
    var gw4 = getDefaultGateway4(), gw6 = getDefaultGateway6();
    gatewayRoute(env("VPNGATEWAY"), gw4, gw6);
    if (env("reason") === "reconnect") {
        excludedRoutes(gw4, gw6);
        releaseRoutes(function(r) {
            return r[2] === (r[0] === "4" ? gw4 : gw6) &&
                (r[3] === "e" || r[1].split("/")[0] === env("VPNGATEWAY"));
        });
    }
    break;
case "connect":
    if (env("CISCO_BANNER")) {
        echo(INFO, "--------------------- BANNER ---------------------");
        echo(INFO, env("CISCO_BANNER"));
        echo(INFO, "------------------- BANNER end -------------------");
    }

    var gw4 = getDefaultGateway4();
    var gw6 = getDefaultGateway6();

    // Use INTERNAL_IP4_ADDRESS as the "gateway" address for the
    // VPN tunnel connection. As noted in the OpenConnect source,
    // "It's a tunnel; having a gateway is meaningless." Setting
    // the gateway to match the INTERNAL_IP4_ADDRESS seems like
    // the simplest way to behave correctly in all cases,
    // including when the INTERNAL_IP4_NETMASK is /0 or /32.
    var internal_ip4_netmask = env("INTERNAL_IP4_NETMASK") || "255.255.255.255";
    var internal_gw = env("INTERNAL_IP4_ADDRESS");

    echo(INFO, "Legacy IP Internet gateway: " + gw4);
    echo(INFO, "IPv6 Internet gateway     : " + gw6);
    echo(INFO, "VPN Interface Identifiers : \"" + env("TUNDEV") + "\" / " + env("TUNIDX"));
    echo(INFO, "Public VPN Gateway Address: " + env("VPNGATEWAY"));
    echo(INFO, "Internal Legacy IP Address: " + env("INTERNAL_IP4_ADDRESS"));
    echo(INFO, "Internal Legacy IP Netmask: " + internal_ip4_netmask);


    if (env("INTERNAL_IP4_MTU")) {
        echo(INFO, "MTU: " + env("INTERNAL_IP4_MTU"));
        run("netsh interface ipv4 set subinterface " + env("TUNIDX") +
            " mtu=" + env("INTERNAL_IP4_MTU") + " store=active");

        if (env("INTERNAL_IP6_ADDRESS")) {
            run("netsh interface ipv6 set subinterface " + env("TUNIDX") +
                " mtu=" + env("INTERNAL_IP4_MTU") + " store=active");
        }
    }

    // Add explicit route for the VPN gateway to avoid routing loops
    gatewayRoute(env("VPNGATEWAY"), gw4, gw6);
    echo(INFO, "done.");

    echo(INFO, "Configuring \"" + env("TUNDEV") + "\" / " + env("TUNIDX") + " interface for Legacy IP...");

    if (!env("CISCO_SPLIT_INC")) {
        // Interface metric must be set to 1 in order to add a route with metric 1 since Windows Vista
        run("netsh interface ip set interface " + env("TUNIDX") + " metric=1 store=active");
    }

    if (env("CISCO_SPLIT_INC")) {
        run("netsh interface ip set address " + env("TUNIDX") + " static " +
            env("INTERNAL_IP4_ADDRESS") + " " + internal_ip4_netmask + " store=active");
    } else {
        // The default route will be added automatically
        run("netsh interface ip set address " + env("TUNIDX") + " static " +
            env("INTERNAL_IP4_ADDRESS") + " " + internal_ip4_netmask + " " + internal_gw +
            " gwmetric=1 store=active");
    }

    run("netsh interface ipv4 delete winsservers " + env("TUNIDX") + " all", true);
    if (env("INTERNAL_IP4_NBNS")) {
        var wins = env("INTERNAL_IP4_NBNS").split(/ /);
        for (var i = 0; i < wins.length; i++) {
            run("netsh interface ipv4 add winsservers " + env("TUNIDX") + " " + wins[i]);
        }
        echo(INFO, "Configured " + wins.length + " WINS servers: " + wins.join(" "));
    }

    run("netsh interface ipv4 delete dnsservers " + env("TUNIDX") + " all", true);
    run("netsh interface ipv6 delete dnsservers " + env("TUNIDX") + " all", true);
    if (env("INTERNAL_IP4_DNS")) {
        var dns = env("INTERNAL_IP4_DNS").split(/ /);
        for (var i = 0; i < dns.length; i++) {
            var protocol = dns[i].indexOf(":") !== -1 ? "ipv6" : "ipv4";
            // With 'validate=yes' (the default on newer Windows versions), Windows will try to
            // connect to the DNS server, time out after ~10 seconds, and print a warning, but
            // nevertheless add the specified server. Adding 'validate=no' is thus NECESSARY.
            // We know that Windows 7 supports/requires the 'validate=no' flag (see #52). If
            // someone using an older version of Windows that errors out on the unknown flag
            // really wants us to support it, we'll need to figure out how to distinguish it.
            run("netsh interface " + protocol + " add dnsservers " + env("TUNIDX") + " " + dns[i]
               + " validate=no");
        }
        echo(INFO, "Configured " + dns.length + " DNS servers: " + dns.join(" "));
    }
    echo(INFO, "done.");

    // Add internal network routes
    echo(INFO, "Configuring Legacy IP networks:");
    if (env("CISCO_SPLIT_INC")) {
        for (var i = 0 ; i < parseInt(env("CISCO_SPLIT_INC")); i++) {
            var network = env("CISCO_SPLIT_INC_" + i + "_ADDR");
            var netmask = env("CISCO_SPLIT_INC_" + i + "_MASK");
            var netmasklen = env("CISCO_SPLIT_INC_" + i + "_MASKLEN");
            run("route add " + network + " mask " + netmask +
                " " + internal_gw + " if " + env("TUNIDX"));
            echo(INFO, "Configured Legacy IP split-include route: " + network + "/" + netmasklen);
        }
    }

    excludedRoutes(gw4, gw6);
    echo(INFO, "Legacy IP route configuration done.");

    if (env("INTERNAL_IP6_ADDRESS")) {
        echo(INFO, "Configuring \"" + env("TUNDEV") + "\" / " + env("TUNIDX") + " interface for IPv6...");

        run("netsh interface ipv6 set address " + env("TUNIDX") + " " + env("INTERNAL_IP6_ADDRESS") + " store=active");

        echo(INFO, "done.");

        // Add internal network routes
        echo(INFO, "Configuring IPv6 networks:");
        if (env("INTERNAL_IP6_NETMASK") && !env("INTERNAL_IP6_NETMASK").match("/128$")) {
            run("netsh interface ipv6 add route " + env("INTERNAL_IP6_NETMASK") +
                " " + env("TUNIDX") + " store=active");
        }

        if (env("CISCO_IPV6_SPLIT_INC")) {
            for (var i = 0 ; i < parseInt(env("CISCO_IPV6_SPLIT_INC")); i++) {
                var network = env("CISCO_IPV6_SPLIT_INC_" + i + "_ADDR");
                var netmasklen = env("CISCO_IPV6_SPLIT_INC_" + i + "_MASKLEN");
                run("netsh interface ipv6 add route " + network + "/" +
                    netmasklen + " " + env("TUNIDX") + " store=active")
                echo(INFO, "Configured IPv6 split-include route: " + network + "/" + netmasklen);
            }
        } else {
            echo(INFO, "Setting default IPv6 route through VPN.");
            // Cover all unicast prefixes, including NAT64, without replacing the uplink default.
            run("netsh interface ipv6 add route ::/1 " + env("TUNIDX") + " store=active");
            run("netsh interface ipv6 add route 8000::/1 " + env("TUNIDX") + " store=active");
        }

        echo(INFO, "IPv6 route configuration done.");
    }

    break;
case "disconnect":
    echo(INFO, "Deconfiguring \"" + env("TUNDEV") + "\" / " + env("TUNIDX") + " interface...");

    // Use recorded next hops; the physical network may have changed or disappeared.
    releaseRoutes();

    // Delete address
    echo(INFO, "Removing" + (env("INTERNAL_IP6_ADDRESS") ? " IPv6 and" : "") + " Legacy IP addresses");
    run("netsh interface ipv4 delete address " + env("TUNIDX") + " " +
        env("INTERNAL_IP4_ADDRESS") + " gateway=all");
    if (env("INTERNAL_IP6_ADDRESS")) {
        run("netsh interface ipv6 delete address " + env("TUNIDX") + " " + env("INTERNAL_IP6_ADDRESS") + " store=active");

        if (!env("CISCO_IPV6_SPLIT_INC")) {
            echo(INFO, "Removing default IPv6 route through VPN.");
            run("netsh interface ipv6 delete route ::/1 " + env("TUNIDX"), true);
            run("netsh interface ipv6 delete route 8000::/1 " + env("TUNIDX"), true);
        }
    }

    echo(INFO, "done.");
}

if (accumulatedExitCode && env("reason") === "connect") releaseRoutes();
failureCleanup = null;

if (logToFile) {
	log.Close();
	failureLog = null;
}

WScript.Quit(accumulatedExitCode);

}
try {
    configureNetwork();
} catch (error) {
    var message = "VPN network script failed: " + (error.message || error.description || error);
    try { if (failureCleanup) failureCleanup(); } catch (ignoredCleanup) {}
    try {
        if (failureLog) {
            failureLog.WriteLine(message);
            failureLog.Close();
        } else {
            WScript.echo(message);
        }
    } catch (ignored) {}
    WScript.Quit(1);
}

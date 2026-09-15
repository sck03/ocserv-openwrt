"""Exercise the N1 nft guard in disposable Linux network namespaces.

Run as root: python3 tests/vpn_guard_tests.py
Requires iproute2 and nftables. All routes, nft rules and services are confined
to uniquely named namespaces; no host firewall or default route is changed.
Uses veth devices to simulate vpns interfaces, not real ocserv authentication.
"""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import threading
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = str(Path(__file__).resolve())
GUARD_TEMPLATE = ROOT / "server/openwrt/luci-app-ocserv-easy/root/usr/share/ocserv-easy/guard.nft.in"
GUARD = None


def run(*args, input=None, check=True):
    return subprocess.run(args, input=input, text=True, capture_output=True,
                          check=check, timeout=20)


def nsrun(namespace, *args, **kwargs):
    return run("ip", "netns", "exec", namespace, *args, **kwargs)


def response(label, peer):
    return json.dumps({"service": label, "source": peer[0]}).encode()


def serve(role):
    ports = ({22: "ssh", 443: "luci", 4443: "ocserv", 53: "dns",
              7890: "http-proxy", 7892: "redirect", 9090: "controller",
              23456: "custom-proxy"} if role == "router" else {9099: "upstream"})
    sockets = []

    def connection(conn, peer, label):
        with conn:
            while conn.recv(4096):
                conn.sendall(response(label, peer))

    def listener(sock, label, udp):
        while True:
            if udp:
                _, peer = sock.recvfrom(4096)
                sock.sendto(response(label, peer), peer)
            else:
                conn, peer = sock.accept()
                threading.Thread(target=connection, args=(conn, peer, label), daemon=True).start()

    for family in (socket.AF_INET, socket.AF_INET6):
        for port, label in ports.items():
            for udp in (False, True):
                sock = socket.socket(family, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM)
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                if family == socket.AF_INET6:
                    sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
                host = "::" if family == socket.AF_INET6 else "0.0.0.0"
                if role == "upstream":
                    # UDP replies must use the queried address, not the LAN
                    # address selected by a wildcard-bound sendto socket.
                    host = "2001:db8:2::10" if family == socket.AF_INET6 else "203.0.113.10"
                sock.bind((host, port))
                if not udp:
                    sock.listen(16)
                sockets.append(sock)
                threading.Thread(target=listener, args=(sock, label, udp), daemon=True).start()

    if role == "router":
        # Receive the original UDP destination and send a transparent reply
        # using that address, as a real TPROXY socket must do.
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_IP, 19, 1)  # IP_TRANSPARENT
        sock.setsockopt(socket.SOL_IP, 20, 1)  # IP_RECVORIGDSTADDR
        sock.bind(("127.0.0.1", 7895))
        sockets.append(sock)

        def tproxy():
            while True:
                _, ancillary, _, peer = sock.recvmsg(4096, 128)
                for level, kind, data in ancillary:
                    if level == socket.SOL_IP and kind == 20:
                        destination = (socket.inet_ntoa(data[4:8]), int.from_bytes(data[2:4], "big"))
                        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reply:
                            reply.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                            reply.setsockopt(socket.SOL_IP, 19, 1)
                            reply.bind(destination)
                            reply.sendto(response("tproxy", peer), peer)
                        break

        threading.Thread(target=tproxy, daemon=True).start()
    print("ready", flush=True)
    threading.Event().wait()


def request(host, port, protocol="tcp", source=""):
    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    sock = socket.socket(family, socket.SOCK_DGRAM if protocol == "udp" else socket.SOCK_STREAM)
    sock.settimeout(0.8)
    try:
        with sock:
            if source:
                sock.bind((source, 0))
            sock.connect((host, int(port)))
            sock.sendall(b"probe")
            return json.loads(sock.recv(4096))
    except OSError as error:
        return {"error": type(error).__name__}


def hold_connection():
    with socket.create_connection(("192.168.19.254", 7890), timeout=1) as sock:
        sock.sendall(b"before")
        print(sock.recv(4096).decode(), flush=True)
        sys.stdin.readline()
        try:
            sock.sendall(b"after")
            print(sock.recv(4096).decode(), flush=True)
        except OSError as error:
            print(json.dumps({"error": type(error).__name__}), flush=True)


class GuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        global GUARD
        if sys.platform != "linux" or os.geteuid() != 0:
            raise RuntimeError("Run this network-namespace test as root on Linux")
        for command in ("ip", "nft"):
            if not shutil.which(command):
                raise RuntimeError(f"Missing {command}")
        cls.rules_directory = tempfile.TemporaryDirectory(prefix="bulijie-guard-test-")
        cls.addClassCleanup(cls.rules_directory.cleanup)
        rules = GUARD_TEMPLATE.read_text(encoding="utf-8")
        for key, value in {"LAN_DEVICE":"br-lan", "LAN_ADDRESS":"192.168.19.254", "ADMIN_ADDRESS":"192.168.19.2",
                           "VPN_POOL":"10.77.0.0/24", "VPN_DNS":"10.77.0.1", "VPN_TCP_PORT":"4443", "VPN_UDP_PORT":"4443", "ADMIN_PORTS":"22, 80, 443"}.items():
            rules = rules.replace("@" + key + "@", value)
        GUARD = Path(cls.rules_directory.name) / "guard.nft"
        GUARD.write_text(rules, encoding="utf-8")
        cls.namespaces = []
        cls.servers = []
        cls.addClassCleanup(cls.cleanup)
        for role in ("router", "switch", "lan", "upstream", "vpn"):
            name = f"bvg-{os.getpid()}-{role}"
            run("ip", "netns", "add", name)
            cls.namespaces.append(name)
            setattr(cls, role, name)
            nsrun(name, "ip", "link", "set", "lo", "up")

        def pair(left, leftdev, right, rightdev):
            nsrun(left, "ip", "link", "add", leftdev, "type", "veth", "peer", "name", "tmp-peer")
            nsrun(left, "ip", "link", "set", "tmp-peer", "netns", right)
            nsrun(right, "ip", "link", "set", "tmp-peer", "name", rightdev)
            nsrun(left, "ip", "link", "set", leftdev, "up")
            nsrun(right, "ip", "link", "set", rightdev, "up")

        nsrun(cls.switch, "ip", "link", "add", "br0", "type", "bridge")
        nsrun(cls.switch, "ip", "link", "set", "br0", "up")
        for role, device in (("router", "br-lan"), ("lan", "eth0"), ("upstream", "eth0")):
            pair(getattr(cls, role), device, cls.switch, role)
            nsrun(cls.switch, "ip", "link", "set", role, "master", "br0")
        pair(cls.router, "vpns0", cls.vpn, "vpn0")

        addresses = [
            (cls.router, "br-lan", "192.168.19.254/24", "fd19::254/64"),
            (cls.lan, "eth0", "192.168.19.20/24", "fd19::20/64"),
            (cls.upstream, "eth0", "192.168.19.1/24", "fd19::1/64"),
            (cls.router, "vpns0", "10.77.0.1/24", "fd77::1/64"),
            (cls.vpn, "vpn0", "10.77.0.2/24", "fd77::2/64"),
        ]
        for namespace, device, ipv4, ipv6 in addresses:
            nsrun(namespace, "ip", "addr", "add", ipv4, "dev", device)
            nsrun(namespace, "ip", "addr", "add", ipv6, "dev", device, "nodad")
        for address in ("192.168.19.2/24", "10.77.0.99/32"):
            nsrun(cls.lan, "ip", "addr", "add", address, "dev", "eth0")
        nsrun(cls.upstream, "ip", "addr", "add", "203.0.113.10/32", "dev", "lo")
        nsrun(cls.upstream, "ip", "addr", "add", "2001:db8:2::10/128", "dev", "lo", "nodad")
        for namespace, gateway, gateway6 in ((cls.router, "192.168.19.1", "fd19::1"),
                                              (cls.lan, "192.168.19.254", "fd19::254"),
                                              (cls.vpn, "10.77.0.1", "fd77::1")):
            nsrun(namespace, "ip", "route", "add", "default", "via", gateway)
            nsrun(namespace, "ip", "-6", "route", "add", "default", "via", gateway6)
        nsrun(cls.router, "sysctl", "-qw", "net.ipv4.ip_forward=1", "net.ipv6.conf.all.forwarding=1")
        for device in ("all", "default", "br-lan", "vpns0"):
            nsrun(cls.router, "sysctl", "-qw", f"net.ipv4.conf.{device}.rp_filter=0",
                  f"net.ipv4.conf.{device}.send_redirects=0")
        # Static IPv6 neighbour: veth normally needs ND, unlike a real TUN.
        link = json.loads(nsrun(cls.router, "ip", "-j", "link", "show", "vpns0").stdout)[0]
        nsrun(cls.vpn, "ip", "-6", "neigh", "replace", "fd77::1", "lladdr", link["address"],
              "dev", "vpn0", "nud", "permanent")
        nsrun(cls.router, "ip", "rule", "add", "fwmark", "354", "lookup", "100")
        nsrun(cls.router, "ip", "route", "add", "local", "0.0.0.0/0", "dev", "lo", "table", "100")
        cls.proxy_rules = """
table inet simulated_openclash {
    chain mangle { type filter hook prerouting priority -150; policy accept;
        ip daddr 203.0.113.11 udp dport 443 counter meta mark set 354 tproxy ip to 127.0.0.1:7895 accept
    }
    chain dstnat { type nat hook prerouting priority -100; policy accept;
        ip daddr 203.0.113.11 tcp dport 80 counter redirect to :7892
        meta l4proto { tcp, udp } th dport 53 counter redirect to :53
    }
}
"""
        nsrun(cls.router, "nft", "-f", "-", input=cls.proxy_rules)
        for role in ("router", "upstream"):
            process = subprocess.Popen(["ip", "netns", "exec", getattr(cls, role),
                                        sys.executable, SCRIPT, "--serve", role],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            cls.servers.append(process)
            if process.stdout.readline().strip() != "ready":
                raise RuntimeError(f"Test server failed: {process.stderr.read()}")

    @classmethod
    def cleanup(cls):
        for process in cls.servers:
            process.terminate()
            process.communicate(timeout=5)
        for namespace in reversed(cls.namespaces):
            run("ip", "netns", "del", namespace, check=False)

    def setUp(self):
        nsrun(self.router, "nft", "-c", "-f", str(GUARD))
        nsrun(self.router, "nft", "-f", str(GUARD))

    def probe(self, role, host, port, protocol="tcp", source=""):
        result = nsrun(getattr(self, role), sys.executable, SCRIPT, "--request",
                       host, str(port), protocol, source)
        return json.loads(result.stdout)

    def allowed(self, role, host, port, service, protocol="tcp", source=""):
        result = self.probe(role, host, port, protocol, source)
        self.assertEqual(result.get("service"), service, result)
        return result

    def denied(self, role, host, port, protocol="tcp", source=""):
        self.assertIn("error", self.probe(role, host, port, protocol, source))

    def counter(self, comment):
        table = json.loads(nsrun(self.router, "nft", "-j", "list", "table", "inet", "bulijie_guard").stdout)
        for item in table["nftables"]:
            rule = item.get("rule", {})
            if rule.get("comment") == comment:
                return sum(expr.get("counter", {}).get("packets", 0) for expr in rule["expr"])
        self.fail(f"Missing counter: {comment}")

    def test_lan_can_reach_vpn_entry_tcp_and_udp(self):
        for protocol in ("tcp", "udp"):
            with self.subTest(protocol=protocol):
                self.allowed("lan", "192.168.19.254", 4443, "ocserv", protocol)

    def test_management_requires_admin_ip(self):
        for port, service in ((22, "ssh"), (443, "luci")):
            with self.subTest(port=port):
                self.allowed("lan", "192.168.19.254", port, service, source="192.168.19.2")
                self.denied("lan", "192.168.19.254", port)

    def test_lan_dns_proxy_controller_and_custom_ports_are_denied(self):
        for port in (53, 7890, 9090, 23456):
            for protocol in ("tcp", "udp"):
                with self.subTest(port=port, protocol=protocol):
                    self.denied("lan", "192.168.19.254", port, protocol)
        self.assertGreater(self.counter("LAN service denied"), 0)

    def test_admin_has_no_proxy_exemption(self):
        self.denied("lan", "192.168.19.254", 7890, source="192.168.19.2")

    def test_lan_gateway_cannot_reach_upstream_or_redirect_or_tproxy(self):
        for host, port, protocol in (("203.0.113.10", 9099, "tcp"),
                                     ("203.0.113.11", 80, "tcp"),
                                     ("203.0.113.11", 443, "udp"),
                                     ("203.0.113.10", 53, "udp")):
            with self.subTest(host=host, port=port):
                self.denied("lan", host, port, protocol)
        self.assertGreater(self.counter("LAN gateway bypass"), 0)

    def test_lan_cannot_use_vpn_dns_address(self):
        self.denied("lan", "10.77.0.1", 53, "udp")

    def test_lan_forged_vpn_source_is_rejected_before_conntrack(self):
        self.denied("lan", "192.168.19.254", 4443, source="10.77.0.99")
        self.assertGreater(self.counter("LAN forged VPN source"), 0)

    def test_vpn_dns_tcp_and_udp_work(self):
        for protocol in ("tcp", "udp"):
            with self.subTest(protocol=protocol):
                self.allowed("vpn", "10.77.0.1", 53, "dns", protocol)

    def test_vpn_cannot_directly_open_management_or_controller(self):
        for port in (443, 9090):
            with self.subTest(port=port):
                self.denied("vpn", "192.168.19.254", port)

    def test_vpn_transparent_tcp_and_udp_proxy_work(self):
        self.allowed("vpn", "203.0.113.11", 80, "redirect")
        self.allowed("vpn", "203.0.113.11", 443, "tproxy", "udp")

    def test_vpn_direct_traffic_is_natted_and_replies_work(self):
        for protocol in ("tcp", "udp"):
            with self.subTest(protocol=protocol):
                result = self.allowed("vpn", "203.0.113.10", 9099, "upstream", protocol)
                self.assertEqual(result["source"], "192.168.19.254")

    def test_router_own_ipv4_and_ipv6_reply_traffic_works(self):
        for host in ("203.0.113.10", "2001:db8:2::10"):
            for protocol in ("tcp", "udp"):
                with self.subTest(host=host, protocol=protocol):
                    self.allowed("router", host, 9099, "upstream", protocol)

    def test_lan_ipv6_gateway_and_local_proxy_are_denied(self):
        self.denied("lan", "2001:db8:2::10", 9099)
        self.denied("lan", "fd19::254", 7890)

    def test_unconfigured_vpn_ipv6_is_denied(self):
        self.denied("vpn", "2001:db8:2::10", 9099)
        self.assertGreater(self.counter("VPN IPv6 disabled"), 0)

    def test_wildcard_handles_renamed_dynamic_vpn_device(self):
        nsrun(self.router, "ip", "link", "set", "vpns0", "down")
        nsrun(self.router, "ip", "link", "set", "vpns0", "name", "vpns23")
        nsrun(self.router, "ip", "link", "set", "vpns23", "up")
        try:
            self.allowed("vpn", "10.77.0.1", 53, "dns", "udp")
            self.allowed("vpn", "203.0.113.11", 80, "redirect")
        finally:
            nsrun(self.router, "ip", "link", "set", "vpns23", "down")
            nsrun(self.router, "ip", "link", "set", "vpns23", "name", "vpns0")
            nsrun(self.router, "ip", "link", "set", "vpns0", "up")

    def test_repeat_load_and_downstream_firewall_recreation_preserve_guard(self):
        nsrun(self.router, "nft", "-f", str(GUARD))
        nsrun(self.router, "nft", "delete", "table", "inet", "simulated_openclash")
        nsrun(self.router, "nft", "-f", "-", input=self.proxy_rules)
        self.denied("lan", "203.0.113.11", 80)
        self.allowed("vpn", "203.0.113.11", 80, "redirect")

    def test_bad_reload_is_atomic_and_keeps_existing_guard(self):
        result = nsrun(self.router, "nft", "-f", "-", check=False,
                       input=GUARD.read_text() + "\ninvalid_statement\n")
        self.assertNotEqual(result.returncode, 0)
        self.denied("lan", "192.168.19.254", 7890)

    def test_preexisting_unauthorized_proxy_connection_is_cut_off(self):
        nsrun(self.router, "nft", "delete", "table", "inet", "bulijie_guard")
        process = subprocess.Popen(["ip", "netns", "exec", self.lan,
                                    sys.executable, SCRIPT, "--hold"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        try:
            before = json.loads(process.stdout.readline())
            self.assertEqual(before["service"], "http-proxy")
            nsrun(self.router, "nft", "-f", str(GUARD))
            output, error = process.communicate("probe\n", timeout=5)
            self.assertEqual(process.returncode, 0, error)
            self.assertIn("error", json.loads(output))
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--serve":
        serve(sys.argv[2])
    elif len(sys.argv) > 1 and sys.argv[1] == "--request":
        print(json.dumps(request(*sys.argv[2:])))
    elif len(sys.argv) > 1 and sys.argv[1] == "--hold":
        hold_connection()
    else:
        unittest.main(verbosity=2)

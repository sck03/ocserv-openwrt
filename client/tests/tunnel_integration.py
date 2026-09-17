"""Real Wintun/CSTP regressions for an isolated Windows administrator test machine.

Creates a temporary 198.18.0.0/24 split tunnel to a loopback TLS peer. Never use a
production VPN server. The ordinary test-client.py run does not enable this test.
"""
import argparse
import ctypes
import http.server
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import threading
import time
import uuid

from auth_integration import certificate, PASSWORD, COOKIE

PAYLOAD = b"bulijie-loopback-tunnel"


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    value = sum(struct.unpack("!" + "H" * (len(data) // 2), data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def echo_datagram(packet):
    if len(packet) < 28 or packet[0] >> 4 != 4 or packet[9] != 17:
        return None
    header_size = (packet[0] & 15) * 4
    if header_size < 20 or len(packet) < header_size + 8:
        return None
    source, destination, length, _ = struct.unpack_from("!HHHH", packet, header_size)
    if destination != 37001 or packet[header_size + 8:header_size + length] != PAYLOAD:
        return None
    if packet[12:20] != socket.inet_aton("198.18.0.2") + socket.inet_aton("198.18.0.1"):
        return None
    udp = struct.pack("!HHHH", destination, source, len(PAYLOAD) + 8, 0) + PAYLOAD
    header = bytearray(struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 1, 0, 64, 17, 0,
                                   packet[16:20], packet[12:16]))
    struct.pack_into("!H", header, 10, checksum(header))
    return bytes(header) + udp


def fixture_case(client, output, credentials, mode):
    counts = {"credentials": 0, "connects": 0, "datagrams": 0}

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def auth(self, body):
            success = PASSWORD.encode() in body
            if success:
                counts["credentials"] += 1
                text = (f'<config-auth client="vpn" type="complete"><auth id="success"><message>OK</message></auth>'
                        f'<session-token>{COOKIE}</session-token></config-auth>')
            else:
                text = ('<config-auth client="vpn" type="auth-request"><version who="sg">1.0</version>'
                        '<auth id="main"><form method="post" action="/">'
                        '<input type="text" name="username" label="Username:"/>'
                        '<input type="password" name="password" label="Password:"/>'
                        '</form></auth></config-auth>')
            raw = ('<?xml version="1.0"?>' + text).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/xml; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            if success:
                self.send_header("Set-Cookie", f"webvpn={COOKIE}; Path=/; Secure; HttpOnly")
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self):
            self.auth(b"")

        def do_POST(self):
            self.auth(self.rfile.read(int(self.headers.get("Content-Length", "0"))))

        def do_CONNECT(self):
            counts["connects"] += 1
            number = counts["connects"]
            self.send_response(200, "CONNECTED")
            for key, value in {
                "X-CSTP-Version": "1", "X-CSTP-MTU": "1400", "X-CSTP-Address": "198.18.0.2",
                "X-CSTP-Netmask": "255.255.255.255", "X-CSTP-Split-Include": "198.18.0.0/255.255.255.0",
                "X-CSTP-Keepalive": "5", "X-CSTP-DPD": "5",
            }.items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.flush()
            self.close_connection = True
            self.connection.settimeout(0.5)
            buffer = b""
            deadline = time.monotonic() + 90
            try:
                while time.monotonic() < deadline:
                    try:
                        block = self.connection.recv(65536)
                    except socket.timeout:
                        continue
                    if not block:
                        return
                    buffer += block
                    while len(buffer) >= 8:
                        if buffer[:4] != b"STF\x01":
                            raise RuntimeError("Unexpected CSTP frame")
                        length = int.from_bytes(buffer[4:6], "big")
                        if len(buffer) < 8 + length:
                            break
                        kind, payload = buffer[6], buffer[8:8 + length]
                        buffer = buffer[8 + length:]
                        if kind == 3:  # OpenConnect AC_PKT_DPD_OUT -> AC_PKT_DPD_RESP.
                            self.connection.sendall(b"STF\x01\x00\x00\x04\x00")
                        elif kind == 0:
                            reply = echo_datagram(payload)
                            if reply:
                                counts["datagrams"] += 1
                                self.connection.sendall(b"STF\x01" + struct.pack("!HBB", len(reply), 0, 0) + reply)
                                if mode == "reconnect_failure" and number == 1:
                                    return
            except (OSError, ssl.SSLError):
                return

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(credentials[0], credentials[1])
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    directory = output / mode
    directory.mkdir()
    config = {
        "gateway": f"https://127.0.0.1:{server.server_port}", "directory": str(directory / "data"),
        "pin": credentials[2], "password": PASSWORD, "tunnel": True, "disable_udp": True,
        # Windows initially marks a newly assigned address tentative while DAD completes.
        "udp_probe": mode != "connect_failure", "tunnel_duration_ms": 6000 if mode == "normal" else 12000,
    }
    if mode != "normal":
        script = directory / "failure.js"
        reason = "connect" if mode == "connect_failure" else "reconnect"
        source = ('if (WScript.CreateObject("WScript.Shell").Environment("Process")("reason") === "'
                  + reason + '") WScript.Quit(7);\n')
        if mode == "reconnect_failure":
            source += (client.parent / "vpnc-script-win.js").read_text(encoding="utf-8")
        script.write_text(source, encoding="utf-8")
        config["script"] = str(script)
    fixture = directory / "fixture.json"
    fixture.write_text(json.dumps(config), encoding="utf-8")
    try:
        result = subprocess.run([str(client), str(fixture)], capture_output=True, timeout=100)
        (directory / "stdout.log").write_bytes(result.stdout)
        (directory / "stderr.log").write_bytes(result.stderr)
        try:
            report = json.loads(result.stdout.decode("utf-8"))
        except (ValueError, UnicodeError) as error:
            raise RuntimeError(f"Native tunnel driver exited with {result.returncode}: "
                               + result.stderr.decode("utf-8", "replace")) from error
        (directory / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        passed = (result.returncode == 0 and not report["timeout"] and report["adapter_removed"]
                  and counts["credentials"] == 1)
        if mode == "normal":
            passed = passed and report["terminal"] == "idle" and report["udp_probe"] and counts["datagrams"] > 0
        elif mode == "connect_failure":
            passed = passed and report["terminal"] == "failed" and "connected" not in report["states"]
        else:
            passed = (passed and report["terminal"] == "failed" and counts["connects"] >= 2
                      and report["states"].count("connected") == 1 and counts["datagrams"] > 0)
        return {"test": mode, "passed": bool(passed), **counts,
                "terminal": report["terminal"], "adapter_removed": report["adapter_removed"]}
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-network-changes", action="store_true")
    args = parser.parse_args()
    if os.name != "nt" or not args.allow_network_changes or not ctypes.windll.shell32.IsUserAnAdmin():
        parser.error("Requires an isolated Windows administrator machine and --allow-network-changes")
    output = args.output.resolve() / uuid.uuid4().hex
    output.mkdir(parents=True)
    credentials = certificate(output, "loopback")
    results = [fixture_case(args.client.resolve(), output, credentials, mode)
               for mode in ("normal", "connect_failure", "reconnect_failure")]
    report = {"passed": sum(case["passed"] for case in results), "total": len(results), "results": results}
    (args.output / "tunnel-results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))
    raise SystemExit(0 if all(case["passed"] for case in results) else 1)


if __name__ == "__main__":
    main()

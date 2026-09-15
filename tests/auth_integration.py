"""Exercise the real native TLS/authentication stack against a loopback-only fixture.

No virtual adapter, administrator rights, physical routes, or real credentials are used.
Python/OpenSSL here are build-time test tools and are not part of the client package.
"""
import argparse
import base64
import hashlib
import http.server
import json
from pathlib import Path
import ssl
import subprocess
import threading
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    key, cert = args.output / "fixture.key", args.output / "fixture.pem"
    config = args.output / "openssl.cnf"
    config.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
    subprocess.run([str(args.openssl), "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(key),
                    "-out", str(cert), "-days", "1", "-config", str(config), "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost", "-addext", "basicConstraints=critical,CA:TRUE"],
                   check=True, capture_output=True)
    pub = subprocess.run([str(args.openssl), "x509", "-in", str(cert), "-pubkey", "-noout"], check=True, capture_output=True).stdout
    der = subprocess.run([str(args.openssl), "pkey", "-pubin", "-outform", "DER"], input=pub, check=True, capture_output=True).stdout
    pin = "pin-sha256:" + base64.b64encode(hashlib.sha256(der).digest()).decode("ascii")
    counts = {"credentials": 0, "requests": 0}

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def reply(self, text, cookie=False):
            body = text.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/xml; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            if cookie:
                self.send_header("Set-Cookie", "webvpn=fixture-session-token; Path=/; Secure; HttpOnly")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            self.handle_auth(b"")

        def do_POST(self):
            n = int(self.headers.get("Content-Length", "0"))
            if n > 65536:
                self.send_error(413)
                return
            self.handle_auth(self.rfile.read(n))

        def handle_auth(self, body):
            counts["requests"] += 1
            supplied = b"fixture-password" in body
            if supplied:
                counts["credentials"] += 1
            if self.path.startswith("/redirect"):
                self.send_response(302)
                redirect_host = "127.0.0.1" if self.headers.get("Host", "").startswith("localhost:") else "localhost"
                self.send_header("Location", f"https://{redirect_host}:{server.server_port}/ok")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if self.path.startswith("/stall"):
                time.sleep(1)
                self.close_connection = True
                return
            if supplied and not self.path.startswith("/reject"):
                self.reply('<?xml version="1.0"?><config-auth client="vpn" type="complete"><auth id="success"><message>OK</message></auth><session-token>fixture-session-token</session-token></config-auth>', True)
                return
            name = "otp" if self.path.startswith("/otp") else "password"
            error = "<error>Wrong credentials</error>" if supplied else ""
            self.reply(f'''<?xml version="1.0"?><config-auth client="vpn" type="auth-request"><version who="sg">1.0</version><auth id="main"><title>Test VPN</title>{error}<message>Enter credentials</message><form method="post" action="{self.path}"><input type="text" name="username" label="Username:"/><input type="password" name="{name}" label="Password:"/></form></auth></config-auth>''')

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.minimum_version = ssl.TLSVersion.TLSv1_2
    tls.load_cert_chain(cert, key)
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    results = []
    try:
        for name, route, cert_pin, expected_success, expected_posts in [
            ("trusted_pin_authentication", "/ok", pin, True, 1),
            ("untrusted_certificate_before_credentials", "/ok", "-", False, 0),
            ("mismatched_pin_before_credentials", "/ok", "pin-sha256:" + "A" * 43 + "=", False, 0),
            ("incorrect_password_does_not_loop", "/reject", pin, False, 1),
            ("unsupported_second_factor_does_not_receive_password", "/otp", pin, False, 0),
            ("cross_origin_redirect_rejected", "/redirect", pin, False, 0),
            ("explicit_ca_and_hostname", "/ok", "ca:" + str(cert.resolve()), True, 1),
            ("explicit_ca_hostname_mismatch", "/ok", "ca:" + str(cert.resolve()), False, 0),
            ("explicit_ca_cross_origin_redirect", "/redirect", "ca:" + str(cert.resolve()), False, 0),
            ("cancel_during_authentication", "/stall", pin, True, 0),
        ]:
            before = dict(counts)
            host = "localhost" if name in {"explicit_ca_and_hostname", "explicit_ca_cross_origin_redirect"} else "127.0.0.1"
            url = f"https://{host}:{server.server_port}{route}"
            command = "--cancel-auth" if name == "cancel_during_authentication" else "--authenticate"
            completed = subprocess.run([str(args.client.resolve()), command, url, cert_pin, "fixture-user"],
                                       input="fixture-password\n", text=True, capture_output=True, timeout=25)
            sent = counts["credentials"] - before["credentials"]
            passed = ((completed.returncode == 0) == expected_success and sent == expected_posts
                      and "fixture-password" not in completed.stdout + completed.stderr)
            result = {"test": name, "passed": passed, "exit_code": completed.returncode,
                      "credential_submissions": sent, "client_result": completed.stdout.strip()}
            results.append(result)
            print(json.dumps(result))
        trust_dir = args.output / ("trust-" + str(time.time_ns()))
        trust_dir.mkdir()
        for name, route, mode, success, posts, prompts, changes in [
            ("first_certificate_cancelled", "/ok", "reject", False, 0, 1, 0),
            ("pending_certificate_is_cancellable", "/ok", "cancel", True, 0, 1, 0),
            ("first_certificate_confirmed", "/ok", "accept", True, 1, 1, 0),
            ("remembered_key_survives_new_process", "/ok", "auto", True, 1, 0, 0),
            ("remembered_key_same_host_other_path", "/another-path", "auto", True, 1, 0, 0),
            ("changed_key_rejected", "/ok", "reject", False, 0, 1, 1),
            ("changed_key_cancelled", "/ok", "cancel", True, 0, 1, 1),
            ("changed_key_explicitly_confirmed", "/ok", "accept", True, 1, 1, 1),
            ("replacement_key_remembered", "/ok", "auto", True, 1, 0, 0),
        ]:
            if name == "changed_key_rejected":
                replacement_key, replacement_cert = args.output / "replacement.key", args.output / "replacement.pem"
                subprocess.run([str(args.openssl), "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(replacement_key),
                                "-out", str(replacement_cert), "-days", "1", "-config", str(config), "-subj", "/CN=changed-server"],
                               check=True, capture_output=True)
                tls.load_cert_chain(replacement_cert, replacement_key)
            before = dict(counts)
            saved_before = {p.name: p.read_bytes() for p in trust_dir.glob("*.ini")}
            completed = subprocess.run([str(args.client.resolve()), "--authenticate-trust", f"https://127.0.0.1:{server.server_port}{route}",
                                        str(trust_dir.resolve()), mode], input="fixture-password\n", text=True, capture_output=True, timeout=25)
            sent = counts["credentials"] - before["credentials"]
            saved_after = {p.name: p.read_bytes() for p in trust_dir.glob("*.ini")}
            passed = ((completed.returncode == 0) == success and sent == posts
                      and f"prompts={prompts} changes={changes}" in completed.stdout
                      and "fixture-password" not in completed.stdout + completed.stderr
                      and (mode == "accept" or saved_before == saved_after))
            result = {"test": name, "passed": passed, "exit_code": completed.returncode,
                      "credential_submissions": sent, "client_result": completed.stdout.strip()}
            results.append(result)
            print(json.dumps(result))
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=2)
    (args.output / "auth-results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    raise SystemExit(0 if all(result["passed"] for result in results) else 1)


if __name__ == "__main__":
    main()

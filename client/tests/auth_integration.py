"""Real OpenConnect TLS/auth callbacks against synthetic, loopback-only HTTPS servers.

No adapter or system routes are created. cryptography is a test dependency only.
"""
import argparse
import base64
from datetime import datetime, timedelta, timezone
import hashlib
import http.server
import ipaddress
import json
from pathlib import Path
import ssl
import subprocess
import threading
import time
import urllib.parse
import uuid
import xml.etree.ElementTree as ET

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID


PASSWORD = "fixture-password-050"
WRONG_PASSWORD = "wrong-fixture-password"
OTP = "654321"
COOKIE = "fixture-session-cookie-050"


def certificate(folder, name, expired=False):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
    now = datetime.now(timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - timedelta(days=2))
            .not_valid_after(now + timedelta(days=-1 if expired else 2))
            .add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost"),
                x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]), critical=False)
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
            .sign(key, hashes.SHA256()))
    key_path, cert_path = folder / f"{name}.key", folder / f"{name}.pem"
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    public = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    pin = "pin-sha256:" + base64.b64encode(hashlib.sha256(public).digest()).decode("ascii")
    return cert_path, key_path, pin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve() / uuid.uuid4().hex
    output.mkdir(parents=True, exist_ok=True)
    original = certificate(output, "original")
    replacement = certificate(output, "replacement")
    expired = certificate(output, "expired", expired=True)
    counts = {"requests": 0, "credentials": 0, "passwords": [], "choices": []}

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def reply(self, text, cookie=False):
            raw = text.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/xml; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            if cookie:
                self.send_header("Set-Cookie", f"webvpn={COOKIE}; Path=/; Secure; HttpOnly")
            self.end_headers()
            self.wfile.write(raw)

        def form(self, fields, error="", message="Enter credentials"):
            self.reply(f'<?xml version="1.0"?><config-auth client="vpn" type="auth-request">'
                       f'<version who="sg">1.0</version><auth id="main"><title>Fixture VPN</title>'
                       f'{error}<message>{message}</message><form method="post" action="{self.path}">'
                       f'{fields}</form></auth></config-auth>')

        def success(self):
            self.reply(f'<?xml version="1.0"?><config-auth client="vpn" type="complete">'
                       f'<auth id="success"><message>OK</message></auth>'
                       f'<session-token>{COOKIE}</session-token></config-auth>', True)

        def do_GET(self):
            self.auth(b"")

        def do_POST(self):
            size = int(self.headers.get("Content-Length", "0"))
            if size > 65536:
                self.send_error(413)
                return
            self.auth(self.rfile.read(size))

        def auth(self, body):
            counts["requests"] += 1
            values = {}
            try:
                document = ET.fromstring(body)
                values = {node.tag: node.text or "" for node in document.iter() if node.tag in ("username", "password", "secondary_password", "region", "group_list", "group-select")}
            except ET.ParseError:
                values = {key: value[0] for key, value in urllib.parse.parse_qs(body.decode("utf-8")).items() if value}
            password = values.get("password", values.get("secondary_password", ""))
            if password:
                counts["credentials"] += 1
                counts["passwords"].append(password)
            for field in ("region", "group_list", "group-select"):
                if field in values:
                    counts["choices"].append(values[field])
            if self.path == "/redirect":
                self.send_response(302)
                self.send_header("Location", f"https://localhost:{server.server_port}/ok")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if self.path == "/stall":
                time.sleep(3)
                self.close_connection = True
                return
            if self.path == "/notice" and counts["requests"] == 1:
                self.form('<input type="hidden" name="notice_ack" value="1"/>', message="Synthetic server notice")
                return
            if self.path == "/otp" and password == PASSWORD:
                self.form('<input type="password" name="password" label="One-time verification code:"/>')
                return
            expected = OTP if self.path == "/otp" else PASSWORD
            if password == expected and self.path != "/reject":
                self.success()
                return
            fields = '<input type="text" name="username" label="Username:"/>'
            if self.path == "/choice":
                fields += '<select name="region" label="Region:"><option value="north">North</option><option value="south">South</option></select>'
            if self.path == "/group":
                fields += '<select name="group_list" label="Group:"><option value="north">North</option><option value="south">South</option></select>'
            fields += '<input type="password" name="password" label="Password:"/>'
            self.form(fields, "<error>Wrong credentials</error>" if password else "")

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.minimum_version = ssl.TLSVersion.TLSv1_2
    tls.load_cert_chain(original[0], original[1])
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    results = []

    def case(name, route="/ok", terminal="idle", posts=1, checks=None, **options):
        counts.update(requests=0, credentials=0, passwords=[], choices=[])
        directory = options.pop("store", name)
        config = {"gateway": f"https://127.0.0.1:{server.server_port}{route}",
                  "directory": str(output / directory), "password": PASSWORD, **options}
        fixture = output / f"{name}.json"
        fixture.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
        run = subprocess.run([str(args.client.resolve()), str(fixture)], capture_output=True, timeout=70)
        try:
            report = json.loads(run.stdout.decode("utf-8"))
        except Exception:
            report = {"terminal": "invalid-output", "stdout": run.stdout.decode("utf-8", "replace"), "stderr": run.stderr.decode("utf-8", "replace")}
        logs = "\n".join(report.get("logs", []))
        passed = (run.returncode == 0 and report.get("terminal") == terminal and counts["credentials"] == posts
                  and not report.get("timeout", True)
                  and all(secret not in logs for secret in (PASSWORD, WRONG_PASSWORD, COOKIE, OTP)))
        if checks:
            passed = passed and checks(report)
        record = {"test": name, "passed": bool(passed), "terminal": report.get("terminal"),
                  "credential_submissions": counts["credentials"], "prompts": report.get("prompts", [])}
        results.append(record)
        (output / f"{name}-result.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(json.dumps(record), flush=True)

    try:
        case("verified_fixed_pin", pin=original[2], checks=lambda r: r["stoken"] and r["oath"] and r["system_keys"])
        case("untrusted_certificate_sends_no_credentials", posts=0, checks=lambda r: r["canceled"] and len(r["prompts"]) == 1)
        case("wrong_fixed_pin_is_not_overridable", terminal="failed", posts=0, pin=replacement[2], accept_certificate=True,
             checks=lambda r: not r["prompts"])
        case("custom_CA", ca_file=str(original[0]), checks=lambda r: all(p["kind"] != "certificate" for p in r["prompts"]))
        case("wrong_custom_CA_is_not_overridable", terminal="failed", posts=0, ca_file=str(replacement[0]), accept_certificate=True)
        case("approve_certificate", accept_certificate=True, store="remembered", checks=lambda r: r["saved_pin"])
        case("remembered_pin_after_restart", reuse_profile=True, store="remembered",
             checks=lambda r: all(p["kind"] != "certificate" for p in r["prompts"]))
        case("remembered_pin_other_path", route="/other", reuse_profile=True, store="remembered",
             checks=lambda r: all(p["kind"] != "certificate" for p in r["prompts"]))
        tls.load_cert_chain(replacement[0], replacement[1])
        case("changed_pin_cancel", posts=0, reuse_profile=True, store="remembered",
             checks=lambda r: r["canceled"] and r["prompts"][0]["changed_pin"])
        case("changed_pin_approve", reuse_profile=True, store="remembered", accept_certificate=True,
             checks=lambda r: r["prompts"][0]["changed_pin"])
        tls.load_cert_chain(original[0], original[1])
        case("saved_password_auto_login", pin=original[2], batch_mode=True, saved_password=PASSWORD,
             checks=lambda r: all(p["kind"] != "password" for p in r["prompts"]))
        case("wrong_saved_password_prompts_again", route="/retry", posts=2, pin=original[2], batch_mode=True, saved_password=WRONG_PASSWORD,
             checks=lambda r: r["saved_password_matches_response"])
        case("OTP_is_not_saved_as_password", route="/otp", posts=2, pin=original[2], batch_mode=True, saved_password=PASSWORD,
             responses={"password": OTP}, checks=lambda r: r["saved_password_matches_response"] and counts["passwords"] == [PASSWORD, OTP])
        case("generic_choice_form", route="/choice", pin=original[2], choice=1, checks=lambda r: "south" in counts["choices"])
        case("authentication_group", route="/group", pin=original[2], choice=1,
             checks=lambda r: "south" in counts["choices"] and len([p for p in r["prompts"] if p["kind"] == "selection"]) == 1)
        case("server_notice", route="/notice", pin=original[2], checks=lambda r: any(p["kind"] == "notice" for p in r["prompts"]))
        case("password_cancel", posts=0, pin=original[2], cancel_prompt="password", checks=lambda r: r["canceled"])
        case("redirect_requires_new_trust", route="/redirect", posts=0, pin=original[2], batch_mode=True, saved_password=PASSWORD,
             checks=lambda r: r["canceled"])
        case("redirect_never_autofills_credentials", route="/redirect", pin=original[2], batch_mode=True, saved_password=WRONG_PASSWORD,
             accept_certificate=True, allow_redirect_prompts=True,
             checks=lambda r: any(p["kind"] == "text" and p["initial_empty"] for p in r["prompts"]) and not r["saved_password_matches_response"])
        case("cancellation_during_HTTPS", route="/stall", posts=0, pin=original[2], cancel_after_ms=250,
             checks=lambda r: r["canceled"] and r["elapsed_ms"] < 2000)
        tls.load_cert_chain(expired[0], expired[1])
        case("expired_certificate_rejects_even_matching_pin", terminal="failed", posts=0, pin=expired[2], accept_certificate=True,
             checks=lambda r: not r["prompts"])
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=3)
    (output / "auth-results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    (args.output / "auth-results.json").write_text(json.dumps({"run": str(output), "results": results}, indent=2), encoding="utf-8")
    print(json.dumps({"passed": sum(r["passed"] for r in results), "total": len(results), "run": str(output)}))
    raise SystemExit(0 if all(r["passed"] for r in results) else 1)


if __name__ == "__main__":
    main()

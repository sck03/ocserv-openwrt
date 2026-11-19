"""Run each browser suite against a fresh isolated backend and reap its server."""
import argparse
from contextlib import contextmanager
from pathlib import Path
import socket
import subprocess
import sys
import time
import urllib.request
import os
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = dict(os.environ, PYTHONUTF8='1', PYTHONDONTWRITEBYTECODE='1')

@contextmanager
def preview(proxy):
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
    log = tempfile.TemporaryFile()
    server = subprocess.Popen([sys.executable, '-B', str(ROOT / 'tests/server_preview.py'), '--port', str(port), '--proxy', proxy.lower()],
                              cwd=ROOT, env=ENVIRONMENT, stdout=subprocess.DEVNULL, stderr=log)
    url = f'http://127.0.0.1:{port}/'
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if server.poll() is not None:
                log.seek(0)
                raise RuntimeError(log.read(16384).decode('utf-8', 'replace'))
            try:
                urllib.request.urlopen(url, timeout=1).close()
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise TimeoutError('Isolated UI backend did not become ready')
        yield url
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill(); server.wait(timeout=5)
        log.close()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser-channel', default='chrome')
    parser.add_argument('--all-proxies', action='store_true')
    args = parser.parse_args()
    for proxy in (('OpenClash', 'Nikki', 'Momo') if args.all_proxies else ('OpenClash',)):
        suites = ['server_ui_tests']
        if proxy == 'OpenClash': suites += ['server_table_tests', 'server_polling_tests', 'server_online_tests']
        for suite in suites:
            with preview(proxy) as url:
                command = [sys.executable, '-B', str(ROOT / 'tests' / (suite + '.py')), '--url', url, '--browser-channel', args.browser_channel]
                if suite == 'server_ui_tests': command += ['--proxy', proxy, '--output', str(ROOT / 'test-results/server-ui' / proxy)]
                subprocess.run(command, cwd=ROOT, env=ENVIRONMENT, check=True, timeout=180)

if __name__ == '__main__': main()

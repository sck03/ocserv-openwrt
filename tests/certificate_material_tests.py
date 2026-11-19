"""Verify the real trust-material module with real certificates and certtool."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from lupa.lua51 import LuaRuntime
from lua_modules import install_modules


@unittest.skipUnless(shutil.which('openssl') and shutil.which('certtool'), 'openssl and certtool are required')
class CertificateMaterialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='lansyra-trust-tests-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name).resolve()
        assert cls.directory.is_relative_to(Path(tempfile.gettempdir()).resolve())
        cls.certtool = shutil.which('certtool')
        openssl = shutil.which('openssl')

        def command(*args):
            subprocess.run([openssl, *args], cwd=cls.directory, check=True, capture_output=True, timeout=30)

        for name in ('a', 'b'):
            command('req', '-x509', '-newkey', 'rsa:2048', '-noenc', '-days', '2',
                    '-subj', '/CN=Lansyra fixture CA ' + name,
                    '-addext', 'basicConstraints=critical,CA:TRUE',
                    '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
                    '-keyout', name + '-ca.key', '-out', name + '-ca.pem')
            command('req', '-new', '-newkey', 'rsa:2048', '-noenc', '-subj', '/CN=vpn.example.test',
                    '-addext', 'basicConstraints=critical,CA:FALSE',
                    '-addext', 'subjectAltName=DNS:vpn.example.test,IP:192.0.2.1',
                    '-keyout', name + '-server.key', '-out', name + '-server.csr')
            command('x509', '-req', '-days', '2', '-in', name + '-server.csr',
                    '-CA', name + '-ca.pem', '-CAkey', name + '-ca.key', '-CAcreateserial',
                    '-copy_extensions', 'copy', '-out', name + '-server.pem')

    def setUp(self):
        shutil.copyfile(self.directory / 'a-ca.pem', self.directory / 'ca.pem')
        shutil.copyfile(self.directory / 'a-server.pem', self.directory / 'server.pem')
        self.calls = 0
        self.rotate_after_verification = False
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        install_modules(self.lua)
        paths = {'/etc/ocserv/ca.pem': self.directory / 'ca.pem',
                 '/etc/ocserv/server.pem': self.directory / 'server.pem'}

        def read(path, limit):
            file = paths.get(path)
            if file is None or not file.exists():
                return None
            value = file.read_text(encoding='utf-8')
            if len(value.encode()) > limit:
                raise ValueError('fixture read limit')
            return value

        def run(arguments, timeout):
            self.calls += 1
            args = [arguments[i] for i in range(1, len(arguments) + 1)]
            result = subprocess.run([self.certtool, *[str(paths.get(arg, arg)) for arg in args[1:]]],
                                    capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=timeout)
            if self.rotate_after_verification:
                shutil.copyfile(self.directory / 'b-ca.pem', self.directory / 'ca.pem')
            return result.returncode, result.stdout + result.stderr

        self.lua.globals().read_fixture = read
        self.lua.globals().run_fixture = run
        self.lua.execute('''
            package.loaded['ocserv.storage']={read=function(path,limit)return read_fixture(path,limit)end}
            package.loaded['ocserv.process']={run=function(args,timeout)return run_fixture(args,timeout)end}
        ''')
        self.verify = self.lua.eval(r'''function(hostname, rendered)
            local material=require 'luci.model.ocserv_easy.certificate_material'
            local ok,value=pcall(material.ca,rendered or 'server-cert = /etc/ocserv/server.pem\n',hostname)
            return ok,ok and value or value.code
        end''')

    def test_matching_chain_and_hostname(self):
        ok, value = self.verify('vpn.example.test', None)
        self.assertTrue(ok)
        self.assertEqual(value, (self.directory / 'ca.pem').read_text())
        self.assertTrue(self.verify('192.0.2.1', None)[0])

    def test_unrelated_ca_is_rejected(self):
        shutil.copyfile(self.directory / 'b-ca.pem', self.directory / 'ca.pem')
        self.assertEqual(self.verify('vpn.example.test', None), (False, 'ca_certificate_mismatch'))

    def test_wrong_hostname_is_rejected(self):
        self.assertEqual(self.verify('other.example.test', None), (False, 'ca_certificate_mismatch'))

    def test_rotation_requires_matching_ca(self):
        shutil.copyfile(self.directory / 'b-server.pem', self.directory / 'server.pem')
        self.assertFalse(self.verify('vpn.example.test', None)[0])
        shutil.copyfile(self.directory / 'b-ca.pem', self.directory / 'ca.pem')
        self.assertTrue(self.verify('vpn.example.test', None)[0])

    def test_private_material_is_rejected_before_execution(self):
        path = self.directory / 'ca.pem'
        path.write_text(path.read_text() + '\n-----BEGIN PRIVATE KEY-----\n')
        self.assertEqual(self.verify(None, None), (False, 'ca_unavailable'))
        self.assertEqual(self.calls, 0)

    def test_change_during_verification_is_rejected(self):
        self.rotate_after_verification = True
        self.assertEqual(self.verify('vpn.example.test', None), (False, 'stale_revision'))


if __name__ == '__main__':
    unittest.main()

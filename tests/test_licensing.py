import argparse
import base64
import concurrent.futures
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from wsgiref.simple_server import make_server

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "licensing" / "server"))
from phoenix_license import Store, LicenseError, create_app, QuietHandler, b64, HEADER
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

PROBE = None
KEY_DIR = None
DLL = None
LOADER = None
DEVICE = "a" * 64
NONCE = "b" * 64


class ServiceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.now = 1800000000
        self.store = Store(Path(self.tmp.name), lambda: self.now, 30)
        self.store.initialize()
        self.id, self.code = self.store.issue(60)

    def activate(self, code=None, device=DEVICE):
        return self.store.dispatch("/v1/activate", {"product": "phoenix", "code": code or self.code, "device": device, "nonce": NONCE})

    def refresh(self, session):
        return self.store.dispatch("/v1/session/refresh", {"product": "phoenix", "session": session, "device": DEVICE, "nonce": NONCE})

    def assert_error(self, code, function):
        with self.assertRaises(LicenseError) as result:
            function()
        self.assertEqual(result.exception.code, code)

    def test_activation_refresh_expiry_extension_revocation(self):
        result = self.activate()
        first = self.store.list_licenses()[0]
        self.now += 10
        second = self.activate()
        self.assertEqual(self.store.list_licenses()[0]["expires_at"], first["expires_at"])
        self.assert_error("invalid_session", lambda: self.refresh(result["session"]))
        self.refresh(second["session"])
        self.now += 50
        self.assert_error("expired", lambda: self.refresh(second["session"]))
        self.store.administer("extend", self.id, 60)
        self.refresh(second["session"])
        self.store.administer("revoke", self.id)
        self.assert_error("revoked", lambda: self.refresh(second["session"]))
        self.store.administer("restore", self.id)
        self.refresh(second["session"])

    def test_invalid_device_unbind_and_no_plaintext_code(self):
        self.assert_error("invalid_code", lambda: self.activate("PHX-" + "0" * 32))
        result = self.activate()
        self.assert_error("device_mismatch", lambda: self.activate(device="c" * 64))
        self.store.administer("unbind", self.id)
        self.assert_error("invalid_session", lambda: self.refresh(result["session"]))
        self.activate(device="c" * 64)
        self.assertNotIn(self.code.encode(), self.store.db_path.read_bytes())
        self.assertNotIn(result["session"].encode(), self.store.db_path.read_bytes())

    def test_concurrent_activation_only_binds_one_device(self):
        def attempt(device):
            try:
                self.activate(device=device)
                return True
            except LicenseError:
                return False
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            outcomes = list(executor.map(attempt, ["c" * 64, "d" * 64]))
        self.assertEqual(sum(outcomes), 1)

    def test_clock_rollback_and_restart_persistence(self):
        result = self.activate()
        self.now -= 100
        self.assert_error("clock_rollback", lambda: self.refresh(result["session"]))
        self.now += 101
        self.store = Store(Path(self.tmp.name), lambda: self.now, 30)
        self.refresh(result["session"])
        pem = self.store.key_path.read_bytes()
        self.store.initialize()
        self.assertEqual(pem, self.store.key_path.read_bytes())

    def test_http_validation(self):
        app = create_app(Path(self.tmp.name), clock=lambda: self.now)
        def request(data, length=None, content_type="application/json"):
            statuses = []
            body = b"".join(app({"PATH_INFO": "/v1/activate", "REQUEST_METHOD": "POST",
                "CONTENT_TYPE": content_type, "CONTENT_LENGTH": str(len(data) if length is None else length),
                "wsgi.input": io.BytesIO(data)}, lambda status, headers: statuses.append(status)))
            return int(statuses[0].split()[0]), json.loads(body)
        self.assertEqual(request(b'{"product":"phoenix","product":"x"}')[0], 400)
        self.assertEqual(request(b"{}", length=999999)[0], 413)
        self.assertEqual(request(b"{}", content_type="text/plain")[0], 415)
        self.assertEqual(request(b"[]")[0], 400)


@unittest.skipUnless(PROBE, "native probe not supplied")
class NativeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.data = self.root / "data"
        self.data.mkdir()
        shutil.copyfile(KEY_DIR / "signing-key.pem", self.data / "signing-key.pem")
        self.store = Store(self.data, lease_seconds=3)
        self.store.initialize()
        self.client = self.root / "client"
        self.client.mkdir()
        self.server = make_server("127.0.0.1", 0, create_app(self.data, 3), handler_class=QuietHandler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.running = True
        self.addCleanup(self.stop_server)
        (self.client / "license-server.json").write_text(json.dumps({"url": f"http://127.0.0.1:{self.server.server_port}"}))

    def stop_server(self):
        if self.running:
            self.server.shutdown()
            self.server.server_close()
            self.thread.join()
            self.running = False

    def probe(self, action, *extra, code=None, expected=0):
        process = subprocess.run([str(PROBE), action, str(self.client), *map(str, extra)],
            input=(code + "\n") if code else None, capture_output=True, text=True, timeout=20)
        self.assertEqual(process.returncode, expected, process.stdout + process.stderr)
        if code:
            self.assertNotIn(code, process.stdout + process.stderr)
        return json.loads(process.stdout)

    def test_real_windows_client_activation_cache_expiry_and_renewal(self):
        self.assertEqual(self.probe("check", expected=6)["message"], "activation_required")
        self.assertEqual(self.probe("activate", code="PHX-" + "0" * 32, expected=6)["message"], "invalid_code")
        license_id, code = self.store.issue(3)
        activated = self.probe("activate", code=code)
        self.assertNotIn(code.encode(), (self.client / ".license" / "session.bin").read_bytes())
        self.probe("check")
        expired = self.probe("watch", 8, expected=6)
        self.assertFalse(expired["ok"])
        self.store.administer("extend", license_id, 60)
        self.assertGreater(self.probe("check")["expires"], activated["expires"])
        self.store.administer("revoke", license_id)
        self.assertEqual(self.probe("check", expected=6)["message"], "revoked")
        self.store.administer("restore", license_id)
        self.probe("check")

    def test_disconnect_does_not_extend_lease(self):
        _, code = self.store.issue(120)
        self.probe("activate", code=code)
        timer = threading.Timer(1, self.stop_server)
        timer.start()
        started = time.monotonic()
        result = self.probe("watch", 12, expected=6)
        timer.join()
        self.assertFalse(result["ok"])
        self.assertLess(time.monotonic() - started, 9)
        self.assertEqual(self.probe("check", expected=6)["message"], "service_unavailable")

    def test_cache_tamper_and_plain_http_to_remote_rejected(self):
        _, code = self.store.issue(60)
        self.probe("activate", code=code)
        cache = self.client / ".license" / "session.bin"
        content = bytearray(cache.read_bytes())
        content[-1] ^= 1
        cache.write_bytes(content)
        self.assertFalse(self.probe("check", expected=6)["ok"])
        (self.client / "license-server.json").write_text('{"url":"http://example.com"}')
        self.assertEqual(self.probe("activate", code=code, expected=6)["message"], "unsafe_service_url")

    def test_runtime_extension_and_revocation(self):
        license_id, code = self.store.issue(4)
        activated = self.probe("activate", code=code)
        timer = threading.Timer(1, lambda: self.store.administer("extend", license_id, 30))
        timer.start()
        try:
            self.assertGreater(self.probe("watch", 5)["expires"], activated["expires"])
        finally:
            timer.join()
        timer = threading.Timer(1, lambda: self.store.administer("revoke", license_id))
        timer.start()
        try:
            self.assertEqual(self.probe("watch", 8, expected=6)["message"], "revoked")
        finally:
            timer.join()

    def test_actual_dll_denies_missing_and_revoked_authorization(self):
        if not DLL:
            self.skipTest("full DLL not supplied")
        target = self.client / "phoenix.dll"
        shutil.copyfile(DLL, target)
        def verify_denial(expected):
            process = subprocess.run([str(PROBE), "deny-dll", str(target)], capture_output=True, text=True, timeout=20)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            logs = "\n".join(path.read_text() for path in (self.client / "logs").glob("*.log"))
            self.assertIn("Authorization denied: " + expected, logs)
            self.assertNotIn("Waiting for UWorld", logs)
        verify_denial("activation_required")
        license_id, code = self.store.issue(60)
        self.probe("activate", code=code)
        self.store.administer("revoke", license_id)
        verify_denial("revoked")

    def test_signed_claims_and_tampering(self):
        key = self.store.private_key()
        public = key.public_key().public_numbers()
        public_hex = (public.x.to_bytes(32, "big") + public.y.to_bytes(32, "big")).hex()
        claims = {"product": "phoenix", "license_id": "c" * 32, "device": DEVICE,
            "session_id": "d" * 32, "nonce": NONCE, "server_time": 1800000000,
            "server_time_ms": 1800000000000,
            "iat": 1800000000, "exp": 1800000030, "license_expires_at": 1800000100, "version": 1}
        fixtures = []
        def add(name, token, expected=False, **overrides):
            fixtures.append(dict(name=name, token=token, nonce=NONCE, device=DEVICE,
                                 public=public_hex, start=1000, now=1100, expected=expected) | overrides)
        valid = self.store.sign(claims)
        add("valid", valid, True)
        add("deadline equality", valid, now=31000)
        add("subsecond deadline", self.store.sign(claims | {"server_time_ms": 1800000000999}), now=30001)
        add("nonce mismatch", valid, nonce="f" * 64)
        add("device mismatch", valid, device="f" * 64)
        add("clock inversion", valid, now=999)
        add("signature corrupted", valid[:-20] + ("A" if valid[-20] != "A" else "B") + valid[-19:])
        add("malformed", "abc.def")
        for field, value in [("product", "other"), ("version", 2), ("exp", 1800000900),
                             ("exp", 1800000000), ("license_expires_at", 1800000001),
                             ("server_time", 0), ("iat", 1800000001), ("iat", True), ("exp", 2**64-1)]:
            add(f"invalid {field} {value}", self.store.sign(claims | {field: value}))
        duplicate = json.dumps(claims)[:-1] + ',"product":"phoenix"}'
        message = b64(json.dumps(HEADER).encode()) + "." + b64(duplicate.encode())
        r, s = utils.decode_dss_signature(key.sign(message.encode(), ec.ECDSA(hashes.SHA256())))
        add("duplicate signed field", message + "." + b64(r.to_bytes(32, "big") + s.to_bytes(32, "big")))
        fixture_path = self.root / "fixtures.json"
        fixture_path.write_text(json.dumps(fixtures))
        process = subprocess.run([str(PROBE), "verify", str(fixture_path)], capture_output=True, text=True, timeout=10)
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)

    def test_packaged_loader(self):
        if not LOADER or not DLL:
            self.skipTest("packaged loader and DLL not supplied")
        executable = self.client / "phoenix.exe"
        shutil.copyfile(LOADER, executable)
        shutil.copyfile(DLL, self.client / "phoenix.dll")
        def run(arguments, expected, code=None):
            process = subprocess.run([str(executable), *arguments], input=(code + "\n") if code else None,
                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=20)
            self.assertEqual(process.returncode, expected, process.stdout + process.stderr)
            if code:
                self.assertNotIn(code, process.stdout + process.stderr)
            return process.stdout
        run(["--help"], 0)
        run(["--check"], 0)
        run(["--update"], 5)
        run(["--license-check"], 6)
        _, code = self.store.issue(4)
        self.assertIn("授权有效", run(["--activate"], 0, code))
        run(["--license-check"], 0)
        run(["--license-watch", "8"], 6)
        for path in (self.client / "logs").glob("*.log"):
            self.assertNotIn(code, path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--key-dir", type=Path)
    parser.add_argument("--dll", type=Path)
    parser.add_argument("--loader", type=Path)
    args, remaining = parser.parse_known_args()
    PROBE, KEY_DIR, DLL, LOADER = args.probe, args.key_dir, args.dll, args.loader
    # The decorator runs at import time; enable integration tests after CLI parsing.
    NativeTests.__unittest_skip__ = not bool(PROBE)
    unittest.main(argv=[sys.argv[0], *remaining], verbosity=2)

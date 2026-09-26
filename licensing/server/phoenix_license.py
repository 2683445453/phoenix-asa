"""Portable licensing core and loopback-only development HTTP service.

Run --help for local administration. Production deployment must use a production
WSGI server behind HTTPS; create_app() is the reusable application entry point.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import time
from contextlib import contextmanager, closing
from datetime import datetime, timezone
from wsgiref.simple_server import make_server, WSGIRequestHandler

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

PRODUCT = "phoenix"
MAX_BODY = 8192
HEADER = {"alg": "ES256", "typ": "JWT", "kid": "phoenix-v1"}


class LicenseError(Exception):
    def __init__(self, code: str, status: int = 403):
        self.code, self.status = code, status
        super().__init__(code)


def strict_json(data: bytes | str):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate field")
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=pairs,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError("invalid number")))


def b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def digest(value: str) -> str:
    return hashlib.sha256(value.encode("ascii")).hexdigest()


def text_field(body: dict, name: str, pattern: str) -> str:
    value = body.get(name)
    if not isinstance(value, str) or not re.fullmatch(pattern, value, flags=re.ASCII):
        raise LicenseError("bad_request", 400)
    return value


class Store:
    def __init__(self, directory: Path, clock=time.time, lease_seconds: int = 600):
        self.directory = Path(directory)
        self.clock = clock
        if not 2 <= lease_seconds <= 600:
            raise ValueError("lease seconds must be 2..600")
        self.lease_seconds = lease_seconds
        self.db_path = self.directory / "licenses.sqlite3"
        self.key_path = self.directory / "signing-key.pem"

    def initialize(self):
        self.directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        # Never silently replace a lost signing key for an existing database.
        if not self.key_path.exists():
            if self.db_path.exists():
                raise RuntimeError("Signing key is missing; restore it from backup.")
            key = ec.generate_private_key(ec.SECP256R1())
            pem = key.private_bytes(serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
            fd = os.open(self.key_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "wb") as output:
                output.write(pem)
        with closing(sqlite3.connect(self.db_path)) as conn, conn:
            conn.executescript("""
                CREATE TABLE IF NOT EXISTS licenses (
                    id TEXT PRIMARY KEY, code_hash TEXT UNIQUE NOT NULL,
                    duration INTEGER NOT NULL CHECK(duration > 0),
                    activated_at INTEGER, expires_at INTEGER,
                    revoked INTEGER NOT NULL DEFAULT 0,
                    device TEXT, session_hash TEXT, session_id TEXT);
                CREATE TABLE IF NOT EXISTS state (id INTEGER PRIMARY KEY, last_time INTEGER NOT NULL);
                INSERT OR IGNORE INTO state VALUES (1, 0);
            """)
        self.private_key()  # Validate the key type before exporting or serving.

    def private_key(self):
        key = serialization.load_pem_private_key(self.key_path.read_bytes(), password=None)
        if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
            raise RuntimeError("Expected a P-256 signing key.")
        return key

    @contextmanager
    def transaction(self):
        # Existing state must be initialized explicitly, including in WSGI deployments.
        if not self.db_path.is_file():
            raise RuntimeError("Run init before starting the service.")
        with closing(sqlite3.connect(self.db_path, timeout=10)) as conn, conn:
            conn.row_factory = sqlite3.Row
            conn.execute("BEGIN IMMEDIATE")
            yield conn

    def now(self, conn) -> int:
        current = int(self.clock())
        previous = conn.execute("SELECT last_time FROM state WHERE id=1").fetchone()[0]
        if current + 2 < previous:
            raise LicenseError("clock_rollback", 503)
        current = max(current, previous)
        conn.execute("UPDATE state SET last_time=? WHERE id=1", (current,))
        return current

    def export_public(self, path: Path):
        numbers = self.private_key().public_key().public_numbers()
        public_hex = (numbers.x.to_bytes(32, "big") + numbers.y.to_bytes(32, "big")).hex()
        path.parent.mkdir(parents=True, exist_ok=True)
        content = ('#pragma once\n// Generated public verification key; no private key material.\n'
                   f'#define PHOENIX_LICENSE_PUBLIC_KEY_HEX "{public_hex}"\n')
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            path.write_text(content, encoding="utf-8")

    def issue(self, seconds: int) -> tuple[str, str]:
        if not 1 <= seconds <= 10 * 366 * 86400:
            raise ValueError("Duration must be between one second and ten years.")
        code = "PHX-" + secrets.token_hex(16).upper()
        license_id = secrets.token_hex(16)
        with self.transaction() as conn:
            self.now(conn)
            conn.execute("INSERT INTO licenses(id,code_hash,duration) VALUES (?,?,?)",
                         (license_id, digest(code), seconds))
        return license_id, code

    def administer(self, action: str, license_id: str, seconds: int = 0):
        with self.transaction() as conn:
            now = self.now(conn)
            row = conn.execute("SELECT * FROM licenses WHERE id=?", (license_id,)).fetchone()
            if row is None:
                raise ValueError("Unknown license id.")
            if action == "extend":
                if not 1 <= seconds <= 10 * 366 * 86400:
                    raise ValueError("Invalid extension duration.")
                if row["activated_at"] is None:
                    conn.execute("UPDATE licenses SET duration=duration+? WHERE id=?", (seconds, license_id))
                else:
                    conn.execute("UPDATE licenses SET expires_at=? WHERE id=?",
                                 (max(now, row["expires_at"]) + seconds, license_id))
            elif action in ("revoke", "restore"):
                conn.execute("UPDATE licenses SET revoked=? WHERE id=?", (int(action == "revoke"), license_id))
            elif action == "unbind":
                conn.execute("UPDATE licenses SET device=NULL,session_hash=NULL,session_id=NULL WHERE id=?", (license_id,))
            else:
                raise ValueError("Unknown action.")

    def list_licenses(self):
        with self.transaction() as conn:
            return [dict(row) for row in conn.execute(
                "SELECT id,duration,activated_at,expires_at,revoked,device FROM licenses ORDER BY rowid DESC")]

    def sign(self, claims: dict) -> str:
        encode = lambda obj: b64(json.dumps(obj, separators=(",", ":"), sort_keys=True).encode("ascii"))
        message = encode(HEADER) + "." + encode(claims)
        der = self.private_key().sign(message.encode("ascii"), ec.ECDSA(hashes.SHA256()))
        r, s = utils.decode_dss_signature(der)
        return message + "." + b64(r.to_bytes(32, "big") + s.to_bytes(32, "big"))

    def dispatch(self, path: str, body: dict) -> dict:
        if not isinstance(body, dict) or body.get("product") != PRODUCT:
            raise LicenseError("wrong_product", 400)
        device = text_field(body, "device", r"[0-9a-f]{64}")
        nonce = text_field(body, "nonce", r"[0-9a-f]{64}")
        if path not in ("/v1/activate", "/v1/session/refresh", "/v1/deactivate"):
            raise LicenseError("not_found", 404)
        with self.transaction() as conn:
            now = self.now(conn)
            credential = None
            if path == "/v1/activate":
                code = text_field(body, "code", r"PHX-[A-F0-9]{32}")
                row = conn.execute("SELECT * FROM licenses WHERE code_hash=?", (digest(code),)).fetchone()
                if row is None:
                    raise LicenseError("invalid_code")
            else:
                credential = text_field(body, "session", r"[0-9a-f]{64}")
                row = conn.execute("SELECT * FROM licenses WHERE session_hash=?", (digest(credential),)).fetchone()
                if row is None:
                    raise LicenseError("invalid_session")
            if row["device"] is not None and row["device"] != device:
                raise LicenseError("device_mismatch")
            if path == "/v1/deactivate":
                conn.execute("UPDATE licenses SET device=NULL,session_hash=NULL,session_id=NULL WHERE id=?", (row["id"],))
                return {"deactivated": True}
            if row["revoked"]:
                raise LicenseError("revoked")
            expires = row["expires_at"]
            if expires is not None and now >= expires:
                raise LicenseError("expired")
            if expires is None:
                expires = now + row["duration"]
                conn.execute("UPDATE licenses SET activated_at=?,expires_at=? WHERE id=?", (now, expires, row["id"]))
            if path == "/v1/activate":
                credential, session_id = secrets.token_hex(32), secrets.token_hex(16)
                conn.execute("UPDATE licenses SET device=?,session_hash=?,session_id=? WHERE id=?",
                             (device, digest(credential), session_id, row["id"]))
            else:
                session_id = row["session_id"]
            # Include the subsecond time anchor: truncating it would let clients
            # run almost one extra second beyond an absolute license deadline.
            now_ms = max(now * 1000, int(self.clock() * 1000))
            now = now_ms // 1000
            if now >= expires:
                raise LicenseError("expired")
            claims = {"product": PRODUCT, "license_id": row["id"], "device": device,
                      "session_id": session_id, "nonce": nonce, "server_time": now,
                      "server_time_ms": now_ms,
                      "iat": now, "exp": min(expires, now + self.lease_seconds),
                      "license_expires_at": expires, "version": 1}
            result = {"lease": self.sign(claims)}
            if path == "/v1/activate":
                result["session"] = credential
            return result


def create_app(directory: Path, lease_seconds: int = 600, clock=time.time):
    store = Store(directory, clock, lease_seconds)
    store.private_key()

    def application(environ, start_response):
        status, result = 200, {}
        try:
            path = environ.get("PATH_INFO", "")
            if path == "/health" and environ.get("REQUEST_METHOD") == "GET":
                result = {"product": PRODUCT, "status": "ok", "protocol": 1}
            elif environ.get("REQUEST_METHOD") != "POST":
                raise LicenseError("method_not_allowed", 405)
            else:
                if environ.get("CONTENT_TYPE", "").split(";")[0] != "application/json":
                    raise LicenseError("content_type", 415)
                try:
                    length = int(environ.get("CONTENT_LENGTH", "0"))
                except ValueError:
                    raise LicenseError("bad_request", 400)
                if not 1 <= length <= MAX_BODY:
                    raise LicenseError("body_too_large", 413)
                data = environ["wsgi.input"].read(length)
                if len(data) != length:
                    raise LicenseError("bad_request", 400)
                try:
                    body = strict_json(data)
                except (ValueError, UnicodeError, RecursionError):
                    raise LicenseError("bad_request", 400)
                result = store.dispatch(path, body)
        except LicenseError as error:
            status, result = error.status, {"error": error.code}
        except Exception:
            # Never include request bodies, credentials, database paths or tracebacks.
            status, result = 500, {"error": "service_error"}
        payload = json.dumps(result, separators=(",", ":")).encode("utf-8")
        start_response(f"{status} {'OK' if status == 200 else 'Error'}", [
            ("Content-Type", "application/json"), ("Content-Length", str(len(payload))),
            ("Cache-Control", "no-store"), ("X-Content-Type-Options", "nosniff")])
        return [payload]
    return application


class QuietHandler(WSGIRequestHandler):
    def log_message(self, format, *args):
        pass  # Requests can contain credentials; do not emit request logs.

    def handle(self):
        self.connection.settimeout(5)
        super().handle()


def main():
    parser = argparse.ArgumentParser(description="phoenix local licensing service and administrator")
    parser.add_argument("--data-dir", type=Path, default=Path(__file__).resolve().parents[2] / ".local-license")
    commands = parser.add_subparsers(dest="command", required=True)
    initialize = commands.add_parser("init")
    initialize.add_argument("--public-header", type=Path)
    issue = commands.add_parser("issue")
    issue.add_argument("--seconds", type=int, default=86400)
    commands.add_parser("list")
    for action in ("extend", "revoke", "restore", "unbind"):
        command = commands.add_parser(action)
        command.add_argument("license_id")
        if action == "extend":
            command.add_argument("--seconds", type=int, required=True)
    serve = commands.add_parser("serve")
    serve.add_argument("--port", type=int, default=17891)
    serve.add_argument("--lease-seconds", type=int, default=600)
    args = parser.parse_args()
    store = Store(args.data_dir)
    if args.command == "init":
        store.initialize()
        store.export_public(args.public_header or args.data_dir / "LicensePublicKey.h")
        print("phoenix local licensing initialized; existing keys and licenses preserved.")
    elif args.command == "issue":
        license_id, code = store.issue(args.seconds)
        print(json.dumps({"license_id": license_id, "activation_code": code, "seconds": args.seconds}))
    elif args.command == "list":
        for row in store.list_licenses():
            row["expires_utc"] = (datetime.fromtimestamp(row["expires_at"], timezone.utc).isoformat()
                                  if row["expires_at"] else "starts on activation")
            print(json.dumps(row))
    elif args.command == "serve":
        app = create_app(args.data_dir, args.lease_seconds)
        with make_server("127.0.0.1", args.port, app, handler_class=QuietHandler) as server:
            print(f"phoenix local license service: http://127.0.0.1:{server.server_port}", flush=True)
            server.serve_forever()
    else:
        store.administer(args.command, args.license_id, getattr(args, "seconds", 0))
        print("License updated.")


if __name__ == "__main__":
    main()

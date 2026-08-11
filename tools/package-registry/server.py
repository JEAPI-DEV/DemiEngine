#!/usr/bin/env python3
"""Disposable Demi package registry used for local development and tests."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
from urllib.parse import unquote, urlparse

ROOT = Path(os.environ.get("DEMI_REGISTRY_DATA", "/data")).resolve()
MAX_ARCHIVE = 512 * 1024 * 1024
NAME = re.compile(r"^[a-z0-9][a-z0-9._-]{0,127}$")
VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


class RegistryHandler(BaseHTTPRequestHandler):
    server_version = "DemiDevRegistry/1"

    def reply(self, status, value=None, content_type="application/json"):
        body = b"" if value is None else (json_bytes(value) if content_type == "application/json" else value)
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def parts(self):
        return [unquote(part) for part in urlparse(self.path).path.split("/") if part]

    def package_path(self, name, version=None):
        path = ROOT / "packages" / name
        return path / version if version else path

    def do_GET(self):
        parts = self.parts()
        if parts == ["health"]:
            self.reply(200, {"status": "ok", "format_version": 1})
            return
        if len(parts) == 3 and parts[:2] == ["v1", "packages"] and NAME.fullmatch(parts[2]):
            name, releases = parts[2], []
            root = self.package_path(name)
            if not root.is_dir():
                self.reply(404, {"error": "package_not_found"})
                return
            for version_path in sorted(root.iterdir()):
                manifest_path, archive_path = version_path / "demi.package.json", version_path / "package.demipkg"
                if not manifest_path.is_file() or not archive_path.is_file():
                    continue
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                archive_hash = "sha256:" + hashlib.sha256(archive_path.read_bytes()).hexdigest()
                manifest_hash = "sha256:" + hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
                releases.append({"manifest": manifest, "manifest_hash": manifest_hash, "archive_hash": archive_hash,
                    "archive_url": f"/v1/packages/{name}/{version_path.name}/archive",
                    "yanked": (version_path / "YANKED").exists()})
            self.reply(200, {"format_version": 1, "releases": releases})
            return
        if len(parts) == 5 and parts[:2] == ["v1", "packages"] and parts[4] == "archive":
            name, version = parts[2], parts[3]
            if not NAME.fullmatch(name) or not VERSION.fullmatch(version):
                self.reply(400, {"error": "invalid_identity"})
                return
            path = self.package_path(name, version) / "package.demipkg"
            if not path.is_file():
                self.reply(404, {"error": "release_not_found"})
                return
            data = path.read_bytes()
            self.reply(200, data, "application/octet-stream")
            return
        self.reply(404, {"error": "not_found"})

    def do_PUT(self):
        parts = self.parts()
        if len(parts) != 5 or parts[:2] != ["v1", "packages"] or parts[4] not in ("manifest", "archive"):
            self.reply(404, {"error": "not_found"})
            return
        name, version, kind = parts[2], parts[3], parts[4]
        if not NAME.fullmatch(name) or not VERSION.fullmatch(version):
            self.reply(400, {"error": "invalid_identity"})
            return
        try:
            length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            length = -1
        limit = 8 * 1024 * 1024 if kind == "manifest" else MAX_ARCHIVE
        if length < 0 or length > limit:
            self.reply(413, {"error": "invalid_size"})
            return
        destination = self.package_path(name, version)
        if destination.exists():
            self.reply(409, {"error": "immutable_release_exists"})
            return
        staging = ROOT / "staging" / name / version
        staging.mkdir(parents=True, exist_ok=True)
        data = self.rfile.read(length)
        if len(data) != length:
            self.reply(400, {"error": "truncated_body"})
            return
        if kind == "manifest":
            try:
                manifest = json.loads(data)
            except (UnicodeDecodeError, json.JSONDecodeError):
                self.reply(400, {"error": "invalid_manifest"})
                return
            if manifest.get("format_version") != 1 or manifest.get("name") != name or manifest.get("version") != version:
                self.reply(400, {"error": "manifest_identity_mismatch"})
                return
            (staging / "demi.package.json").write_bytes(json_bytes(manifest))
            self.reply(201, {"status": "manifest_staged"})
            return
        manifest_path = staging / "demi.package.json"
        if not manifest_path.is_file():
            self.reply(409, {"error": "manifest_required_first"})
            return
        (staging / "package.demipkg").write_bytes(data)
        destination.parent.mkdir(parents=True, exist_ok=True)
        os.replace(staging, destination)
        self.reply(201, {"status": "published", "archive_hash": "sha256:" + hashlib.sha256(data).hexdigest()})

    def do_DELETE(self):
        if self.parts() != ["v1", "test", "reset"]:
            self.reply(404, {"error": "not_found"})
            return
        shutil.rmtree(ROOT / "packages", ignore_errors=True)
        shutil.rmtree(ROOT / "staging", ignore_errors=True)
        self.reply(200, {"status": "reset"})

    def log_message(self, pattern, *args):
        print(f"{self.address_string()} {pattern % args}", flush=True)


if __name__ == "__main__":
    ROOT.mkdir(parents=True, exist_ok=True)
    ThreadingHTTPServer(("0.0.0.0", int(os.environ.get("PORT", "8080"))), RegistryHandler).serve_forever()

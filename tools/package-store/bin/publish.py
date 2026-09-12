#!/usr/bin/env python3
"""Publish a Demi archive over HTTPS using a key file, never SSH."""
import argparse
import http.client
import json
import os
from pathlib import Path
import secrets
import ssl
import sys
from urllib.parse import urlsplit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--key-file", type=Path,
                        default=Path(os.environ.get("DEMI_PUBLISH_KEY", str(Path.home()/".config/demi-store/publish-token"))))
    parser.add_argument("--registry", default="https://demiengine.de")
    args = parser.parse_args()
    url = urlsplit(args.registry)
    if url.scheme != "https" or not url.hostname or url.username or url.password or url.query or url.fragment:
        parser.error("registry must be an HTTPS URL without credentials")
    key = args.key_file.read_text().strip()
    if len(key) != 64 or any(c not in "0123456789abcdef" for c in key):
        parser.error("invalid publishing key file")
    metadata = json.loads(args.metadata.read_text())
    files = [("archive", args.archive)]
    images = []
    for reference in metadata.get("images", []):
        if reference.startswith("https://"):
            images.append(reference)
        else:
            image = args.metadata.parent/reference
            files.append(("images[]", image))
            images.append(image.name)
    if len(set(p.name for _, p in files[1:])) != len(files[1:]):
        parser.error("preview filenames must be unique")
    metadata["images"] = images
    boundary = "Demi"+secrets.token_hex(16)
    parts = []
    encoded = json.dumps(metadata).encode()
    parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="metadata"\r\nContent-Type: application/json\r\n\r\n'.encode()+encoded+b"\r\n")
    for field, path in files:
        if not path.is_file() or any(c in path.name for c in '"\r\n'):
            parser.error("invalid upload filename")
        parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{field}"; filename="{path.name}"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode())
        parts.append(path)
        parts.append(b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode())
    size = sum(p.stat().st_size if isinstance(p, Path) else len(p) for p in parts)
    connection = http.client.HTTPSConnection(url.hostname, url.port, timeout=300, context=ssl.create_default_context())
    try:
        connection.putrequest("POST", url.path.rstrip("/")+"/api/publishing/releases")
        connection.putheader("Authorization", "Bearer "+key)
        connection.putheader("Content-Type", "multipart/form-data; boundary="+boundary)
        connection.putheader("Content-Length", str(size))
        connection.endheaders()
        for part in parts:
            if isinstance(part, Path):
                with part.open("rb") as stream:
                    while chunk := stream.read(65536):
                        connection.send(chunk)
            else:
                connection.send(part)
        response = connection.getresponse()
        body = response.read(65536)
        try:
            result = json.loads(body)
        except ValueError:
            raise RuntimeError(f"Server returned HTTP {response.status}") from None
        if response.status != 201:
            raise RuntimeError(result.get("error", f"HTTP {response.status}"))
        print(f"Published {result['name']}@{result['version']}")
        print(args.registry.rstrip("/")+result["url"])
    finally:
        connection.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Publication failed: {error}", file=sys.stderr)
        sys.exit(1)


#!/usr/bin/env python3
"""Compare staged archives with hosted releases before announcing a package update."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from urllib.parse import quote, urlsplit
from urllib.request import urlopen


def verify(registry: str, staged: Path) -> int:
    archives = sorted(staged.glob("packages/*/*/package.demipkg"))
    if not archives:
        raise ValueError("No staged package archives found")
    failures = 0
    for archive in archives:
        name = archive.parent.parent.name
        version = archive.parent.name
        try:
            with urlopen(f"{registry}/v1/packages/{quote(name, safe='')}", timeout=30) as response:
                releases = json.load(response)["releases"]
            release = next((item for item in releases if item["manifest"]["version"] == version), None)
            if release is None:
                raise ValueError(f"version {version} is not published")
            with archive.open("rb") as source:
                expected = "sha256:" + hashlib.file_digest(source, "sha256").hexdigest()
            if release["archive_hash"] != expected:
                raise ValueError("published archive hash differs from staged content")
            print(f"OK {name}@{version}: published content matches staging")
        except (OSError, ValueError, KeyError) as error:
            print(f"FAIL {name}@{version}: {error}", file=sys.stderr)
            failures += 1
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staged_registry", type=Path)
    parser.add_argument("--registry", default="https://demiengine.de")
    args = parser.parse_args()
    url = urlsplit(args.registry)
    if url.scheme != "https" or not url.hostname or url.username or url.password or url.query or url.fragment:
        parser.error("registry must be an HTTPS URL without credentials")
    try:
        return verify(args.registry.rstrip("/"), args.staged_registry)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    sys.exit(main())

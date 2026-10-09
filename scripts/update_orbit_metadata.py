#!/usr/bin/env python3
"""Refresh Orbit's Lua argument metadata from its authoritative UI prefabs."""
import argparse
import json
from pathlib import Path


def lua(value):
    if isinstance(value, dict):
        return "{" + ",".join(f"[{lua(k)}]={lua(v)}" for k, v in sorted(value.items())) + "}"
    if isinstance(value, list):
        return "{" + ",".join(map(lua, value)) + "}"
    return json.dumps(value, ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / "packages/sources/demi.ui.orbit"
    lines = ["-- Generated parameter metadata; run scripts/update_orbit_metadata.py.", "return {"]
    for path in sorted((root / "ui/orbit").glob("*.ui.prefab.json")):
        document = json.loads(path.read_text())
        kind = document["id"].removeprefix("ui-prefab://orbit/")
        lines.append(f"  [{lua(kind)}] = {lua({'parameters': document.get('parameters', {})})},")
    icons = {p.stem: True for p in sorted((root / "assets/icons").glob("*.svg"))}
    lines.append(f"  _icons = {lua(icons)},")
    lines.append("}\n")
    expected = "\n".join(lines)
    output = root / "scripts/demi/ui/orbit/templates.lua"
    if args.check:
        if output.read_text() != expected:
            raise SystemExit("Orbit metadata is stale; run scripts/update_orbit_metadata.py")
    else:
        output.write_text(expected)


if __name__ == "__main__":
    main()

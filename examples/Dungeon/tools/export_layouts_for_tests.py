"""Feed the actual authored JSON layouts to standalone Lua rule tests.

The runtime uses demi.data. This test adapter uses Python's JSON parser instead
of duplicating the maps or adding a second JSON parser to gameplay.
"""
import json
from pathlib import Path


def lua(value):
    if value is None:
        return "nil"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, list):
        return "{" + ",".join(lua(item) for item in value) + "}"
    return "{" + ",".join("[" + lua(key) + "]=" + lua(item) for key, item in value.items()) + "}"


root = Path(__file__).resolve().parents[1]
documents = {}
for name in ("floor1", "floor2", "floor3", "boss1"):
    source = root / "assets" / "dungeon" / "maps" / name / (name + ".tilemap.json")
    documents["asset://dungeon/layouts/" + name] = json.loads(source.read_text())
print("return " + lua(documents))

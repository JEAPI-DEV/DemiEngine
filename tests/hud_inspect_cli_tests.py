import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="demi-hud-inspect-") as directory:
    root = Path(directory)
    (root / "ui").mkdir()
    (root / "demi.project.json").write_text('{"format_version":1}')
    (root / "ui/panel.ui.prefab.json").write_text(json.dumps({
        "format_version": 1, "id": "ui-prefab://panel", "root": {
            "id": "panel", "type": "panel", "size": [300, 100],
            "children": [{"id": "caption", "type": "label", "text": "Inherited"}]
        }}))
    (root / "theme.json").write_text('{"styles":{"wide":{"min_size":[320,120]}}}')
    hud = {"format_version": 1, "canvas_size": [960,540], "theme": "theme.json", "children": [{
        "id": "instance", "prefab": "ui-prefab://panel",
        "overrides": {"$root": {"blocks_pointer": True, "style": "wide", "size": None}}
    }]}
    path = root / "test.hud.json"
    path.write_text(json.dumps(hud))
    command = [sys.argv[1], "hud", "inspect", str(path), "--format", "json"]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    nodes = {n["id"]: n for n in json.loads(result.stdout)["nodes"]}
    assert "instance.caption" in nodes, result.stdout
    panel = nodes["instance"]
    assert panel["type"] == "panel" and panel["blocks_pointer"] and not panel["focusable"]
    assert panel["resolved"]["width"] == 320 and panel["resolved"]["height"] == 120, panel
    hud["children"][0]["overrides"]["$root"]["blocks_pointer"] = "yes"
    path.write_text(json.dumps(hud))
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode != 0 and "blocks_pointer" in result.stderr, result.stderr
print("HUD inspect resolves prefab content, theme layout and validation")

"""Exercise the real CLI/runtime headless E2E loop, including bounded failures."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="demi-headless-e2e-") as directory:
    root = Path(directory)
    (root / "scenes").mkdir()
    (root / "scripts/tests").mkdir(parents=True)
    (root / "demi.project.json").write_text(json.dumps({
        "format_version": 1, "name": "Headless E2E", "main_scene": "scene://main",
        "scenes": [{"id": "scene://main", "path": "scenes/main.scene.json"}],
    }))
    (root / "scenes/main.scene.json").write_text(json.dumps({
        "format_version": 1, "id": "scene://main", "hud": "main.hud.json", "entities": [],
    }))
    (root / "scenes/main.hud.json").write_text(json.dumps({
        "format_version": 1, "canvas_size": [320, 180], "children": [
            {"id": "toggle", "type": "toggle", "text": "Toggle", "at": [10, 10], "size": [180, 40]},
        ],
    }))
    suite = root / "scripts/tests/e2e.lua"
    env = dict(os.environ, DEMI_HEADLESS="1", XDG_DATA_HOME=str(root / "data"))

    def run(body, *flags):
        suite.write_text('local Test = require("demi.test")\nreturn {tests={{name="probe",func=function()\n'
                         + body + '\nend}}}\n')
        result = subprocess.run([binary, "run", "--project", str(root), "--e2e-tests", *flags],
                                env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=15)
        assert result.stdout.count("SUMMARY") == 1, result.stdout
        return result

    passed = run('Test.expect_scene("scene://main"); Test.wait(0.3); Test.expect(true, "wait completed")')
    assert passed.returncode == 0 and "SUMMARY passed=1 failed=0" in passed.stdout, passed.stdout
    cli = subprocess.run([binary, "test", "linux", "--project", str(root)], env=env,
                         text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    assert cli.returncode == 0 and "passed=1 failed=0" in cli.stdout, cli.stdout
    touched = run('local Hud = require("demi.hud"); Test.wait(0.1); '
                  'Test.touch("toggle"); Test.wait(0.1); local checked = false; '
                  'for _, node in ipairs(Hud.accessibility_snapshot()) do '
                  'if node.id == "toggle" then checked = node.checked end end; '
                  'Test.expect(checked, "synthetic touch toggled the HUD control")')
    assert touched.returncode == 0 and "SUMMARY passed=1 failed=0" in touched.stdout, touched.stdout
    failed = run('Test.wait(0.1); Test.expect(false, "expected failure")')
    assert failed.returncode != 0 and "SUMMARY passed=0 failed=1" in failed.stdout, failed.stdout
    bounded = run('Test.wait(30)', "--max-frames", "2")
    assert bounded.returncode != 0 and "SUMMARY passed=0 failed=1" in bounded.stdout, bounded.stdout
    missing = run('Test.expect_scene("scene://missing", 0.1)')
    assert missing.returncode != 0 and "SUMMARY passed=0 failed=1" in missing.stdout, missing.stdout
print("Headless E2E runtime checks passed")

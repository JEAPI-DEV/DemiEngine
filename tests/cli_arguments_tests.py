"""Exercise user-visible CLI parsing without opening a window or packaging."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


cli = Path(sys.argv[1]).resolve()
runtime = Path(sys.argv[2]).resolve()
editor = Path(sys.argv[3]).resolve()


def run(arguments, expected=0, executable=cli, environment=None):
    result = subprocess.run(
        [str(executable), *arguments],
        text=True,
        capture_output=True,
        timeout=30,
        env=environment,
    )
    assert result.returncode == expected, (
        arguments,
        result.returncode,
        result.stdout,
        result.stderr,
    )
    return result


for arguments, token in [
    ([], "DemiEngine"),
    (["--help"], "terrain"),
    (["terrain", "--help"], "apply-preset"),
    (["terrain", "explain", "--help"], "--at"),
    (["asset", "--help"], "register-generated"),
    (["asset", "import", "--help"], "--meters-per-unit"),
    (["package", "--help"], "install"),
    (["package", "install", "--help"], "--locked"),
    (["prefab", "--help"], "inspect"),
    (["prefab", "inspect", "--help"], "path"),
    (["build", "--help"], "--configuration"),
    (["run", "--help"], "--window-size"),
    (["run", "android", "--help"], "--serial"),
    (["test", "linux", "--help"], "--timeout"),
    (["editor", "--help"], "--max-frames"),
]:
    assert token in run(arguments).stdout, arguments

for executable in [runtime, editor]:
    assert "--project" in run(["--help"], executable=executable).stdout
    for arguments in [["--unknown"], ["--max-frames", "1junk"], ["--max-frames", "-1"]]:
        assert run(arguments, 2, executable).stderr

# These fail at the argument boundary, before any project I/O or runtime work.
for arguments in [
    ["unknown-command"],
    ["validate", "--unknown"],
    ["validate", "--platform", "unknown"],
    ["validate", "--format"],
    ["run", "--max-frames", "12frames"],
    ["run", "--max-frames", "-1"],
    ["run", "--profile-frames"],
    ["run", "--window-size", "800x0"],
    ["run", "other-platform"],
    ["build", "other-platform"],
    ["build", "linux", "--configuration", "other"],
    ["cook", "--platform", "other"],
    ["doctor", "--format", "other"],
    ["new", "destination"],
    ["new", "--name", "missing-template"],
    ["new", "--list", "destination", "--template", "minimal-2d"],
    ["hud", "inspect"],
    ["hud", "inspect", "missing", "--safe-area", "1,2,3,bad"],
    ["scene", "diff", "only-one"],
    ["prefab", "unknown", "source"],
    ["prefab", "inspect"],
    ["terrain", "seeds", "12junk"],
    ["terrain", "inspect", "missing", "--unknown"],
    ["terrain", "explain", "missing"],
    ["terrain", "apply-preset", "missing"],
    ["terrain", "compact", "missing", "--unknown"],
    ["asset", "inspect", "source", "deps", "source"],
    ["asset", "import", "source"],
    ["asset", "import", "source", "--id", "asset://test", "--meters-per-unit", "1junk"],
    ["asset", "lod", "source", "--medium-ratio", "half"],
    ["asset", "collider", "source", "--body", "static"],
    ["asset", "collider", "source", "--recommend", "--detail", "2"],
    ["asset", "export", "--output", "unused"],
    ["package", "list", "outdated"],
    ["package", "add"],
    ["package", "install", "--unknown"],
    ["capabilities", "check", "--format", "other"],
    ["script", "check"],
    ["test", "linux", "--timeout", "nan"],
    ["test", "linux", "--max-frames", "0"],
]:
    assert run(arguments, 2).stderr, arguments

# Positional/option order, negative numeric positionals, and comma splitting.
seed = json.loads(run(["terrain", "seeds", "--format", "json", "-12"]).stdout)
assert seed["world_seed"] == -12
assert "DemiEngine" in run(["version"]).stdout
assert "blank-2d" in run(["new", "--list"]).stdout

with tempfile.TemporaryDirectory(prefix="demi cli arguments ") as temporary:
    root = Path(temporary)
    schema = root / "component schema.json"
    run(["schema", "export", str(schema)])
    document = json.loads(schema.read_text())
    assert document["type"] == "object"
    assert "Transform2D" in document["properties"]

    # Capture the actual execvp handoff instead of invoking adb or a phone.
    binary = root / "bin"
    binary.mkdir()
    python = binary / "python3"
    python.write_text(
        f"#!{sys.executable}\n"
        "import json, sys\n"
        "print(json.dumps(sys.argv[1:]))\n"
    )
    python.chmod(0o755)
    project = root / "project with spaces"
    project.mkdir()
    (project / "demi.project.json").write_text('{"format_version":1}')
    environment = dict(os.environ, PATH=f"{binary}{os.pathsep}{os.environ['PATH']}")
    for verb, mode in [("run", "run"), ("test", "qualify")]:
        captured = json.loads(
            run(
                [verb, "android", "--serial", "phone serial", "--project", str(project),
                 "--watch", "--settle-seconds", "0.5", "--output", str(root / "phone output")],
                environment=environment,
            ).stdout
        )
        assert captured[0].endswith("scripts/android_device.py")
        assert captured[1] == mode
        assert captured[captured.index("--project") + 1] == str(project / "demi.project.json")
        assert captured[captured.index("--serial") + 1] == "phone serial"
        assert captured[captured.index("--demi-executable") + 1] == str(cli)
        assert captured[captured.index("--output") + 1] == str(root / "phone output")
        assert float(captured[captured.index("--settle-seconds") + 1]) == 0.5
        assert "--watch" in captured

print("CLI argument workflows passed.")

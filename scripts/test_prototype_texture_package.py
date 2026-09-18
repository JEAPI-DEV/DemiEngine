#!/usr/bin/env python3
"""Exercise the real local publish/install/cook path for the Kenney asset pack."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demi", default="build/linux-release/demi")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    demi = (repo / args.demi).resolve()
    package = repo / "packages/sources/kenney.textures.prototype"
    manifest = json.loads((package / "demi.package.json").read_text())
    assets = [json.loads((package / p).read_text()) for p in manifest["asset_manifests"]]
    assert len(assets) == len({a["id"] for a in assets}) == 78
    for path, asset in zip(manifest["asset_manifests"], assets):
        image = package / Path(path).parent / asset["source"]
        assert image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert image.relative_to(package).as_posix() in manifest["files"]
        assert "generated_output" not in asset

    def run(*argv, **kwargs):
        subprocess.run([str(demi), *map(str, argv)], check=True, timeout=120, **kwargs)

    # Only this freshly created temporary directory is cleaned up on exit.
    with tempfile.TemporaryDirectory(prefix="demi-prototype-package-") as temp:
        root = Path(temp)
        project = root / "game"
        project.mkdir()
        (project / "scenes").mkdir()
        (project / "demi.project.json").write_text(json.dumps({
            "format_version": 1, "name": "Prototype textures package probe",
            "main_scene": "scene://probe/main", "scenes": [{"id": "scene://probe/main"}]
        }))
        entities = [{"id": "camera", "components": {
            "Transform3D": {"position": [0, 0, 5]},
            "Camera3D": {"target_offset": [0, 0, -5]}
        }}]
        for index, asset in enumerate(assets):
            entities.append({"id": f"tile_{index}", "components": {
                "Transform3D": {"position": [index % 13, index // 13, 0]},
                "MeshRenderer": {"shape": "cube", "texture": asset["id"]}
            }})
        (project / "scenes/main.scene.json").write_text(json.dumps({
            "format_version": 1, "id": "scene://probe/main", "entities": entities
        }))
        run("package", "publish", package, "--registry", root / "registry")
        # Project discovery from cwd; no manual manifests or startup preloads.
        run("package", "add", "kenney.textures.prototype@1.0.0",
            "--registry", root / "registry", cwd=project)
        run("package", "install", "--locked", "--offline", cwd=project)
        run("validate", project)
        for platform in ("linux", "android"):
            output = root / f"cooked-{platform}"
            run("cook", "--project", project, "--platform", platform, "--output", output)
            cooked = json.loads((output / "cook.manifest.json").read_text())
            assert {a["asset"] for a in cooked["assets"]} == {a["id"] for a in assets}
            assert all(a["source_package"] == manifest["name"] for a in cooked["assets"])
            if platform == "linux":
                run("run", "--project", output, "--max-frames", "3",
                    env={**os.environ, "DEMI_HEADLESS": "1"})
        run("run", "--project", project, "--max-frames", "3",
            env={**os.environ, "DEMI_HEADLESS": "1"})
    print("PASS: 78 exported textures, offline install, Linux/Android cook, headless runtime")


if __name__ == "__main__":
    main()

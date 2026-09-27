"""Validate actual website examples through Demi's native composition services."""

import argparse
import html
import json
from pathlib import Path
import re
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def write_json(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    repository = Path(__file__).resolve().parents[1]
    template = repository / "tools/package-store/templates/docs/content/prefab-json.html.twig"
    examples = {}
    for name, body in re.findall(
        r'<pre data-engine-example="([^"]+)"><code>(.*?)</code></pre>',
        template.read_text(encoding="utf-8"), re.DOTALL,
    ):
        text = html.unescape(body).strip()
        examples[name] = json.loads("{" + text + "}" if text.startswith('"') else text)
    require(set(examples) == {"prefab-player", "prefab-house", "prefab-house-children", "scene-overrides"},
            "Named prefab examples missing or renamed; update their regression coverage.")

    with tempfile.TemporaryDirectory(prefix="demi-doc-prefabs-") as temporary:
        project = Path(temporary)

        def run(*arguments):
            result = subprocess.run([str(binary), *map(str, arguments)], cwd=project,
                                    capture_output=True, text=True, timeout=30)
            require(result.returncode == 0, result.stdout + result.stderr)
            return result.stdout

        write_json(project / "demi.project.json", {
            "format_version": 1, "name": "Documentation examples",
            "main_scene": "scene://docs/main",
            "scenes": [{"id": "scene://docs/main", "path": "scenes/main.scene.json"}],
        })
        for name in ("prefab-player", "prefab-house", "prefab-house-children"):
            document = examples[name]
            relative = document["id"].removeprefix("prefab://")
            write_json(project / "prefabs" / (relative + ".prefab.json"), document)
        # The prose explicitly defines this dependency as a wall with local ID wall.
        write_json(project / "prefabs/props/wall.prefab.json", {
            "format_version": 1, "id": "prefab://props/wall",
            "entities": [{"id": "wall", "components": {"Transform2D": {}}}],
        })
        scene = {"format_version": 1, "id": "scene://docs/main", **examples["scene-overrides"]}
        scene["entities"].extend([
            {"id": "house", "prefab": "prefab://props/house"},
            {"id": "grouped", "prefab": "prefab://props/house_grouped"},
        ])
        path = project / "scenes/main.scene.json"
        write_json(path, scene)
        run("asset", "import", repository / "images/demi_engine.png",
            "--project", project, "--id", "asset://textures/hero")
        run("validate", project)
        expanded = json.loads(run("scene", "expand", path))
        entities = {entity["id"]: entity for entity in expanded["entities"]}
        require("player" not in entities, "Placement unexpectedly produced a runtime wrapper")
        require(entities["player/body"]["components"]["Transform2D"]["position"] == [3, 2],
                "Documented exact-field override failed")
        sprite = entities["player/body"]["components"]["Sprite"]
        require(sprite["color"] == [1, .5, .5, 1] and sprite["texture"] == "asset://textures/hero",
                "Nested component merge lost the inherited texture or color")
        require(entities["player/shadow"]["components"]["Transform2D"]["parent"] == "player/body",
                "Ordinary children changed stable IDs or lost parenting")
        require("house/floor" in entities and "house/wall/wall" in entities,
                "Mixed entities/prefab entries did not expand")
        require(entities["grouped/wall/wall"]["components"]["Transform2D"]["parent"] == "grouped/house_root",
                "Prefab placement under children lost its authored owner")
    print("Website prefab examples validated and expanded by the engine.")


if __name__ == "__main__":
    main()

"""Check direct native-service call names in published code examples.

This is deliberately not a Lua parser or a signature/execution test. It follows
explicit local require aliases across a page's examples and compares calls with
the checked-in native metadata. Package modules and instance methods need their
own runtime tests/review.
"""

from pathlib import Path
import re


def main():
    repository = Path(__file__).resolve().parents[1]
    content = repository / "tools/package-store/templates/docs/content"
    block_pattern = re.compile(r"<pre\b[^>]*><code\b[^>]*>(.*?)</code></pre>", re.DOTALL)
    import_pattern = re.compile(r'''local\s+(\w+)\s*=\s*require\(["'](demi\.[\w.]+)["']\)''')
    export_pattern = re.compile(r"function\s+\w+\.(\w+)\s*\(")
    exports = {}
    failures = []
    calls = 0
    for path in sorted(content.glob("*.html.twig")):
        aliases = {}
        for block in block_pattern.findall(path.read_text(encoding="utf-8")):
            for alias, module in import_pattern.findall(block):
                stub = repository / "scripts/stubs" / (module.replace(".", "/") + ".lua")
                aliases[alias] = module if stub.is_file() else None
                if stub.is_file() and module not in exports:
                    exports[module] = set(export_pattern.findall(stub.read_text(encoding="utf-8")))
            for alias, module in aliases.items():
                if module is None:
                    continue
                for name in re.findall(r"\b" + re.escape(alias) + r"\.(\w+)\s*\(", block):
                    calls += 1
                    if name not in exports[module]:
                        failures.append(f"{path.name}: {module}.{name} is not exported")
    if failures:
        raise AssertionError("\n".join(failures))
    if not calls:
        raise AssertionError("No native-service references were checked")
    print(f"Checked {calls} native-service call names in website examples.")


if __name__ == "__main__":
    main()

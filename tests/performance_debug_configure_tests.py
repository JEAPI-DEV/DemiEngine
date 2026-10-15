#!/usr/bin/env python3
"""Configure-only coverage; no engine dependencies, compilation or native run."""
import pathlib
import re
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
STAGES = (
    "terrain_materials", "terrain_normals", "terrain_macro",
    "terrain_triplanar", "terrain_blending", "shadow_receive",
)


def configure(source, build, *options, succeeds=True):
    result = subprocess.run(
        ["cmake", "-S", str(source), "-B", str(build), *options],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        check=False,
    )
    assert (result.returncode == 0) == succeeds, result.stdout
    if not succeeds:
        assert "Unknown DEMI_PERFORMANCE_DEBUG_DISABLE stage" in result.stdout
        return {}
    generated = build / "generated/demi/runtime/render"
    header = (generated / "PerformanceDebug.h").read_text()
    assert header == (generated / "demi_performance_debug.sh").read_text()
    assert "@" not in header
    return dict(re.findall(r"^#define (DEMI_PERFORMANCE_DEBUG\w*) (.+)$",
                           header, re.MULTILINE))


def verify(config, enabled, selected):
    assert config["DEMI_PERFORMANCE_DEBUG"] == str(int(enabled))
    assert config["DEMI_PERFORMANCE_DEBUG_DISABLED_MASK"] == str(
        sum(1 << index for index, stage in enumerate(STAGES) if stage in selected))
    assert config["DEMI_PERFORMANCE_DEBUG_DISABLED_NAMES"] == (
        '"' + ",".join(stage for stage in STAGES if stage in selected) + '"')
    for stage in STAGES:
        assert config["DEMI_PERFORMANCE_DEBUG_DISABLE_" + stage.upper()] == str(
            int(stage in selected))


def main():
    with tempfile.TemporaryDirectory(prefix="demi-performance-config-") as directory:
        temporary = pathlib.Path(directory)
        source = temporary / "source"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\n"
            "project(PerformanceDebugConfiguration LANGUAGES NONE)\n"
            f'include("{ROOT / "cmake/DemiPerformanceDebug.cmake"}")\n')
        build = temporary / "build"
        verify(configure(source, build), False, ())
        verify(configure(source, build, "-DDEMI_PERFORMANCE_DEBUG=ON"), True, ())
        selected = ("terrain_materials", "shadow_receive")
        verify(configure(source, build,
                         "-DDEMI_PERFORMANCE_DEBUG_DISABLE=shadow_receive;terrain_materials;shadow_receive"),
               True, selected)
        verify(configure(source, build,
                         "-DDEMI_PERFORMANCE_DEBUG_DISABLE=" + ";".join(STAGES)),
               True, STAGES)
        # The cached stage list survives master OFF, but every effective flag resets.
        verify(configure(source, build, "-DDEMI_PERFORMANCE_DEBUG=OFF"), False, ())
        verify(configure(source, build, "-DDEMI_PERFORMANCE_DEBUG_DISABLE=typo"),
               False, ())
        configure(source, build, "-DDEMI_PERFORMANCE_DEBUG=ON", succeeds=False)
    print("Performance diagnostic configure checks passed")


if __name__ == "__main__":
    main()

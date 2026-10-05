#!/usr/bin/env python3
"""Capture only a newly launched Demi editor window, never the desktop.

Uses X11/XWayland, xdotool and ImageMagick. The editor runs with disposable XDG
settings so capture layout and caches do not affect your regular editor setup.
The output directory receives a PNG, editor log and capture provenance JSON.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


EDITOR_TITLE_PREFIX = "Demi Engine Editor - "


def checked_command(arguments: list[str]) -> str:
    result = subprocess.run(arguments, check=True, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=15)
    return result.stdout.strip()


def verify_editor_window(window: str, process: subprocess.Popen,
                         binary: Path) -> str:
    if process.poll() is not None:
        raise RuntimeError("The editor exited before capture; inspect its log.")
    executable = Path(f"/proc/{process.pid}/exe").resolve(strict=True)
    if executable != binary:
        raise RuntimeError("The launched process no longer owns the expected binary.")
    owner = checked_command(["xdotool", "getwindowpid", window])
    title = checked_command(["xdotool", "getwindowname", window])
    if owner != str(process.pid) or not title.startswith(EDITOR_TITLE_PREFIX):
        raise RuntimeError("Refusing to capture a window not owned by this editor.")
    return title


def wait_for_window(process: subprocess.Popen, binary: Path,
                    timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("The editor exited before creating a window; inspect its log.")
        result = subprocess.run(
            ["xdotool", "search", "--all", "--onlyvisible", "--pid",
             str(process.pid), "--name", "^" + EDITOR_TITLE_PREFIX],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=5)
        if result.returncode not in (0, 1):
            raise RuntimeError("Window discovery failed: " + result.stderr.strip())
        windows = result.stdout.split()
        if len(windows) > 1:
            raise RuntimeError("More than one visible editor window matches; refusing an ambiguous capture.")
        if windows:
            verify_editor_window(windows[0], process, binary)
            return windows[0]
        time.sleep(0.1)
    raise TimeoutError("No verified editor window appeared before the timeout.")


def settle_editor(process: subprocess.Popen, seconds: float) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("The editor exited while waiting for its view to render.")
        time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))


def stop_editor(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def capture(arguments: argparse.Namespace) -> Path:
    binary = arguments.binary.resolve(strict=True)
    project = arguments.project.resolve(strict=True)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise ValueError("--binary must be an executable editor binary.")
    if not project.is_dir() and project.name != "demi.project.json":
        raise ValueError("--project must be a project directory or demi.project.json.")
    if not os.environ.get("DISPLAY"):
        raise RuntimeError("X11/XWayland DISPLAY is required for window-only capture.")
    for executable in ("xdotool", "import", "identify"):
        if shutil.which(executable) is None:
            raise RuntimeError(f"Required capture tool is unavailable: {executable}")
    output = arguments.output.resolve()
    if output.suffix.lower() != ".png":
        raise ValueError("--output must name a PNG file.")
    log_path = output.with_suffix(".log")
    report_path = output.with_suffix(".capture.json")
    for destination in (output, log_path, report_path):
        if destination.exists():
            raise FileExistsError(f"Choose a new output path; refusing to overwrite {destination}")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(binary), "--project", str(project)]
    if arguments.open_source is not None:
        command.extend(["--open", str(arguments.open_source)])
    if arguments.terrain_graph:
        command.append("--terrain-graph")
    if arguments.terrain_settings_node is not None:
        command.extend(["--terrain-settings-node", arguments.terrain_settings_node])

    with tempfile.TemporaryDirectory(prefix="demi-editor-capture-") as task_state:
        environment = os.environ.copy()
        environment.pop("DEMI_HEADLESS", None)
        for variable, directory in (("XDG_DATA_HOME", "data"),
                                    ("XDG_CONFIG_HOME", "config"),
                                    ("XDG_CACHE_HOME", "cache")):
            state_path = Path(task_state) / directory
            state_path.mkdir()
            environment[variable] = str(state_path)
        if arguments.video_driver != "auto":
            environment["SDL_VIDEODRIVER"] = arguments.video_driver
        with log_path.open("x") as log:
            process = subprocess.Popen(command, env=environment,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                print(f"Waiting for editor PID {process.pid}...", flush=True)
                window = wait_for_window(process, binary, arguments.window_timeout)
                checked_command(["xdotool", "windowactivate", "--sync", window])
                if arguments.width is not None:
                    checked_command(["xdotool", "windowsize", "--sync", window,
                                     str(arguments.width), str(arguments.height)])
                settle_editor(process, arguments.settle_seconds)
                title = verify_editor_window(window, process, binary)
                # No -screen, root window, desktop region or other-window fallback.
                checked_command(["import", "-window", window, "png:" + str(output)])
                dimensions = checked_command(["identify", "-format", "%w %h", str(output)])
                width, height = map(int, dimensions.split())
                if width <= 0 or height <= 0:
                    raise RuntimeError("The window capture has invalid dimensions.")
                report = {
                    "captured_at": datetime.now(timezone.utc).isoformat(),
                    "capture_method": "ImageMagick import -window",
                    "window_title": title,
                    "window_id": window,
                    "editor_pid": process.pid,
                    "binary": binary.name,
                    "video_driver": environment.get("SDL_VIDEODRIVER", "auto"),
                    "isolated_xdg_settings": True,
                    "width": width,
                    "height": height,
                    "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                }
                with report_path.open("x") as report_file:
                    json.dump(report, report_file, indent=2)
                    report_file.write("\n")
                print(f"Captured {title}: {width}x{height} -> {output}", flush=True)
            finally:
                stop_editor(process)
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/linux-release/demi-editor"))
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--open", dest="open_source", type=Path)
    parser.add_argument("--terrain-graph", action="store_true")
    parser.add_argument("--terrain-settings-node", metavar="STABLE_NODE_ID",
                        help="Open a node's native settings; implies graph opening.")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--width", type=int, help="Optional fixed capture width; otherwise keep the display-sized editor.")
    parser.add_argument("--height", type=int, help="Optional fixed capture height, paired with --width.")
    parser.add_argument("--video-driver", choices=("x11", "auto"), default="x11")
    parser.add_argument("--window-timeout", type=float, default=45)
    parser.add_argument("--settle-seconds", type=float, default=3)
    arguments = parser.parse_args()
    if arguments.terrain_settings_node is not None and not arguments.terrain_settings_node:
        parser.error("Terrain settings require a nonempty stable graph node ID.")
    if (arguments.width is None) != (arguments.height is None):
        parser.error("Provide both --width and --height, or neither.")
    if arguments.width is not None and (arguments.width <= 0 or arguments.height <= 0):
        parser.error("Window dimensions must be positive.")
    if not math.isfinite(arguments.window_timeout) or arguments.window_timeout <= 0:
        parser.error("Window timeout must be finite and positive.")
    if not math.isfinite(arguments.settle_seconds) or arguments.settle_seconds < 0:
        parser.error("Settle time must be finite and nonnegative.")
    try:
        capture(arguments)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Editor capture failed: {error}\n")


if __name__ == "__main__":
    main()

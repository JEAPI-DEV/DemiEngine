#!/usr/bin/env python3
"""Physical Android device runner, hot-reload loop, and qualification gate."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import time


class ToolError(RuntimeError):
    pass


def command(arguments: list[str], *, binary: bool = False,
            check: bool = True) -> subprocess.CompletedProcess:
    result = subprocess.run(arguments, capture_output=True,
                            text=not binary, check=False)
    if check and result.returncode != 0:
        stderr = result.stderr.decode() if binary else result.stderr
        raise ToolError(stderr.strip() or f"Command failed: {arguments[0]}")
    return result


def parse_devices(output: str) -> list[str]:
    devices = []
    for line in output.splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "device":
            devices.append(fields[0])
    return devices


class Adb:
    def __init__(self, executable: str, serial: str | None):
        self.executable = executable
        available = parse_devices(command([executable, "devices", "-l"]).stdout)
        if serial and serial not in available:
            raise ToolError(f"Android device is not connected: {serial}")
        if not serial:
            if len(available) != 1:
                raise ToolError("Connect exactly one Android device or pass --serial.")
            serial = available[0]
        self.serial = serial

    def args(self, *arguments: str) -> list[str]:
        return [self.executable, "-s", self.serial, *arguments]

    def run(self, *arguments: str, binary: bool = False,
            check: bool = True) -> subprocess.CompletedProcess:
        return command(self.args(*arguments), binary=binary, check=check)

    def shell(self, script: str, *, check: bool = True) -> str:
        return self.run("shell", script, check=check).stdout.strip()


def project_configuration(project_file: Path) -> tuple[str, str, str]:
    document = json.loads(project_file.read_text(encoding="utf-8"))
    build = document.get("build", {})
    application_id = build.get("application_id", "dev.jeapi.demi.android")
    executable = build.get("executable_name", project_file.parent.name)
    activity = "dev.jeapi.demi.android.DemiActivity"
    return application_id, executable, f"{application_id}/{activity}"


def build_apk(demi: Path, project: Path) -> Path:
    command([str(demi), "build", "apk", "--project", str(project)])
    _, executable, _ = project_configuration(project)
    apk = project.parent / "build" / "android" / f"{executable}-debug.apk"
    if not apk.is_file():
        raise ToolError(f"Android build did not publish an APK: {apk}")
    return apk


def install(adb: Adb, apk: Path) -> None:
    print(f"Installing {apk}", flush=True)
    adb.run("install", "-r", str(apk))


def set_hot_reload_marker(adb: Adb, package: str, enabled: bool) -> None:
    action = "mkdir -p files && touch files/.demi_hot_reload" if enabled \
        else "rm -f files/.demi_hot_reload"
    adb.shell(f"run-as {shlex.quote(package)} sh -c {shlex.quote(action)}")


def launch(adb: Adb, package: str, component: str) -> None:
    adb.run("shell", "am", "force-stop", package)
    result = adb.run("shell", "am", "start", "-W", "-n", component)
    print(result.stdout.strip(), flush=True)


IGNORED_DIRECTORIES = {".git", ".demi", "build", "generated", "saves"}


def source_snapshot(project_root: Path) -> dict[str, tuple[int, int]]:
    snapshot = {}
    for path in project_root.rglob("*"):
        relative = path.relative_to(project_root)
        if any(component in IGNORED_DIRECTORIES for component in relative.parts):
            continue
        if path.is_file():
            stat = path.stat()
            snapshot[relative.as_posix()] = (stat.st_mtime_ns, stat.st_size)
    return snapshot


def file_hashes(root: Path) -> dict[str, str]:
    result = {}
    for path in root.rglob("*"):
        if path.is_file() and ".cook-cache" not in path.parts:
            result[path.relative_to(root).as_posix()] = hashlib.sha256(
                path.read_bytes()).hexdigest()
    return result


def sync_changed(adb: Adb, package: str, cooked: Path,
                 previous: dict[str, str]) -> dict[str, str]:
    current = file_hashes(cooked)
    changed = [relative for relative, digest in current.items()
               if previous.get(relative) != digest]
    for index, relative in enumerate(changed):
        source = cooked / relative
        temporary = f"/data/local/tmp/demi_reload_{os.getpid()}_{index}"
        destination = f"files/project/{relative}"
        adb.run("push", str(source), temporary)
        script = (f"mkdir -p {shlex.quote(str(Path(destination).parent))} && "
                  f"cp {shlex.quote(temporary)} {shlex.quote(destination)}")
        adb.shell(f"run-as {shlex.quote(package)} sh -c {shlex.quote(script)}")
        adb.run("shell", "rm", "-f", temporary)
    if changed:
        print(f"Hot reload synchronized {len(changed)} cooked file(s).", flush=True)
    return current


def log_process(adb: Adb) -> subprocess.Popen:
    return subprocess.Popen(adb.args(
        "logcat", "-v", "color", "DemiEngine:I", "SDL:I", "AndroidRuntime:E",
        "libc:F", "*:S"))


def run_on_device(args: argparse.Namespace) -> int:
    project = Path(args.project).resolve()
    demi = Path(args.demi_executable).resolve()
    adb = Adb(args.adb, args.serial)
    package, _, component = project_configuration(project)
    apk = Path(args.apk).resolve() if args.apk else build_apk(demi, project)
    install(adb, apk)
    set_hot_reload_marker(adb, package, args.watch)
    adb.run("logcat", "-b", "all", "-c")
    launch(adb, package, component)
    logs = log_process(adb)
    try:
        if not args.watch:
            return logs.wait()
        cooked = project.parent / "generated" / "android-dev"
        command([str(demi), "cook", "--project", str(project),
                 "--platform", "android", "--output", str(cooked)])
        cooked_hashes = file_hashes(cooked)
        sources = source_snapshot(project.parent)
        print(f"Watching {project.parent} for Android hot reload. Ctrl-C stops.",
              flush=True)
        while logs.poll() is None:
            time.sleep(0.5)
            next_sources = source_snapshot(project.parent)
            if next_sources == sources:
                continue
            sources = next_sources
            command([str(demi), "cook", "--project", str(project),
                     "--platform", "android", "--output", str(cooked)])
            cooked_hashes = sync_changed(adb, package, cooked, cooked_hashes)
        return logs.returncode or 0
    except KeyboardInterrupt:
        return 130
    finally:
        if logs.poll() is None:
            logs.terminate()
            try:
                logs.wait(timeout=2)
            except subprocess.TimeoutExpired:
                logs.kill()


def qualification_step(report: dict, name: str, operation) -> bool:
    started = time.monotonic()
    try:
        detail = operation()
        report["steps"].append({"name": name, "status": "passed",
                                "duration_ms": round((time.monotonic() - started) * 1000),
                                "detail": detail or ""})
        return True
    except Exception as error:  # keep collecting diagnostics after a failed step
        report["steps"].append({"name": name, "status": "failed",
                                "duration_ms": round((time.monotonic() - started) * 1000),
                                "detail": str(error)})
        return False


def qualify_device(args: argparse.Namespace) -> int:
    project = Path(args.project).resolve()
    demi = Path(args.demi_executable).resolve()
    adb = Adb(args.adb, args.serial)
    package, _, component = project_configuration(project)
    output = Path(args.output or project.parent / "build/android/qualification")
    output.mkdir(parents=True, exist_ok=True)
    report = {"format_version": 1, "serial": adb.serial, "package": package,
              "steps": [], "device": {}, "success": False}
    session_pids: set[str] = set()
    report["device"] = {
        "model": adb.run("shell", "getprop", "ro.product.model").stdout.strip(),
        "sdk": adb.run("shell", "getprop", "ro.build.version.sdk").stdout.strip(),
        "abi": adb.run("shell", "getprop", "ro.product.cpu.abi").stdout.strip(),
        "renderer": adb.run("shell", "getprop", "ro.hardware.egl").stdout.strip(),
    }
    apk = Path(args.apk).resolve() if args.apk else build_apk(demi, project)
    adb.run("logcat", "-b", "all", "-c")
    critical = []
    critical.append(qualification_step(report, "install", lambda: adb.run(
        "install", "-r", str(apk)).stdout.strip()))
    if critical[-1]:
        set_hot_reload_marker(adb, package, False)
    critical.append(qualification_step(report, "launch", lambda: adb.run(
        "shell", "am", "start", "-W", "-n", component).stdout.strip()))
    time.sleep(args.settle_seconds)
    adb.run("shell", "cmd", "statusbar", "collapse", check=False)
    session_pids.update(adb.run("shell", "pidof", package,
                                check=False).stdout.split())
    screenshot = output / "launch.png"
    critical.append(qualification_step(report, "screenshot", lambda: (
        screenshot.write_bytes(adb.run("exec-out", "screencap", "-p",
                                       binary=True).stdout),
        hashlib.sha256(screenshot.read_bytes()).hexdigest())[1]))
    image = screenshot.read_bytes()
    screen_width, screen_height = struct.unpack(">II", image[16:24])
    report["screen"] = {"width": screen_width, "height": screen_height}
    qualification_step(report, "touch", lambda: adb.run(
        "shell", "input", "tap", str(screen_width // 2),
        str(int(screen_height * 0.40))).stdout.strip())
    time.sleep(0.4)
    qualification_step(report, "scene_load_input", lambda: adb.run(
        "shell", "input", "tap", str(int(screen_width * 0.27)),
        str(int(screen_height * 0.78))).stdout.strip())
    time.sleep(args.settle_seconds)
    qualification_step(report, "ime_text", lambda: adb.run(
        "shell", "input", "text", "DemiEngine").stdout.strip())
    qualification_step(report, "background_resume", lambda: (
        adb.run("shell", "input", "keyevent", "HOME"),
        time.sleep(1), adb.run("shell", "am", "start", "-n", component))[2].stdout.strip())
    qualification_step(report, "low_memory", lambda: adb.run(
        "shell", "am", "send-trim-memory", package, "RUNNING_LOW").stdout.strip())
    original_rotation = adb.run("shell", "settings", "get", "system",
                                "user_rotation").stdout.strip()
    original_auto_rotation = adb.run("shell", "settings", "get", "system",
                                     "accelerometer_rotation").stdout.strip()
    try:
        qualification_step(report, "rotation", lambda: (
            adb.run("shell", "settings", "put", "system", "accelerometer_rotation", "0"),
            adb.run("shell", "settings", "put", "system", "user_rotation", "1"),
            time.sleep(1), adb.run("shell", "settings", "put", "system",
                                  "user_rotation", "0"))[3].stdout.strip())
    finally:
        if original_rotation:
            adb.run("shell", "settings", "put", "system", "user_rotation",
                    original_rotation, check=False)
        else:
            adb.run("shell", "settings", "delete", "system",
                    "user_rotation", check=False)
        if original_auto_rotation:
            adb.run("shell", "settings", "put", "system",
                    "accelerometer_rotation", original_auto_rotation,
                    check=False)
        else:
            adb.run("shell", "settings", "delete", "system",
                    "accelerometer_rotation", check=False)
    critical.append(qualification_step(report, "force_stop_relaunch", lambda: (
        adb.run("shell", "am", "force-stop", package),
        adb.run("shell", "am", "start", "-W", "-n", component))[1].stdout.strip()))
    time.sleep(args.settle_seconds)
    session_pids.update(adb.run("shell", "pidof", package,
                                check=False).stdout.split())
    critical.append(qualification_step(report, "process_alive", lambda: adb.run(
        "shell", "pidof", package).stdout.strip()))
    qualification_step(report, "private_files", lambda: adb.shell(
        f"run-as {shlex.quote(package)} find files -maxdepth 4 -type f"))
    log_file = output / "logcat.txt"
    logs = adb.run("logcat", "-b", "all", "-d", "-v", "threadtime").stdout
    log_file.write_text(logs, encoding="utf-8")
    fatal_markers = []
    for line in logs.splitlines():
        fields = line.split()
        process_matches = len(fields) > 3 and fields[2] in session_pids
        if process_matches:
            for marker in ("FATAL EXCEPTION", "Fatal signal"):
                if marker in line and marker not in fatal_markers:
                    fatal_markers.append(marker)
        if f"ANR in {package}" in line and "ANR" not in fatal_markers:
            fatal_markers.append("ANR")
    report["session_pids"] = sorted(session_pids)
    report["fatal_markers"] = fatal_markers
    required_runtime_markers = [
        "Switched scene to scene://minimal_2d_android/platformer",
        "[runtime] Frame 61",
    ]
    missing_runtime_markers = [marker for marker in required_runtime_markers
                               if marker not in logs]
    report["missing_runtime_markers"] = missing_runtime_markers
    critical.append(not missing_runtime_markers)
    report["log"] = log_file.name
    report["screenshot"] = screenshot.name
    report["success"] = all(critical) and not fatal_markers
    report_path = output / "qualification.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote Android qualification report: {report_path}")
    return 0 if report["success"] else 1


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    result.add_argument("mode", choices=("run", "qualify"))
    result.add_argument("--project", required=True)
    result.add_argument("--demi-executable", required=True)
    result.add_argument("--adb", default="adb")
    result.add_argument("--serial")
    result.add_argument("--apk")
    result.add_argument("--watch", action="store_true")
    result.add_argument("--output")
    result.add_argument("--settle-seconds", type=float, default=2.0)
    return result


def main() -> int:
    args = parser().parse_args()
    try:
        return run_on_device(args) if args.mode == "run" else qualify_device(args)
    except ToolError as error:
        print(f"Android device error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

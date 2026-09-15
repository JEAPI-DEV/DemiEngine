#!/usr/bin/env python3
"""Sequential optimized Android crowd captures; requires an authorized device."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import zipfile

import animation_crowd_workload as crowd
from android_device import Adb
from benchmark_3d_visible import summarize


PACKAGE = 'dev.jeapi.demi.m2profile'
COMPONENT = PACKAGE + '/dev.jeapi.demi.android.DemiActivity'
ROOT = Path(__file__).resolve().parents[1]


def extract_cooked_assets(apk, destination):
    """Repackage only the native packager's already audited cooked assets."""
    with zipfile.ZipFile(apk) as archive:
        for entry in archive.infolist():
            if not entry.filename.startswith('assets/') or entry.is_dir():
                continue
            relative = Path(entry.filename.removeprefix('assets/'))
            if relative.as_posix() == 'demi_asset_index.txt':
                continue  # Gradle regenerates its platform-owned index.
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Unsafe APK asset path')
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(entry) as source, target.open('xb') as output:
                shutil.copyfileobj(source, output)
    if not (destination / 'demi.project.json').is_file():
        raise ValueError('APK has no cooked project')


def run_case(args, adb, output, count, workload):
    case = output / f'{workload}-{count}'
    case.mkdir()
    project = case / 'source'
    shutil.copytree(ROOT / 'examples/animation_3d', project,
                    ignore=shutil.ignore_patterns('build', 'generated'))
    project_file = project / 'demi.project.json'
    settings = json.loads(project_file.read_text())
    settings['display'] = {'vsync': False}
    settings['debug'] = {'grid': False}
    settings['build'] = {
        'application_id': PACKAGE, 'executable_name': 'm2_profile',
        'android': {'orientation': 'landscape', 'min_sdk': 26}}
    crowd.configure(project, settings, count, workload, args.seconds,
                    args.rig_layout, args.visual_rate, 30)
    project_file.write_text(json.dumps(settings, indent=2) + '\n')
    with (case / 'build.log').open('w') as log:
        def run(command):
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
        if args.rig_layout == 'split':
            run([str(args.binary), 'asset', 'reimport',
                 str(project / 'assets/AnimationLib/ual1_standard.asset.json')])
        # Keep validation, cooking and the shipping-content audit at their owner.
        run([str(args.binary), 'build', 'apk', '--project', str(project_file)])
        cooked = case / 'cooked'
        extract_cooked_assets(project / 'build/android/m2_profile-debug.apk', cooked)
        run(['gradle', '--no-daemon', '--console=plain', '--no-watch-fs',
             '-p', str(ROOT / 'android'),
             '-PdemiProjectFile=' + str(cooked / 'demi.project.json'),
             ':app:assembleProfile'])
    apk = case / 'profile.apk'
    shutil.copyfile(ROOT / 'android/app/build/outputs/apk/profile/app-profile.apk', apk)
    with apk.open('rb') as stream:
        apk_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    adb.run('install', '--no-streaming', '-r', str(apk))
    adb.run('shell', 'am', 'force-stop', PACKAGE)
    adb.shell(f"run-as {PACKAGE} sh -c 'mkdir -p files && touch files/.demi_profile'")
    before = adb.run('shell', 'dumpsys', 'thermalservice').stdout
    (case / 'thermal-before.txt').write_text(before)
    adb.run('shell', 'am', 'start', '-W', '-n', COMPONENT)
    started = time.monotonic()
    captured = False
    try:
        # Poll briefly; screenshot only once, before the measured window.
        while True:
            elapsed = time.monotonic() - started
            if not captured and elapsed >= 1:
                (case / 'launch.png').write_bytes(
                    adb.run('exec-out', 'screencap', '-p', binary=True).stdout)
                captured = True
            if not adb.run('shell', 'pidof', PACKAGE, check=False).stdout.strip():
                break
            # Android may retain an idle process after the native main returns.
            # The summary is written only after the per-frame trace closes.
            if adb.run('shell', 'run-as', PACKAGE, 'sh', '-c',
                       "'test files/profile.csv -nt files/.demi_profile'",
                       check=False).returncode == 0:
                break
            if elapsed > args.seconds * 4 + 60:
                raise TimeoutError('Android crowd did not terminate')
            time.sleep(1)
        for name in ('profile.csv', 'profile.frames.csv'):
            (case / name).write_bytes(adb.run(
                'exec-out', 'run-as', PACKAGE, 'cat', 'files/' + name,
                binary=True).stdout)
        (case / 'thermal-after.txt').write_text(
            adb.run('shell', 'dumpsys', 'thermalservice').stdout)
        (case / 'runtime.log').write_text(adb.run('logcat', '-d', '-s', 'DemiEngine').stdout)
    finally:
        adb.run('shell', 'am', 'force-stop', PACKAGE)
    result = summarize(case / 'profile.frames.csv', args.warmup, args.width, args.height)
    crowd.qualify(result, count, workload, args.visual_rate)
    crowd.qualify_skinning(result, (count + 1) // 2 if workload == 'mixed' else count, 'gpu')
    result['completed_duration'] = result['measured_wall_seconds'] >= (args.seconds - args.warmup) * .9
    result['valid_capture'] &= result['completed_duration'] and result['unfocused_frames'] == 0
    result.update(apk_sha256=apk_hash, workload=workload, count=count,
                  configuration='profile / RelWithDebInfo / debug signing',
                  rig_layout=args.rig_layout, visual_rate=args.visual_rate)
    (case / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'{workload}-{count}: valid={result["valid_capture"]} '
          f'frame={result["metrics"]["Frame.interval"]} '
          f'dropped_ms={result["dropped_fixed_ms"]}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/linux-release/demi')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--serial')
    parser.add_argument('--counts', type=int, nargs='+', default=[64])
    parser.add_argument('--workloads', choices=crowd.WORKLOADS, nargs='+', default=['animated'])
    parser.add_argument('--rig-layout', choices=['original', 'split'], default='split')
    parser.add_argument('--visual-rate', type=float, default=0)
    parser.add_argument('--seconds', type=float, default=20)
    parser.add_argument('--warmup', type=float, default=5)
    parser.add_argument('--width', type=int, default=2400)
    parser.add_argument('--height', type=int, default=1080)
    args = parser.parse_args()
    if (not 0 <= args.warmup < args.seconds <= 600 or
            any(not 1 <= n <= 2000 for n in args.counts) or
            not 0 <= args.visual_rate <= 240):
        parser.error('Require 0 <= warmup < seconds <= 600, counts 1..2000, rate 0..240')
    args.binary = args.binary.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    adb = Adb('adb', args.serial)
    (output / 'device.json').write_text(json.dumps({
        key: adb.run('shell', 'getprop', prop).stdout.strip()
        for key, prop in [('model', 'ro.product.model'), ('sdk', 'ro.build.version.sdk'),
                          ('abi', 'ro.product.cpu.abi')]}, indent=2) + '\n')
    for count in args.counts:
        for workload in args.workloads:
            print(f'Building {workload}-{count}', flush=True)
            run_case(args, adb, output, count, workload)


if __name__ == '__main__':
    main()

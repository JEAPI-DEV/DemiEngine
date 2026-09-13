#!/usr/bin/env python3
"""Run the CPU compound feasibility checks and retain bounded-work measurements."""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import platform
import re
import statistics
import subprocess
import time


def percentile(values, fraction):
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    lower = int(index)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (index - lower)


def summarize(text):
    groups = {}
    for row in csv.DictReader(io.StringIO(text)):
        key = (int(row['chunks']), int(row['group_size']))
        groups.setdefault(key, []).append(row)
    expected = {(n, g) for n in (16, 64, 256) for g in (1, 4)}
    if set(groups) != expected or any(len(rows) != 10 for rows in groups.values()):
        raise ValueError('Expected ten measured runs for every population/group pair')
    results = []
    for (chunks, group_size), rows in sorted(groups.items()):
        metrics = {}
        for name in rows[0]:
            if not name.endswith('_ms'):
                continue
            values = [float(row[name]) for row in rows]
            if not all(math.isfinite(value) and value >= 0 for value in values):
                raise ValueError('Invalid timing samples')
            metrics[name] = {'p50': statistics.median(values),
                             'p95': percentile(values, .95),
                             'p99': percentile(values, .99), 'max': max(values)}
        results.append({'chunks': chunks, 'group_size': group_size, 'samples': 10,
                        'peak_bodies': max(int(row['peak_bodies']) for row in rows),
                        'metrics_ms': metrics})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--adb-serial', help='Run an Android ARM64 executable on this device')
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    metadata = {'format_version': 1, 'binary': str(binary),
                'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                'host': platform.platform(), 'physics_steps_per_sample': 120,
                'fixed_dt': 1 / 60, 'jolt_workers': 1, 'warmups_per_case': 2,
                'passed': False, 'gpu_measured': False}
    commit = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root,
                            text=True, capture_output=True, check=True)
    metadata['source_commit'] = commit.stdout.strip()
    metadata['probe_sources_sha256'] = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted((root / 'tools/3d-feasibility').iterdir()) if path.is_file()}
    remote = None
    safe_to_clean = False
    adb = ['adb', '-s', args.adb_serial] if args.adb_serial else None

    def adb_command(*command):
        return subprocess.run([*adb, *command], text=True, capture_output=True,
                              timeout=30, check=True).stdout.strip()

    try:
        if adb:
            metadata['device'] = {key: adb_command('shell', 'getprop', prop)
                                  for key, prop in {
                                      'model': 'ro.product.model',
                                      'abi': 'ro.product.cpu.abi',
                                      'android': 'ro.build.version.release',
                                      'sdk': 'ro.build.version.sdk',
                                      'hardware': 'ro.hardware'}.items()}
            if metadata['device']['abi'] != 'arm64-v8a':
                raise RuntimeError('This probe requires an arm64-v8a device')
            remote = adb_command('shell', 'mktemp', '-d', '/data/local/tmp/demi-compound.XXXXXX')
            if not re.fullmatch(r'/data/local/tmp/demi-compound\.[A-Za-z0-9]+', remote):
                raise RuntimeError('Unexpected remote staging path; refusing to use it')
            metadata['remote_staging'] = remote
            adb_command('push', str(binary), remote + '/probe')
            remote_hash = adb_command('shell', 'sha256sum', remote + '/probe').split()[0]
            if remote_hash != metadata['binary_sha256']:
                raise RuntimeError('Device executable checksum mismatch')
            adb_command('shell', 'chmod', '700', remote + '/probe')
            command = [*adb, 'shell', remote + '/probe']
        else:
            command = [str(binary)]
        for name, extra in [('correctness', []), ('benchmark', ['--benchmark'])]:
            safe_to_clean = False
            start = time.perf_counter()
            run = subprocess.run([*command, *extra], capture_output=True, text=True, timeout=180)
            safe_to_clean = True
            metadata[name + '_wall_seconds'] = time.perf_counter() - start
            (output / (name + '.stderr.log')).write_text(run.stderr)
            (output / (name + ('.csv' if name == 'benchmark' else '.log'))).write_text(run.stdout)
            if run.returncode:
                raise RuntimeError(f'{name} failed ({run.returncode}); see retained logs')
            if name == 'correctness' and 'Compound transition probe passed:' not in run.stdout:
                raise RuntimeError('Missing correctness completion marker')
            if name == 'benchmark':
                metadata['cases'] = summarize(run.stdout)
        metadata['passed'] = True
    finally:
        if remote and safe_to_clean:
            # Only our one regular executable and its now-empty staging directory.
            cleanup = (f'test -d {remote} && test ! -L {remote} && '
                       f'test -f {remote}/probe && test ! -L {remote}/probe && '
                       f'rm {remote}/probe && rmdir {remote}')
            try:
                adb_command('shell', cleanup)
                metadata['remote_cleaned'] = True
            except (subprocess.SubprocessError, OSError):
                metadata['remote_cleaned'] = False
                metadata['passed'] = False
        (output / 'summary.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps({'passed': metadata['passed'], 'report': str(output / 'summary.json')}))
    return 0 if metadata['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

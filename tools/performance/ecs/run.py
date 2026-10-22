#!/usr/bin/env python3
"""Run isolated ECS controls sequentially and reject divergent visible output."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--frames", type=int, default=60)
parser.add_argument("--rounds", type=int, default=3)
args = parser.parse_args()
if args.frames < 1 or args.rounds < 1:
    parser.error("frames and rounds must be positive")
binary = str(args.binary.resolve())
subprocess.run([binary, "--verify"], check=True)
controls = [("native", "scan"), ("entt", "scan"),
            ("native", "tracked"), ("entt", "tracked")]
with args.output.open("w") as output:
    for count in (25000, 100000):
        for workload in ("static", "moving_1pct", "moving_all", "structural"):
            expected_checksum = None
            for round_number in range(args.rounds):
                order = controls if round_number % 2 == 0 else list(reversed(controls))
                for backend, mode in order:
                    command = [binary, backend, mode, workload, str(count), str(args.frames)]
                    result = subprocess.run(command, check=True, capture_output=True, text=True)
                    row = json.loads(result.stdout)
                    row["round"] = round_number
                    if expected_checksum is None:
                        expected_checksum = row["checksum"]
                    if row["checksum"] != expected_checksum:
                        raise RuntimeError(f"Visible output differs: {row}")
                    output.write(json.dumps(row) + "\n")
                    output.flush()
            print(f"Verified {count} entities: {workload}", flush=True)

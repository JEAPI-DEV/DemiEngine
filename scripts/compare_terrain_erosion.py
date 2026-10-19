#!/usr/bin/env python3
"""Compare exported native erosion fields with pinned libtcod rain erosion.

Run in an isolated environment containing tcod==21.2.1. This tool does not
install dependencies, launch Demi, change authored inputs or choose a quality
winner. Its native input manifest is produced by benchmark_terrain_generation.
"""

import argparse
import hashlib
import importlib.metadata
import json
import platform
from pathlib import Path
try:
    import resource
except ImportError:
    resource = None
import sys
import time


def fnv_digest(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{value:016x}"


def load_field(directory, record, shape, np):
    path = directory / record["file"]
    data = path.read_bytes()
    expected_size = shape[0] * shape[1] * 4
    if len(data) != expected_size:
        raise ValueError(f"Incorrect float32 byte count: {path}")
    expected_digest = record.get("digest")
    if expected_digest is None:
        expected_digest = record["runs"][0]["digest"]
    if fnv_digest(data) != expected_digest:
        raise ValueError(f"Exported height digest mismatch: {path}")
    result = np.frombuffer(data, dtype="<f4").reshape(shape).astype(np.float32, copy=True)
    if not np.isfinite(result).all():
        raise ValueError(f"Nonfinite exported heights: {path}")
    return result


def field_metrics(baseline, heights, spacing, np):
    finite = bool(np.isfinite(heights).all())
    if not finite:
        return {"finite": False}
    difference = heights.astype(np.float64) - baseline.astype(np.float64)
    # These are sums of sample-height changes, not a claim of physical mass.
    # Physical slope uses the actual X/Z world spacing, including anisotropy.
    gradient_z, gradient_x = np.gradient(
        heights.astype(np.float64), spacing[1], spacing[0])
    slopes = np.hypot(gradient_x, gradient_z)
    return {
        "finite": True,
        "minimum_height": float(heights.min()),
        "maximum_height": float(heights.max()),
        "cut_height_sum": float(-difference[difference < 0].sum()),
        "deposited_height_sum": float(difference[difference > 0].sum()),
        "net_height_sum": float(difference.sum()),
        "mean_absolute_height_change": float(np.abs(difference).mean()),
        "physical_slope_mean": float(slopes.mean()),
        "physical_slope_p95": float(np.percentile(slopes, 95)),
        "physical_slope_max": float(slopes.max()),
    }


def native_transport_budget(record, metrics):
    ledger = record["runs"][0].get("transport")
    if ledger is None:
        return {"available": False,
                "note": "Historical export has no hydraulic transport ledger."}
    exported = ledger["exported_sediment"]
    suspended = ledger["suspended_sediment"]
    return {
        "available": True,
        **ledger,
        "hydraulic_ledger_residual": (
            ledger["eroded_sediment"] - ledger["deposited_sediment"] -
            exported - suspended),
        "surface_net_plus_outgoing": metrics["net_height_sum"] + exported + suspended,
        "note": "Discrete sample-height units, not physical mass. Ledger is hydraulic-only; "
                "surface change also includes thermal rounding in combined mode. "
                "Suspended is unfinished bounded transport, not persistent fluid state.",
    }


def run_tcod(baseline, drops, seed, batch_size, np, tcod):
    result = baseline.copy(order="C")
    random = tcod.random.Random(
        algorithm=tcod.random.MERSENNE_TWISTER, seed=seed)
    start = time.perf_counter_ns()
    if batch_size is None:
        tcod.libtcodpy.heightmap_rain_erosion(result, drops, 0.05, 0.05, random)
    else:
        remaining = drops
        while remaining > 0:
            count = min(batch_size, remaining)
            tcod.libtcodpy.heightmap_rain_erosion(result, count, 0.05, 0.05, random)
            remaining -= count
    elapsed = (time.perf_counter_ns() - start) / 1_000_000
    # Cloning and RNG setup are outside solver timing. The batched measurement
    # includes Python/C call overhead; it is not a native cancellation guarantee.
    return result, elapsed


def save_result(directory, stem, case_name, variant, heights):
    filename = f"{stem}.{case_name}.{variant}.f32"
    data = heights.astype("<f4", copy=False).tobytes(order="C")
    (directory / filename).write_bytes(data)
    return {"file": filename, "sha256": hashlib.sha256(data).hexdigest()}


def compare_case(case, directory, report_path, batch_size, drops_override,
                 np, tcod):
    shape = tuple(case["shape"])
    baseline = load_field(directory, case["baseline"], shape, np)
    native = {}
    native_hydraulic = None
    for variant, record in case["native"].items():
        heights = load_field(directory, record, shape, np)
        metrics = field_metrics(baseline, heights, case["spacing"], np)
        native[variant] = {
            "settings": record["settings"], "runs": record["runs"],
            "metrics": metrics,
            "transport_budget": native_transport_budget(record, metrics),
        }
        if variant == "hydraulic":
            native_hydraulic = heights
    drops = (drops_override if drops_override is not None else
             case["native"]["hydraulic"]["settings"]["total_droplets"])
    if not 0 < drops <= 2_147_483_647:
        raise ValueError("Rain drop count must fit a positive C int")
    outputs = []
    runs = []
    for variant, batch in [("repeat1", None), ("repeat2", None),
                           ("batched", batch_size)]:
        heights, milliseconds = run_tcod(
            baseline, drops, case["seed"], batch, np, tcod)
        if not np.isfinite(heights).all():
            raise ValueError(f"libtcod produced nonfinite heights: {case['name']}")
        outputs.append(heights)
        run = save_result(report_path.parent, report_path.stem, case["name"],
                          variant, heights)
        run.update({"variant": variant, "milliseconds": milliseconds,
                    "batch_size": batch,
                    "metrics": field_metrics(baseline, heights,
                                             case["spacing"], np)})
        runs.append(run)
    repeat_equal = outputs[0].tobytes() == outputs[1].tobytes()
    batched_equal = outputs[0].tobytes() == outputs[2].tobytes()
    difference = outputs[0].astype(np.float64) - native_hydraulic.astype(np.float64)
    return {
        "name": case["name"], "shape": case["shape"], "size": case["size"],
        "spacing": case["spacing"], "seed": case["seed"],
        "input_digest": case["baseline"]["digest"],
        "drainage_milliseconds": case["drainage_milliseconds"],
        "native": native,
        "libtcod": {
            "settings": {"drops": drops, "erosion_coefficient": 0.05,
                         "sedimentation_coefficient": 0.05,
                         "rng": "MERSENNE_TWISTER", "seed": case["seed"]},
            "advisory_minimum_drops": int(baseline.size),
            "below_advisory_minimum": drops < baseline.size,
            "runs": runs, "repeat_bitwise_equal": repeat_equal,
            "batched_bitwise_equal": batched_equal,
        },
        "difference_from_native_hydraulic": {
            "mean_absolute_height_difference": float(np.abs(difference).mean()),
            "maximum_absolute_height_difference": float(np.abs(difference).max()),
            "note": "Different algorithms and transport rules; not a quality score.",
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("export", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--drops", type=int,
                        help="Override drop count; otherwise use reported native total")
    args = parser.parse_args()
    if args.batch_size <= 0:
        parser.error("--batch-size must be positive")
    if args.output.resolve() == args.export.resolve():
        parser.error("Output must not overwrite the native export")
    import numpy as np
    import tcod
    import tcod.libtcodpy
    version = importlib.metadata.version("tcod")
    if version != "21.2.1":
        raise ValueError(f"Expected pinned tcod 21.2.1, found {version}")
    manifest = json.loads(args.export.read_text(encoding="utf-8"))
    if (manifest["format_version"] != 1 or
            manifest["sample_format"] != "little-endian-float32" or
            manifest["array_order"] != "rows-z-columns-x"):
        raise ValueError("Unsupported native comparison export")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "format_version": 1, "native_export": str(args.export.resolve()),
        "native_metadata": {key: value for key, value in manifest.items()
                            if key != "cases"},
        "environment": {"python": platform.python_version(),
                        "platform": platform.platform(), "machine": platform.machine(),
                        "numpy": np.__version__, "tcod": version},
        "release_source_reference": {
            "python_tcod": "https://github.com/libtcod/python-tcod/tree/21.2.1",
            "libtcod_submodule_commit": "27c2dbc9d97bacb18b9fd43a5c7f070dc34339ed",
        },
        "note": "No normalization. Solver timings exclude export/import and metrics. "
                "Native hydraulic, thermal and combined outputs are distinct. "
                "Native export/suspension budgets accompany surface changes; closed "
                "surface-mass conservation is not assumed. libtcod has no thermal stage; "
                "work counts do not imply equal quality.",
        "cases": [],
    }
    for case in manifest["cases"]:
        report["cases"].append(compare_case(
            case, args.export.parent, args.output, args.batch_size,
            args.drops, np, tcod))
    report["process_max_rss_kib"] = None
    if resource is not None:
        maximum_rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        report["process_max_rss_kib"] = (
            maximum_rss / 1024 if sys.platform == "darwin" else maximum_rss)
    report["memory_note"] = "Process high-water RSS includes imports and all cases; not solver-only memory."
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not all(case["libtcod"]["repeat_bitwise_equal"] and
               case["libtcod"]["batched_bitwise_equal"] for case in report["cases"]):
        raise RuntimeError("Repeat or batching changed libtcod output; inspect the written report")
    print(f"Wrote {args.output}; repeat and batch outputs match for all cases")


if __name__ == "__main__":
    main()

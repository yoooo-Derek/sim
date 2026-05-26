#!/usr/bin/env python3
import argparse
import csv
from collections import Counter
from pathlib import Path


def parse_experiment_name(name):
    parts = name.split("__")
    if len(parts) != 7:
        return {
            "suite": "unknown",
            "scale": "unknown",
            "traffic": "unknown",
            "baseline": "unknown",
            "admission": "unknown",
            "wecmp": "unknown",
            "seed": "unknown",
        }, f"warning: experimentName does not match expected pattern: {name}"
    seed = parts[6][4:] if parts[6].startswith("seed") else parts[6]
    return {
        "suite": parts[0],
        "scale": parts[1],
        "traffic": parts[2],
        "baseline": parts[3],
        "admission": parts[4],
        "wecmp": parts[5],
        "seed": seed,
    }, None


def main():
    parser = argparse.ArgumentParser(description="Combine TL-OCS summary CSV files.")
    parser.add_argument("--run-dir")
    parser.add_argument("--csv-dir")
    parser.add_argument("--output")
    args = parser.parse_args()

    if not args.run_dir and not args.csv_dir:
        parser.error("provide --run-dir or --csv-dir")

    run_dir = Path(args.run_dir) if args.run_dir else None
    csv_dir = Path(args.csv_dir) if args.csv_dir else run_dir / "csv"
    output = Path(args.output) if args.output else (
        run_dir / "combined-summary.csv" if run_dir else csv_dir / "combined-summary.csv"
    )

    summary_paths = sorted(csv_dir.glob("*-summary.csv"))
    rows = []
    fieldnames = [
        "sourceFile",
        "suite",
        "scale",
        "traffic",
        "baseline",
        "admission",
        "wecmp",
        "seed",
    ]
    baseline_counts = Counter()
    warnings = []

    for path in summary_paths:
        with path.open(newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                row = dict(row)
                experiment_name = row.get("experimentName", path.name.removesuffix("-summary.csv"))
                metadata, warning = parse_experiment_name(experiment_name)
                if warning:
                    warnings.append(warning)
                combined = {"sourceFile": path.name}
                combined.update(metadata)
                combined.update(row)
                rows.append(combined)
                baseline_counts[(combined["suite"], combined["baseline"], combined["admission"], combined["wecmp"])] += 1
                for field in combined:
                    if field not in fieldnames:
                        fieldnames.append(field)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    for warning in warnings:
        print(warning)
    print(f"Combined {len(rows)} summary rows into {output}")
    print("Baseline row counts:")
    for key, count in sorted(baseline_counts.items()):
        suite, baseline, admission, wecmp = key
        print(f"  suite={suite} baseline={baseline} admission={admission} wecmp={wecmp} rows={count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

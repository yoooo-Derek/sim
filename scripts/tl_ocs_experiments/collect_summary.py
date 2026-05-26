#!/usr/bin/env python3
import argparse
import csv
from collections import defaultdict
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
    parser.add_argument("--baseline-report")
    args = parser.parse_args()

    if not args.run_dir and not args.csv_dir:
        parser.error("provide --run-dir or --csv-dir")

    run_dir = Path(args.run_dir) if args.run_dir else None
    csv_dir = Path(args.csv_dir) if args.csv_dir else run_dir / "csv"
    output = Path(args.output) if args.output else (
        run_dir / "combined-summary.csv" if run_dir else csv_dir / "combined-summary.csv"
    )
    baseline_report = Path(args.baseline_report) if args.baseline_report else (
        run_dir / "baseline-summary.csv" if run_dir else csv_dir / "baseline-summary.csv"
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
    baseline_rows = defaultdict(list)
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
                baseline_rows[
                    (
                        combined["suite"],
                        combined["scale"],
                        combined["traffic"],
                        combined["baseline"],
                        combined["admission"],
                        combined["wecmp"],
                    )
                ].append(combined)
                for field in combined:
                    if field not in fieldnames:
                        fieldnames.append(field)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    metric_fields = [
        "completedFlowCount",
        "avgFctSeconds",
        "p99FctSeconds",
        "aggregateGoodputMbps",
        "ocsByteShare",
        "epsFallbackRatio",
        "linkTimeseriesSampleRows",
    ]
    baseline_fieldnames = [
        "suite",
        "scale",
        "traffic",
        "baseline",
        "admission",
        "wecmp",
        "rowCount",
    ] + [field + "Mean" for field in metric_fields]
    baseline_output_rows = []
    for key, group_rows in sorted(baseline_rows.items()):
        output_row = {
            "suite": key[0],
            "scale": key[1],
            "traffic": key[2],
            "baseline": key[3],
            "admission": key[4],
            "wecmp": key[5],
            "rowCount": str(len(group_rows)),
        }
        for field in metric_fields:
            values = []
            for row in group_rows:
                raw_value = row.get(field, "")
                if raw_value == "":
                    warnings.append(f"warning: missing field {field} for {row.get('experimentName', 'unknown')}")
                    continue
                try:
                    values.append(float(raw_value))
                except ValueError:
                    warnings.append(f"warning: could not parse {field}={raw_value} for {row.get('experimentName', 'unknown')}")
            output_row[field + "Mean"] = (
                "" if not values else f"{sum(values) / len(values):.12g}"
            )
        baseline_output_rows.append(output_row)

    baseline_report.parent.mkdir(parents=True, exist_ok=True)
    with baseline_report.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=baseline_fieldnames)
        writer.writeheader()
        writer.writerows(baseline_output_rows)

    for warning in warnings:
        print(warning)
    print(f"Combined {len(rows)} summary rows into {output}")
    print(f"Baseline summary rows: {len(baseline_output_rows)} into {baseline_report}")
    print("Baseline row counts:")
    for row in baseline_output_rows:
        print(
            "  suite={suite} scale={scale} traffic={traffic} baseline={baseline} "
            "admission={admission} wecmp={wecmp} rows={rowCount}".format(**row)
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

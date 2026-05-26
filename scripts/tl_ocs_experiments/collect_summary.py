#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="Combine TL-OCS small-smoke summary CSV files.")
    parser.add_argument("--run-dir")
    parser.add_argument("--csv-dir")
    parser.add_argument("--output")
    args = parser.parse_args()

    if not args.run_dir and not args.csv_dir:
        parser.error("provide --run-dir or --csv-dir")

    run_dir = Path(args.run_dir) if args.run_dir else None
    csv_dir = Path(args.csv_dir) if args.csv_dir else run_dir / "csv"
    output = Path(args.output) if args.output else (run_dir / "combined-summary.csv" if run_dir else csv_dir / "combined-summary.csv")

    summary_paths = sorted(csv_dir.glob("*-summary.csv"))
    rows = []
    fieldnames = []
    for path in summary_paths:
        with path.open(newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                row = dict(row)
                row["sourceFile"] = path.name
                rows.append(row)
                for field in row:
                    if field not in fieldnames:
                        fieldnames.append(field)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Combined {len(rows)} summary rows into {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

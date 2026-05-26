#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path


KEY_METRICS = [
    "experimentName",
    "overallResultConsistency",
    "overallAlgorithmInvariant",
    "completedFlowCount",
    "avgFctSeconds",
    "p99FctSeconds",
    "aggregateGoodputMbps",
    "ocsByteShare",
    "epsFallbackRatio",
    "linkCounterCount",
    "linkTimeseriesSampleRows",
    "measuredWecmpLaterProofPassed",
]


def read_rows(path):
    if not path.exists():
        return []
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def md_table(headers, rows):
    lines = []
    lines.append("| " + " | ".join(headers) + " |")
    lines.append("| " + " | ".join("---" for _ in headers) + " |")
    for row in rows:
        values = [str(row.get(header, "")) for header in headers]
        values = [value.replace("|", "\\|") for value in values]
        lines.append("| " + " | ".join(values) + " |")
    return "\n".join(lines)


def count_status(validation_rows):
    counts = {"pass": 0, "warn": 0, "fail": 0}
    for row in validation_rows:
        status = row.get("status", "")
        if status in counts:
            counts[status] += 1
    return counts


def summarize_wecmp(validation_rows, combined_rows):
    checks = {
        "measuredNoSampleFallback": any(
            row.get("check") == "wecmp_measured_nosample_fallback"
            and row.get("status") == "pass"
            for row in validation_rows
        ),
        "measuredLaterProof": any(
            row.get("check") == "patch4c_later_proof_passed"
            and row.get("status") == "pass"
            for row in validation_rows
        ),
        "selectedSpineChange": any(
            row.get("check") in ("wecmp_later_changed_spine", "flows_later_selected_spine_changed")
            and row.get("status") == "pass"
            for row in validation_rows
        ),
    }
    for row in combined_rows:
        if row.get("measuredWecmpLaterProofPassed", "").lower() == "true":
            checks["measuredLaterProof"] = True
        changed = row.get("measuredWecmpLaterChangedSelectedSpineCount", "")
        try:
            if float(changed) > 0:
                checks["selectedSpineChange"] = True
        except ValueError:
            pass
    return checks


def main():
    parser = argparse.ArgumentParser(description="Create a TL-OCS run-level engineering report.")
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--csv-dir")
    parser.add_argument("--manifest")
    parser.add_argument("--validation-report")
    parser.add_argument("--combined-summary")
    parser.add_argument("--baseline-summary")
    parser.add_argument("--output")
    args = parser.parse_args()

    run_dir = Path(args.run_dir)
    csv_dir = Path(args.csv_dir) if args.csv_dir else run_dir / "csv"
    manifest_path = Path(args.manifest) if args.manifest else run_dir / "manifest.csv"
    validation_path = (
        Path(args.validation_report)
        if args.validation_report
        else run_dir / "validation-report.csv"
    )
    combined_path = (
        Path(args.combined_summary)
        if args.combined_summary
        else run_dir / "combined-summary.csv"
    )
    baseline_path = (
        Path(args.baseline_summary)
        if args.baseline_summary
        else run_dir / "baseline-summary.csv"
    )
    output_path = Path(args.output) if args.output else run_dir / "run-report.md"

    manifest_rows = read_rows(manifest_path)
    validation_rows = read_rows(validation_path)
    combined_rows = read_rows(combined_path)
    baseline_rows = read_rows(baseline_path)
    status_counts = count_status(validation_rows)
    failed_rows = [row for row in validation_rows if row.get("status") in ("fail", "warn")]
    route_rows = [row for row in validation_rows if row.get("check", "").startswith("route_")]
    route_problem_rows = [row for row in route_rows if row.get("status") != "pass"]
    wecmp_summary = summarize_wecmp(validation_rows, combined_rows)

    lines = []
    lines.append("# TL-OCS Run Report")
    lines.append("")
    lines.append("This is an engineering validation report, not a paper result.")
    lines.append("")
    lines.append("- No paper-candidate sweep was run.")
    lines.append("- No final figures were generated.")
    lines.append("- No statistical conclusion or confidence interval is reported.")
    lines.append("- Tables below summarize script validation outputs only.")
    lines.append("")
    lines.append("## Run Inputs")
    lines.append("")
    lines.append(f"- runDir: `{run_dir}`")
    lines.append(f"- csvDir: `{csv_dir}`")
    lines.append(f"- manifest: `{manifest_path}`")
    lines.append(f"- validationReport: `{validation_path}`")
    lines.append(f"- combinedSummary: `{combined_path}`")
    lines.append(f"- baselineSummary: `{baseline_path}`")
    lines.append("")

    lines.append("## Validation Summary")
    lines.append("")
    lines.append(
        f"- pass: {status_counts['pass']}\n- warn: {status_counts['warn']}\n- fail: {status_counts['fail']}"
    )
    lines.append("")
    if failed_rows:
        lines.append("Failed or warning checks:")
        lines.append("")
        lines.append(md_table(["experimentName", "check", "severity", "status", "message"], failed_rows))
    else:
        lines.append("No failed or warning validation checks.")
    lines.append("")

    lines.append("## Manifest")
    lines.append("")
    lines.append(f"- scenarioCount: {len(manifest_rows)}")
    lines.append("")
    if manifest_rows:
        lines.append(md_table(["experimentName", "returnCode", "logPath"], manifest_rows))
        lines.append("")

    lines.append("## Combined Summary")
    lines.append("")
    lines.append(f"- baselineRows: {len(baseline_rows)}")
    if baseline_rows:
        baseline_headers = [
            "suite",
            "scale",
            "traffic",
            "baseline",
            "admission",
            "wecmp",
            "rowCount",
            "avgFctSecondsMean",
            "aggregateGoodputMbpsMean",
            "ocsByteShareMean",
            "epsFallbackRatioMean",
        ]
        lines.append("")
        lines.append(md_table(baseline_headers, baseline_rows))
    lines.append("")

    if combined_rows:
        present_headers = [header for header in KEY_METRICS if header in combined_rows[0]]
        lines.append("Key metrics table:")
        lines.append("")
        lines.append(md_table(present_headers, combined_rows))
        lines.append("")

    lines.append("## Route Correctness")
    lines.append("")
    route_pass_count = sum(1 for row in route_rows if row.get("status") == "pass")
    lines.append(f"- route checks passed: {route_pass_count}")
    lines.append(f"- route checks with warning/fail: {len(route_problem_rows)}")
    if route_problem_rows:
        lines.append("")
        lines.append(md_table(["experimentName", "check", "severity", "status", "message"], route_problem_rows))
    lines.append("")

    lines.append("## WECMP")
    lines.append("")
    lines.append(f"- measured no-sample fallback observed: {str(wecmp_summary['measuredNoSampleFallback']).lower()}")
    lines.append(f"- measured later proof passed: {str(wecmp_summary['measuredLaterProof']).lower()}")
    lines.append(f"- selectedSpine change observed: {str(wecmp_summary['selectedSpineChange']).lower()}")
    lines.append("")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Run report written to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

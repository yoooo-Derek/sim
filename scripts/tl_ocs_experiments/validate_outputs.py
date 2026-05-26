#!/usr/bin/env python3
import argparse
import csv
import re
import sys
from pathlib import Path


SMOKE_SCENARIOS = [
    "smoke__4l2s2h__skewed__static_ocs03__hit__det__seed0",
    "smoke__4l2s2h__skewed__static_ocs03__admit_fallback__det__seed0",
    "smoke__4l2s2h__skewed__tl_ocs__fallback__det__seed0",
    "smoke__4l2s2h__skewed__tl_ocs__fallback__cp_wecmp__seed0",
    "smoke__4l2s2h__skewed__tl_ocs__fallback__measured_nosample__seed0",
    "smoke__4l2s2h__skewed__tl_ocs__fallback__measured_later__seed0",
]

MEDIUM_SCENARIOS = [
    "medium__8l4s2h__skewed__static_ocs07__hit__det__seed0",
    "medium__8l4s2h__skewed__static_ocs07__admit_fallback__det__seed0",
    "medium__8l4s2h__skewed__tl_ocs__fallback__cp_wecmp__seed0",
    "medium__8l4s2h__skewed__tl_ocs__fallback__measured_later__seed0",
]


def read_rows(path):
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def parse_float(value, default=None):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def parse_int(value, default=None):
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def is_true(value):
    return str(value).strip().lower() == "true"


def parse_experiment_name(name):
    parts = name.split("__")
    metadata = {
        "suite": "unknown",
        "scale": "unknown",
        "traffic": "unknown",
        "baseline": "unknown",
        "admission": "unknown",
        "wecmp": "unknown",
        "seed": "unknown",
    }
    if len(parts) == 7:
        metadata.update(
            {
                "suite": parts[0],
                "scale": parts[1],
                "traffic": parts[2],
                "baseline": parts[3],
                "admission": parts[4],
                "wecmp": parts[5],
                "seed": parts[6][4:] if parts[6].startswith("seed") else parts[6],
            }
        )
    return metadata


def parse_scale(scale):
    match = re.fullmatch(r"(\d+)l(\d+)s(\d+)h", scale)
    if not match:
        return 4, 2, 2
    return tuple(int(part) for part in match.groups())


def expected_link_counter_count(scenario):
    metadata = parse_experiment_name(scenario)
    leaves, spines, _servers = parse_scale(metadata["scale"])
    return leaves * spines * 2 + 2


def expected_measured_wecmp_rows(scenario):
    metadata = parse_experiment_name(scenario)
    leaves, spines, _servers = parse_scale(metadata["scale"])
    return leaves * spines * 2


def find_selected_ocs_link(rows):
    for row in rows:
        if row.get("linkType") == "ocs" and row.get("direction") == "a-to-b":
            match = re.search(r"ocs-leaf(\d+)-leaf(\d+)-a-to-b", row.get("linkId", ""))
            if match:
                return row, int(match.group(1)), int(match.group(2))
    return None, None, None


def link_tx_by_id(rows):
    by_id = {row.get("linkId", ""): row for row in rows}

    def tx(link_id):
        return parse_int(by_id.get(link_id, {}).get("txBytes"), 0) or 0

    return tx


def first_matching_flow(flows, predicate):
    for row in flows:
        if predicate(row):
            return row
    return None


class Validator:
    def __init__(self, run_dir, csv_dir, manifest_path, report_path, suite, scenarios, strict):
        self.run_dir = run_dir
        self.csv_dir = csv_dir
        self.manifest_path = manifest_path
        self.report_path = report_path
        self.suite = suite
        self.scenarios = list(scenarios)
        self.strict = strict
        self.rows = []
        self.failures = 0

    def record(self, scenario, check, ok, message, required=True):
        status = "pass" if ok else ("fail" if required or self.strict else "warn")
        self.rows.append(
            {
                "experimentName": scenario,
                "check": check,
                "status": status,
                "message": message,
            }
        )
        if status == "fail":
            self.failures += 1

    def require(self, scenario, check, condition, message):
        self.record(scenario, check, bool(condition), message)
        return bool(condition)

    def require_fields(self, scenario, check, rows, fields):
        if not rows:
            self.record(scenario, check, False, "no data rows")
            return False
        missing = [field for field in fields if field not in rows[0]]
        self.record(
            scenario,
            check,
            not missing,
            "ok" if not missing else "missing fields: " + ",".join(missing),
        )
        return not missing

    def file_path(self, scenario, suffix):
        return self.csv_dir / f"{scenario}-{suffix}.csv"

    def validate_manifest(self):
        if not self.require("GLOBAL", "manifest_exists", self.manifest_path.exists(), str(self.manifest_path)):
            return {}
        rows = read_rows(self.manifest_path)
        by_name = {row.get("experimentName", ""): row for row in rows}
        for scenario in self.scenarios:
            row = by_name.get(scenario)
            self.require(scenario, "manifest_row", row is not None, "manifest contains scenario")
            if row is not None:
                self.require(
                    scenario,
                    "scenario_return_code",
                    row.get("returnCode") == "0",
                    f"returnCode={row.get('returnCode')}",
                )
        return by_name

    def validate_summary(self, scenario):
        path = self.file_path(scenario, "summary")
        if not self.require(scenario, "summary_exists", path.exists(), str(path)):
            return None
        rows = read_rows(path)
        if not self.require(scenario, "summary_one_row", len(rows) == 1, f"rows={len(rows)}"):
            return None
        row = rows[0]
        required = [
            "overallResultConsistency",
            "overallAlgorithmInvariant",
            "completedFlows",
            "completedFlowCount",
            "linkCounterCount",
            "linkTimeseriesEnabled",
            "linkTimeseriesSampleRows",
        ]
        self.require_fields(scenario, "summary_schema", rows, required)
        self.require(
            scenario,
            "overall_result_consistency",
            row.get("overallResultConsistency") == "pass",
            row.get("overallResultConsistency", ""),
        )
        self.require(
            scenario,
            "overall_algorithm_invariant",
            row.get("overallAlgorithmInvariant") == "pass",
            row.get("overallAlgorithmInvariant", ""),
        )
        self.require(
            scenario,
            "completed_flows_positive",
            (parse_int(row.get("completedFlows"), 0) or 0) > 0
            and (parse_int(row.get("completedFlowCount"), 0) or 0) > 0,
            f"completedFlows={row.get('completedFlows')} completedFlowCount={row.get('completedFlowCount')}",
        )
        expected_links = expected_link_counter_count(scenario)
        self.require(
            scenario,
            "link_counter_count",
            parse_int(row.get("linkCounterCount")) == expected_links,
            f"linkCounterCount={row.get('linkCounterCount')} expected={expected_links}",
        )
        self.require(
            scenario,
            "timeseries_enabled",
            is_true(row.get("linkTimeseriesEnabled")),
            f"linkTimeseriesEnabled={row.get('linkTimeseriesEnabled')}",
        )
        self.require(
            scenario,
            "timeseries_summary_rows",
            (parse_int(row.get("linkTimeseriesSampleRows"), 0) or 0) > 0,
            f"linkTimeseriesSampleRows={row.get('linkTimeseriesSampleRows')}",
        )
        if scenario.endswith("measured_later__seed0"):
            later_fields = [
                "measuredWecmpLaterProofPassed",
                "measuredWecmpLaterFlowCount",
                "measuredWecmpLaterFlowCompletedCount",
                "measuredWecmpLaterChangedSelectedSpineCount",
                "measuredWecmpLaterOcsLeakageDetected",
            ]
            self.require_fields(scenario, "summary_later_schema", rows, later_fields)
            self.require(
                scenario,
                "patch4c_later_proof_passed",
                is_true(row.get("measuredWecmpLaterProofPassed")),
                f"measuredWecmpLaterProofPassed={row.get('measuredWecmpLaterProofPassed')}",
            )
            self.require(
                scenario,
                "patch4c_later_flow_count",
                parse_int(row.get("measuredWecmpLaterFlowCount")) == 1
                and parse_int(row.get("measuredWecmpLaterFlowCompletedCount")) == 1,
                f"count={row.get('measuredWecmpLaterFlowCount')} completed={row.get('measuredWecmpLaterFlowCompletedCount')}",
            )
            self.require(
                scenario,
                "patch4c_later_changed_spine",
                (parse_int(row.get("measuredWecmpLaterChangedSelectedSpineCount"), 0) or 0) >= 1,
                f"changed={row.get('measuredWecmpLaterChangedSelectedSpineCount')}",
            )
            self.require(
                scenario,
                "patch4c_later_no_ocs_leakage",
                not is_true(row.get("measuredWecmpLaterOcsLeakageDetected")),
                f"leak={row.get('measuredWecmpLaterOcsLeakageDetected')}",
            )
        return row

    def validate_flows(self, scenario):
        path = self.file_path(scenario, "flows")
        if not self.require(scenario, "flows_exists", path.exists(), str(path)):
            return []
        rows = read_rows(path)
        self.require(scenario, "flows_nonempty", len(rows) > 0, f"rows={len(rows)}")
        fields = ["pathType", "fctSeconds", "rxDurationSeconds", "goodputMbps", "fallbackToEps", "frozenSpine"]
        self.require_fields(scenario, "flows_schema", rows, fields)
        if scenario.endswith("measured_later__seed0"):
            later_fields = [
                "isMeasuredLaterProofFlow",
                "measuredLaterSelectedSpine",
                "measuredLaterControlPlaneSelectedSpine",
                "measuredLaterSelectedSpineChanged",
                "measuredLaterHasMeasuredSample",
                "measuredLaterAppliesToLaterFlow",
            ]
            self.require_fields(scenario, "flows_later_schema", rows, later_fields)
            later_rows = [row for row in rows if is_true(row.get("isMeasuredLaterProofFlow"))]
            self.require(scenario, "flows_later_row_count", len(later_rows) == 1, f"rows={len(later_rows)}")
            if later_rows:
                row = later_rows[0]
                self.require(
                    scenario,
                    "flows_later_selected_spine_changed",
                    row.get("measuredLaterSelectedSpine") != row.get("measuredLaterControlPlaneSelectedSpine")
                    and is_true(row.get("measuredLaterSelectedSpineChanged")),
                    f"selected={row.get('measuredLaterSelectedSpine')} baseline={row.get('measuredLaterControlPlaneSelectedSpine')}",
                )
                self.require(
                    scenario,
                    "flows_later_completed_and_sampled",
                    is_true(row.get("completed"))
                    and is_true(row.get("measuredLaterHasMeasuredSample"))
                    and is_true(row.get("measuredLaterAppliesToLaterFlow")),
                    f"completed={row.get('completed')} hasSample={row.get('measuredLaterHasMeasuredSample')} applies={row.get('measuredLaterAppliesToLaterFlow')}",
                )
        return rows

    def validate_links(self, scenario, flows):
        path = self.file_path(scenario, "links")
        if not self.require(scenario, "links_exists", path.exists(), str(path)):
            return []
        rows = read_rows(path)
        expected_links = expected_link_counter_count(scenario)
        self.require(scenario, "links_row_count", len(rows) == expected_links, f"rows={len(rows)} expected={expected_links}")
        self.require_fields(scenario, "links_schema", rows, ["linkId", "linkType", "direction", "txBytes", "utilizationApprox"])
        tx = link_tx_by_id(rows)
        ocs_row, ocs_src, ocs_dst = find_selected_ocs_link(rows)
        self.require(scenario, "selected_ocs_link_present", ocs_row is not None, "found OCS a-to-b counter")
        ocs_tx = parse_int(ocs_row.get("txBytes"), 0) if ocs_row else 0

        if scenario.endswith("__hit__det__seed0"):
            self.require(scenario, "route_ocs_hit_uses_ocs", (ocs_tx or 0) > 0, f"ocsTx={ocs_tx}")
            if ocs_src is not None and ocs_dst is not None:
                self.require(
                    scenario,
                    "flows_ocs_hit_path",
                    any(
                        row.get("srcLeaf") == str(ocs_src)
                        and row.get("dstLeaf") == str(ocs_dst)
                        and row.get("pathType") == "ocs"
                        for row in flows
                    ),
                    f"{ocs_src}-{ocs_dst} flow has pathType=ocs",
                )
        else:
            self.require(scenario, "route_no_ocs_leakage", (ocs_tx or 0) == 0, f"ocsTx={ocs_tx}")

        if "admit_fallback__det" in scenario or "tl_ocs__fallback__det" in scenario:
            flow = first_matching_flow(flows, lambda row: row.get("pathType") in ("eps-fallback", "eps-residual"))
            if flow:
                src = parse_int(flow.get("srcLeaf"))
                dst = parse_int(flow.get("dstLeaf"))
                self.require(
                    scenario,
                    "route_deterministic_spine0",
                    tx(f"eps-leaf{src}-spine0-leaf-to-spine") > 0
                    and tx(f"eps-leaf{dst}-spine0-spine-to-leaf") > 0,
                    f"src={src} dst={dst} leaf-spine={tx(f'eps-leaf{src}-spine0-leaf-to-spine')} spine-leaf={tx(f'eps-leaf{dst}-spine0-spine-to-leaf')}",
                )
            else:
                self.require(scenario, "route_deterministic_spine0", False, "no EPS fallback/residual flow")

        if "cp_wecmp" in scenario or "measured_nosample" in scenario:
            flow = first_matching_flow(
                flows,
                lambda row: row.get("pathType") in ("eps-fallback", "eps-residual")
                and (parse_int(row.get("frozenSpine"), -1) or -1) >= 0,
            )
            if flow:
                src = parse_int(flow.get("srcLeaf"))
                dst = parse_int(flow.get("dstLeaf"))
                spine = parse_int(flow.get("frozenSpine"))
                self.require(
                    scenario,
                    "route_wecmp_frozen_spine",
                    tx(f"eps-leaf{src}-spine{spine}-leaf-to-spine") > 0
                    and tx(f"eps-leaf{dst}-spine{spine}-spine-to-leaf") > 0,
                    f"src={src} dst={dst} spine={spine}",
                )
            else:
                self.require(scenario, "route_wecmp_frozen_spine", False, "no frozen WECMP flow")

        if scenario.endswith("measured_later__seed0"):
            later = [row for row in flows if is_true(row.get("isMeasuredLaterProofFlow"))]
            selected = parse_int(later[0].get("measuredLaterSelectedSpine")) if later else None
            if later and selected is not None:
                src = parse_int(later[0].get("srcLeaf"))
                dst = parse_int(later[0].get("dstLeaf"))
                self.require(
                    scenario,
                    "route_measured_later_selected_spine",
                    tx(f"eps-leaf{src}-spine{selected}-leaf-to-spine") > 0
                    and tx(f"eps-leaf{dst}-spine{selected}-spine-to-leaf") > 0,
                    f"src={src} dst={dst} selected={selected}",
                )
            else:
                self.require(scenario, "route_measured_later_selected_spine", False, "no measured later flow")
        return rows

    def validate_timeseries(self, scenario):
        path = self.file_path(scenario, "link-timeseries")
        if not self.require(scenario, "link_timeseries_exists", path.exists(), str(path)):
            return
        rows = read_rows(path)
        self.require(scenario, "link_timeseries_nonempty", len(rows) > 0, f"rows={len(rows)}")
        self.require_fields(
            scenario,
            "link_timeseries_schema",
            rows,
            ["sampleTimeSeconds", "deltaTxBytes", "sampleThroughputMbps", "utilizationApprox"],
        )
        last_time = -1.0
        monotonic = True
        parseable = True
        nonzero = False
        for row in rows:
            sample_time = parse_float(row.get("sampleTimeSeconds"))
            util = parse_float(row.get("utilizationApprox"))
            delta = parse_int(row.get("deltaTxBytes"), 0) or 0
            if sample_time is None or util is None or util < 0:
                parseable = False
                break
            if sample_time < last_time:
                monotonic = False
            last_time = sample_time
            if delta > 0:
                nonzero = True
        self.require(scenario, "link_timeseries_monotonic", monotonic, "sampleTimeSeconds monotonic")
        self.require(scenario, "link_timeseries_util_parseable", parseable, "utilizationApprox parseable >= 0")
        self.require(scenario, "link_timeseries_nonzero_delta", nonzero, "at least one deltaTxBytes > 0")

    def validate_wecmp(self, scenario):
        needs_wecmp = "cp_wecmp" in scenario or "measured_" in scenario
        path = self.file_path(scenario, "wecmp")
        if not needs_wecmp:
            if path.exists():
                rows = read_rows(path)
                self.require(scenario, "wecmp_optional_exists", True, f"rows={len(rows)}")
            return
        if not self.require(scenario, "wecmp_exists", path.exists(), str(path)):
            return
        rows = read_rows(path)
        self.require(scenario, "wecmp_nonempty", len(rows) > 0, f"rows={len(rows)}")
        self.require_fields(scenario, "wecmp_schema", rows, ["selectedSpine", "loadSource", "controlPlaneSelectedSpine"])
        if "cp_wecmp" in scenario:
            self.require(
                scenario,
                "wecmp_control_plane_source",
                all(row.get("loadSource") == "control-plane" for row in rows),
                "all loadSource=control-plane",
            )
        if "measured_nosample" in scenario:
            self.require(
                scenario,
                "wecmp_measured_nosample_fallback",
                any(
                    row.get("loadSource") == "measured-snapshot"
                    and is_true(row.get("measuredDecisionFallback"))
                    and is_true(row.get("measuredNoSample"))
                    and not is_true(row.get("measuredDecisionUsed"))
                    for row in rows
                ),
                "measured fallback/no-sample rows exist",
            )
        if scenario.endswith("measured_later__seed0"):
            later_rows = [row for row in rows if is_true(row.get("appliesToLaterFlow"))]
            self.require(scenario, "wecmp_later_rows", len(later_rows) > 0, f"rows={len(later_rows)}")
            self.require(
                scenario,
                "wecmp_later_changed_spine",
                any(row.get("selectedSpine") != row.get("controlPlaneSelectedSpine") for row in later_rows),
                "selectedSpine differs from controlPlaneSelectedSpine",
            )
            self.require(
                scenario,
                "wecmp_later_measured_used",
                any(
                    is_true(row.get("measuredDecisionUsed"))
                    and not is_true(row.get("measuredDecisionFallback"))
                    and not is_true(row.get("measuredNoSample"))
                    for row in later_rows
                ),
                "measured used without fallback/no-sample",
            )

    def validate_measured_wecmp(self, scenario):
        needs_measured = "measured_" in scenario
        path = self.file_path(scenario, "measured-wecmp")
        if not needs_measured:
            return
        if not self.require(scenario, "measured_wecmp_exists", path.exists(), str(path)):
            return
        rows = read_rows(path)
        expected_rows = expected_measured_wecmp_rows(scenario)
        self.require(scenario, "measured_wecmp_row_count", len(rows) == expected_rows, f"rows={len(rows)} expected={expected_rows}")
        self.require_fields(scenario, "measured_wecmp_schema", rows, ["hasSample", "measuredUtilization"])
        parseable = True
        for row in rows:
            util = parse_float(row.get("measuredUtilization"))
            if util is None or util < 0:
                parseable = False
                break
        self.require(scenario, "measured_wecmp_util_parseable", parseable, "measuredUtilization parseable >= 0")

    def validate_ocs_candidates(self, scenario):
        path = self.file_path(scenario, "ocs-candidates")
        self.require(scenario, "ocs_candidates_exists", path.exists(), str(path))

    def run(self):
        self.validate_manifest()
        for scenario in self.scenarios:
            self.validate_ocs_candidates(scenario)
            self.validate_summary(scenario)
            flows = self.validate_flows(scenario)
            self.validate_links(scenario, flows)
            self.validate_timeseries(scenario)
            self.validate_wecmp(scenario)
            self.validate_measured_wecmp(scenario)
        self.write_report()
        passed = sum(1 for row in self.rows if row["status"] == "pass")
        warnings = sum(1 for row in self.rows if row["status"] == "warn")
        print(f"Validation checks: {passed} passed, {warnings} warnings, {self.failures} failed")
        print(f"Validation report: {self.report_path}")
        return 0 if self.failures == 0 else 1

    def write_report(self):
        self.report_path.parent.mkdir(parents=True, exist_ok=True)
        with self.report_path.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["experimentName", "check", "status", "message"])
            writer.writeheader()
            writer.writerows(self.rows)


def scenarios_from_manifest(path):
    if not path.exists():
        return []
    return [row.get("experimentName", "") for row in read_rows(path) if row.get("experimentName")]


def main():
    parser = argparse.ArgumentParser(description="Validate TL-OCS experiment outputs.")
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--manifest")
    parser.add_argument("--csv-dir")
    parser.add_argument("--report")
    parser.add_argument("--suite", choices=["smoke", "medium"], default="smoke")
    parser.add_argument("--scenarios-from-manifest", action="store_true")
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    run_dir = Path(args.run_dir)
    manifest = Path(args.manifest) if args.manifest else run_dir / "manifest.csv"
    csv_dir = Path(args.csv_dir) if args.csv_dir else run_dir / "csv"
    report = Path(args.report) if args.report else run_dir / "validation-report.csv"

    if args.scenarios_from_manifest:
        scenarios = scenarios_from_manifest(manifest)
    elif args.suite == "medium":
        scenarios = MEDIUM_SCENARIOS
    else:
        scenarios = SMOKE_SCENARIOS

    if not scenarios:
        print("ERROR: no scenarios to validate", file=sys.stderr)
        return 2

    validator = Validator(run_dir, csv_dir, manifest, report, args.suite, scenarios, args.strict)
    return validator.run()


if __name__ == "__main__":
    sys.exit(main())

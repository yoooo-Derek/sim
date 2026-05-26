# TL-OCS Small-Smoke Experiments

This directory contains the Patch 5B small-smoke experiment harness. It is for engineering validation only.

It does not run paper-candidate experiments, does not produce paper conclusions, does not draw figures, and does not write to `results/raw`.

## Scripts

- `run_smoke_matrix.sh`: runs the six required small-smoke scenarios and then invokes validation.
- `run_medium_sanity.sh`: runs a four-scenario 8-leaf medium-sanity subset and then invokes validation and summary collection.
- `validate_outputs.py`: validates CSV existence, schema, result status, route correctness, link time series, measured WECMP, and the Patch 4C later-flow proof.
- `collect_summary.py`: helper that combines `*-summary.csv` files into one CSV table with parsed experiment metadata. It does not draw figures or make statistical claims.

## Running

Default output root:

`/tmp/tl-ocs-experiments`

Default run id:

`smoke-YYYYmmdd-HHMMSS`

Run the smoke matrix:

```sh
scripts/tl_ocs_experiments/run_smoke_matrix.sh
```

Optional environment overrides:

```sh
RUN_ID=my-smoke RUN_ROOT=/tmp/tl-ocs-experiments scripts/tl_ocs_experiments/run_smoke_matrix.sh
```

Run the medium-sanity subset:

```sh
scripts/tl_ocs_experiments/run_medium_sanity.sh
```

Medium-sanity is still an engineering readiness check. It is not a paper-candidate sweep and must not be cited as a final paper result.

The script uses `NS3_BIN` if set. Otherwise it expects:

`/home/dyn/ns-3.47/build/scratch/ns3.47-hybrid-dcn-main-optimized`

If that binary does not exist, build it first outside this script:

```sh
/home/dyn/ns-3.47/ns3 build
```

## Output Layout

For run id `<run-id>`, outputs are written to:

`/tmp/tl-ocs-experiments/<run-id>`

Layout:

- `logs/<experimentName>.log`
- `csv/<experimentName>-summary.csv`
- `csv/<experimentName>-flows.csv`
- `csv/<experimentName>-links.csv`
- `csv/<experimentName>-link-timeseries.csv`
- `csv/<experimentName>-wecmp.csv`
- `csv/<experimentName>-measured-wecmp.csv`
- `csv/<experimentName>-ocs-candidates.csv`
- `manifest.csv`
- `validation-report.csv`
- `combined-summary.csv` for medium runs, and for smoke runs when `collect_summary.py` is invoked manually

The runner creates an `ns3-cwd` directory under the run directory so relative simulator artifacts land under the same `/tmp` run tree rather than the repository's historical `results/raw`.

## Smoke Scenarios

The required six scenarios are:

1. `smoke__4l2s2h__skewed__static_ocs03__hit__det__seed0`
2. `smoke__4l2s2h__skewed__static_ocs03__admit_fallback__det__seed0`
3. `smoke__4l2s2h__skewed__tl_ocs__fallback__det__seed0`
4. `smoke__4l2s2h__skewed__tl_ocs__fallback__cp_wecmp__seed0`
5. `smoke__4l2s2h__skewed__tl_ocs__fallback__measured_nosample__seed0`
6. `smoke__4l2s2h__skewed__tl_ocs__fallback__measured_later__seed0`

All scenarios use 4 leaves, 2 spines, 2 servers per leaf, `simTime=1.5`, structured CSV export, and `linkUtilizationSampleInterval=0.1`.

## Medium-Sanity Scenarios

`run_medium_sanity.sh` runs four scenarios:

1. `medium__8l4s2h__skewed__static_ocs07__hit__det__seed0`
2. `medium__8l4s2h__skewed__static_ocs07__admit_fallback__det__seed0`
3. `medium__8l4s2h__skewed__tl_ocs__fallback__cp_wecmp__seed0`
4. `medium__8l4s2h__skewed__tl_ocs__fallback__measured_later__seed0`

All medium scenarios use 8 leaves, 4 spines, 2 servers per leaf, `simTime=3.0`, structured CSV export, and `linkUtilizationSampleInterval=0.1`.

## Validation

Run validation directly:

```sh
scripts/tl_ocs_experiments/validate_outputs.py --run-dir /tmp/tl-ocs-experiments/<run-id> --strict
```

Use the suite selector for medium outputs:

```sh
scripts/tl_ocs_experiments/validate_outputs.py --run-dir /tmp/tl-ocs-experiments/<run-id> --suite medium --scenarios-from-manifest --strict
```

`--suite smoke` remains the default for Patch 5B compatibility.

Validation checks include:

- required CSV files exist;
- required schema fields exist;
- `overallResultConsistency=pass`;
- `overallAlgorithmInvariant=pass`;
- `linkCounterCount=18`;
- link time-series rows are present and parseable;
- OCS hit uses the selected OCS link;
- fallback / WECMP / measured-later EPS flows do not leak to selected OCS 0-3;
- deterministic fallback uses spine0;
- control-plane WECMP uses the expected frozen spine;
- measured no-sample fallback records fallback/no-sample;
- measured later proof records `measuredWecmpLaterProofPassed=true`;
- `measured-wecmp.csv` has 16 EPS directed rows for measured WECMP scenarios.

For medium 8l4s2h scenarios, validation infers expected counts from the experiment name:

- `linkCounterCount = 8 * 4 * 2 + 2 = 66`
- `measured-wecmp.csv` rows = `8 * 4 * 2 = 64`

## Summary Collection

Run:

```sh
scripts/tl_ocs_experiments/collect_summary.py --run-dir /tmp/tl-ocs-experiments/<run-id>
```

The combined table adds:

- `sourceFile`
- `suite`
- `scale`
- `traffic`
- `baseline`
- `admission`
- `wecmp`
- `seed`

These fields are parsed from:

`<suite>__<scale>__<traffic>__<baseline>__<admission>__<wecmp>__seed<seed>`

If a name does not match, metadata fields are set to `unknown` and a warning is printed. The collector does not plot, compute confidence intervals, or make statistical conclusions.

Do not run paper-candidate from these scripts. Add a separate reviewed patch before any paper-candidate sweep.

If validation fails, inspect:

- `validation-report.csv`
- `logs/<experimentName>.log`
- the scenario-specific CSV files under `csv/`

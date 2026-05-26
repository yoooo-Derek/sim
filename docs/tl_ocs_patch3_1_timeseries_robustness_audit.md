# TL-OCS Patch 3.1 Link Timeseries Robustness Audit

## 1. 目标和边界

本任务审计 Patch 3 的 per-link measured Tx utilization time series 导出在边界配置下的稳定性和语义，不做功能开发。

本任务未修改源码，只新增本审计文档。

未修改内容：

- 未修改 TL-OCS 控制算法；
- 未修改 OCS admission；
- 未修改 EPS fallback / residual；
- 未修改 EPS-WECMP decision / selectedSpine / frozenSpine；
- 未修改 Patch 2.6 route fix；
- 未修改拓扑、链路速率、链路时延；
- 未修改 BulkSend / matrix flow generation；
- 未接入 WECMP measured utilization；
- 未写入 `results/raw`。

## 2. 运行环境和输出目录

工作目录：`/home/dyn/sim`

运行工作目录：`/tmp/tl-ocs-patch3-1-timeseries-robustness/ns3-cwd`

所有日志和 CSV 输出目录：`/tmp/tl-ocs-patch3-1-timeseries-robustness`

本轮未重新修改 `src/main/hybrid-dcn-main.cc`。审计基于当前 Patch 3 工作树运行。

## 3. 审计场景

所有场景均使用 4 leaves、2 spines、2 servers per leaf、1 selected OCS link、`routeMode=ocs-forced`、`trafficMatrixMode=skewed`、`communityMode=louvain`、`selectionMetric=community-excess`。

| 场景 | 参数摘要 | 返回码 |
|---|---|---:|
| `patch3-1-default-ocs-hit` | `simTime=1.5`, `linkUtilizationSampleInterval=0.1`, structured export on, OCS hit | 0 |
| `patch3-1-default-admission-fallback` | `simTime=1.5`, `linkUtilizationSampleInterval=0.1`, structured export on, admission fallback | 0 |
| `patch3-1-default-wecmp-binding` | `simTime=1.5`, `linkUtilizationSampleInterval=0.1`, structured export on, WECMP binding | 0 |
| `patch3-1-disabled-zero` | `simTime=1.5`, `linkUtilizationSampleInterval=0`, structured export on, OCS hit | 0 |
| `patch3-1-disabled-negative` | `simTime=1.5`, `linkUtilizationSampleInterval=-1`, structured export on, OCS hit | 0 |
| `patch3-1-export-disabled` | `simTime=1.5`, `linkUtilizationSampleInterval=0.1`, structured export off, OCS hit | 0 |
| `patch3-1-short-simtime` | `simTime=0.35`, `linkUtilizationSampleInterval=0.1`, structured export on, OCS hit command shape | 1 |
| `patch3-1-short-simtime-sampling-only` | `simTime=0.35`, `linkUtilizationSampleInterval=0.1`, structured export on, `enableMatrixFlows=false` to isolate sampling schedule | 0 |

The requested short OCS-hit command with `simTime=0.35` fails before simulation because the existing matrix-flow parameter validation requires `matrixFlowStart < simTime`, and default `matrixFlowStart` is `0.35`. This is a command-level audit finding, not a Patch 3 sampling crash. A supplementary sampling-only run was used to verify the expected 0.1, 0.2, 0.3 sampling schedule without changing source code.

## 4. CSV 文件存在性

| 场景 | summary | flows | links | link-timeseries |
|---|---|---|---|---|
| `patch3-1-default-ocs-hit` | yes | yes | yes | yes |
| `patch3-1-default-admission-fallback` | yes | yes | yes | yes |
| `patch3-1-default-wecmp-binding` | yes | yes | yes | yes |
| `patch3-1-disabled-zero` | yes | yes | yes | yes |
| `patch3-1-disabled-negative` | yes | yes | yes | yes |
| `patch3-1-export-disabled` | no | no | no | no |
| `patch3-1-short-simtime` | no | no | no | no |
| `patch3-1-short-simtime-sampling-only` | yes | yes | yes | yes |

For `patch3-1-export-disabled`, logs show the export target paths, then `enabled=false` and `exportSuccess=false`; the simulation still completes with `[OK]` and `overallAlgorithmInvariant=pass`.

For disabled sampling (`0` or negative interval), `link-timeseries.csv` is generated as header-only. This is acceptable and semantically clear because summary reports `linkTimeseriesEnabled=false` and zero sample rows.

## 5. Summary time-series 字段检查

All structured-export-enabled completed scenarios contain the Patch 3 summary fields:

- `linkUtilizationSampleIntervalSeconds`
- `linkTimeseriesEnabled`
- `linkTimeseriesSampleRows`
- `linkTimeseriesNonzeroSampleRows`
- `linkTimeseriesMaxUtilizationApprox`
- `linkTimeseriesAvgUtilizationApprox`
- `linkTimeseriesMaxEpsUtilizationApprox`
- `linkTimeseriesMaxOcsUtilizationApprox`

| 场景 | enabled | interval | rows | nonzero | max | avg | maxEps | maxOcs | overall | invariant |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| `patch3-1-default-ocs-hit` | true | 0.1 | 252 | 8 | 0.00230928 | 2.10883e-05 | 0.00230928 | 0.000461856 | pass | pass |
| `patch3-1-default-admission-fallback` | true | 0.1 | 252 | 8 | 0.00346392 | 2.87568e-05 | 0.00346392 | 0 | pass | pass |
| `patch3-1-default-wecmp-binding` | true | 0.1 | 252 | 8 | 0.00346392 | 2.87568e-05 | 0.00346392 | 0 | pass | pass |
| `patch3-1-disabled-zero` | false | 0 | 0 | 0 | 0 | 0 | 0 | 0 | pass | pass |
| `patch3-1-disabled-negative` | false | -1 | 0 | 0 | 0 | 0 | 0 | 0 | pass | pass |
| `patch3-1-short-simtime-sampling-only` | true | 0.1 | 54 | 0 | 0 | 0 | 0 | 0 | fail | pass |

The sampling-only short run has `overallResultConsistency=fail` because matrix flows are disabled, so it is not used as a result-consistency or route-fix scenario. It is only used to audit the sampling schedule and row count.

## 6. link-timeseries 行数和语义检查

| 场景 | CSV data rows | nonzero delta rows | sampleTimeSeconds monotonic | utilizationApprox parseable and >= 0 |
|---|---:|---:|---|---|
| `patch3-1-default-ocs-hit` | 252 | 8 | true | true |
| `patch3-1-default-admission-fallback` | 252 | 8 | true | true |
| `patch3-1-default-wecmp-binding` | 252 | 8 | true | true |
| `patch3-1-disabled-zero` | 0 | 0 | true | true |
| `patch3-1-disabled-negative` | 0 | 0 | true | true |
| `patch3-1-short-simtime-sampling-only` | 54 | 0 | true | true |

For default 1.5s scenarios, 252 rows correspond to 14 sample batches times 18 direction-level link counters. The final scheduled batch is at 1.4s because the next 1.5s sample is not scheduled under the current `now + interval <= stopTime + 1e-9` scheduling sequence after floating-point accumulation.

For the supplementary 0.35s sampling-only run, sample batches are exactly:

| sampleIndex | sampleTimeSeconds |
|---:|---:|
| 0 | 0.1 |
| 1 | 0.2 |
| 2 | 0.3 |

That gives `3 * 18 = 54` data rows, matching the expected row count.

## 7. links.csv counter count

All structured-export-enabled completed scenarios keep 18 direction-level aggregate counters:

- EPS leaf-spine: `4 * 2 * 2 = 16`
- OCS: `1 * 2 = 2`

| 场景 | links.csv data rows |
|---|---:|
| `patch3-1-default-ocs-hit` | 18 |
| `patch3-1-default-admission-fallback` | 18 |
| `patch3-1-default-wecmp-binding` | 18 |
| `patch3-1-disabled-zero` | 18 |
| `patch3-1-disabled-negative` | 18 |
| `patch3-1-short-simtime-sampling-only` | 18 |

## 8. Patch 2.6 route fix 回归

| 场景 | flow0 pathType | flow0 frozenSpine | OCS 0-3 a-to-b txBytes | EPS confirming direction | 结论 |
|---|---|---:|---:|---|---|
| `patch3-1-default-ocs-hit` | `ocs` | -1 | 577320 | not required | OCS admitted flow still uses OCS. |
| `patch3-1-default-admission-fallback` | `eps-fallback` | -1 | 0 | `spine0->leaf3 txBytes=577320` | Admission fallback does not leak to OCS and uses EPS. |
| `patch3-1-default-wecmp-binding` | `eps-fallback` | 1 | 0 | `spine1->leaf3 txBytes=577320` | WECMP fallback does not leak to OCS and uses frozenSpine 1. |

Additional aggregate directions:

| 场景 | leaf0->spine0 | leaf0->spine1 | spine0->leaf3 | spine1->leaf3 |
|---|---:|---:|---:|---:|
| `patch3-1-default-ocs-hit` | 1154640 | 0 | 0 | 0 |
| `patch3-1-default-admission-fallback` | 1731960 | 0 | 577320 | 0 |
| `patch3-1-default-wecmp-binding` | 0 | 1731960 | 0 | 577320 |

Patch 2.6 route fix remains intact in the default sampling scenarios.

## 9. 审计结论

Patch 3.1 passes for the Patch 3 time-series robustness scope, with one documented command-level caveat:

- `linkUtilizationSampleInterval=0.1` works and produces valid measured Tx time-series rows.
- `linkUtilizationSampleInterval=0` disables sampling cleanly.
- `linkUtilizationSampleInterval=-1` is equivalent to disabled and does not crash.
- Disabled sampling currently writes a header-only `link-timeseries.csv` when structured export is enabled; summary rows report zero samples and zero utilization, so the behavior is semantically clear.
- `enableStructuredResultExport=false` does not produce CSV files and does not crash. Logs still print target CSV paths before returning `exportSuccess=false`, matching the current structured export logging pattern.
- `simTime=0.35` with default OCS-hit matrix-flow settings is invalid because `matrixFlowStart=0.35`; this is not a timeseries failure. A sampling-only 0.35s run confirms the expected 54 rows.
- Patch 2.6 route fix is preserved.

Patch 3 / Patch 3.1 only export measured link utilization observations. They do not feed measured utilization into WECMP. It is reasonable to move to Patch 4 design as a separate task if the next goal is WECMP measured utilization integration.

# TL-OCS Patch 4B Measured Snapshot Data Path Validation

## 1. Patch 4B 目标和边界

Patch 4B 实现 EPS-WECMP measured snapshot 数据通路和 CSV 证明。它不要求 measured utilization 改变 `selectedSpine`，也不新增二阶段 later flow。

本补丁目标：

- 默认 `epsWecmpLoadSource=control-plane` 保持旧 WECMP 行为；
- 显式 `epsWecmpLoadSource=measured-snapshot` 时，WECMP decision 尝试读取 runtime measured EPS directed link snapshot；
- 当前 WECMP decision 仍在 `Simulator::Run()` 前发生，因此 measured mode 在既有场景中没有 sample，应按 `measuredWecmpNoSampleFallback` fallback；
- fallback / no-sample 语义写入 summary 和 `wecmp.csv`；
- Patch 3 `link-timeseries.csv` 继续可用；
- Patch 2.6 route fix 保持。

## 2. 修改范围

修改文件：

| 文件 | 修改内容 |
|---|---|
| `src/main/hybrid-dcn-main.cc` | 新增 WECMP load source 参数；新增 runtime measured EPS directed snapshot；采样回调更新 snapshot；WECMP decision 追加 measured/fallback 语义字段；summary / wecmp CSV 追加字段；新增 `measured-wecmp.csv`。 |
| `src/eps/eps-wecmp-state.h` | 为 `EpsWecmpDecision` 和 `EpsWecmpLinkState` 追加 measured snapshot 语义字段。 |
| `docs/tl_ocs_patch4b_measured_snapshot_data_path_validation.md` | 本验证文档。 |

未修改内容：

- 未修改 TL-OCS 控制算法、Louvain、`B_ij`、`G_ij`、OCS candidate selection；
- 未修改 OCS admission；
- 未修改 EPS fallback / residual 判定；
- 未修改 Patch 2.6 route fix 的核心 route 安装逻辑；
- 未修改 Patch 3 `link-timeseries.csv` schema；
- 未修改拓扑、链路速率、链路时延；
- 未修改 BulkSend / matrix flow generation 主逻辑；
- 未新增二阶段 later flow；
- 未实现 selectedSpine change proof；
- 未做 packet-level rerouting；
- 未改变已建立 flow；
- 未写入 `results/raw`。

## 3. 新增参数

| 参数 | 默认值 | 可选值 | 语义 |
|---|---|---|---|
| `epsWecmpLoadSource` | `control-plane` | `control-plane`, `measured-snapshot` | EPS-WECMP load source。默认保持旧行为。 |
| `measuredWecmpNoSampleFallback` | `control-plane` | `control-plane`, `zero`, `error` | measured mode 没有 runtime sample 时的处理方式。 |
| `measuredWecmpWarmupTime` | `0.0` | 非负 double | Patch 4B 只记录和导出该字段，不新增 delayed decision。 |

非法 `epsWecmpLoadSource` 会报错并返回非零。`measuredWecmpNoSampleFallback=error` 已实现为 no-sample 时返回非零，但主验证使用 `control-plane` fallback。

## 4. Measured snapshot 数据结构和映射

新增 runtime-only snapshot，不从 CSV 读回：

`MeasuredEpsDirectedLinkSnapshot` 字段：

- `hasSample`
- `lastSampleTime`
- `utilizationApprox`
- `sampleThroughputMbps`
- `deltaTxBytes`
- `cumulativeTxBytes`
- `capacityMbps`
- `updateCount`

容器：

- `epsMeasuredLeafToSpineSnapshots[leaf][spine]`
- `epsMeasuredSpineToLeafSnapshots[leaf][spine]`

映射规则：

- 只处理 `linkType == eps-leaf-spine`；
- `endpointAType=leaf, endpointBType=spine` 映射到 leaf-to-spine；
- `endpointAType=spine, endpointBType=leaf` 映射到 spine-to-leaf；
- OCS counters 不进入 WECMP measured snapshot；
- snapshot 在 `SampleLinkUtilizationTimeSeries` runtime callback 中更新；
- Patch 3 `linkUtilizationSamples` 和 `link-timeseries.csv` 不变。

Path-level measured load：

- 使用 `srcLeaf -> spine` 和 `spine -> dstLeaf` 两个 directed snapshots；
- `epsWecmpPathMetric=max` 时取两段 utilization 的 max；
- `epsWecmpPathMetric=average` 时取平均；
- 任一方向缺 sample 时，candidate 标记 `hasMeasuredSample=false`。

## 5. WECMP decision 字段扩展

`EpsWecmpDecision` 追加：

- `loadSource`
- `noSampleFallbackMode`
- `decisionTimeSeconds`
- `measuredDecisionRequested`
- `measuredDecisionUsed`
- `measuredDecisionFallback`
- `measuredNoSample`
- `appliesToLaterFlow`
- `controlPlaneSelectedSpine`

`EpsWecmpLinkState` 追加：

- `hasMeasuredSample`
- `measuredSrcToSpineUtilization`
- `measuredSpineToDstUtilization`
- `measuredPathUtilization`
- `controlPlanePathLoadMetric`
- `effectivePathLoadMetric`

Control-plane mode 下旧字段仍保持旧语义，追加字段只记录 `loadSource=control-plane` 和 measured=false。

Measured-snapshot mode 且 no sample 时：

- `measuredDecisionRequested=true`
- `hasMeasuredSample=false`
- `measuredDecisionUsed=false`
- `measuredDecisionFallback=true`
- `measuredNoSample=true`
- `effectivePathLoadMetric` 在 control-plane fallback 下等于 `controlPlanePathLoadMetric`
- `selectedSpine` 与旧 control-plane 决策保持一致

Patch 4B 中 `appliesToLaterFlow=false`，因为没有二阶段 later flow。

## 6. CSV 变更

### summary.csv 追加字段

- `epsWecmpLoadSource`
- `measuredWecmpEnabled`
- `measuredWecmpNoSampleFallback`
- `measuredWecmpWarmupTime`
- `measuredWecmpSnapshotCount`
- `measuredWecmpSnapshotUpdateCount`
- `measuredWecmpDecisionCount`
- `measuredWecmpFallbackDecisionCount`
- `measuredWecmpNoSampleDecisionCount`
- `measuredWecmpCandidateRowsWithSample`
- `measuredWecmpCandidateRowsWithoutSample`
- `measuredWecmpChangedSelectedSpineCount`

`measuredWecmpSnapshotCount` 口径：仿真结束时 latest EPS directed snapshot entries 中 `hasSample=true` 的数量。4 leaves、2 spines 下最多为 `4 * 2 * 2 = 16`。

### wecmp.csv 追加字段

- `loadSource`
- `hasMeasuredSample`
- `measuredSrcToSpineUtilization`
- `measuredSpineToDstUtilization`
- `measuredPathUtilization`
- `controlPlanePathLoadMetric`
- `effectivePathLoadMetric`
- `noSampleFallbackMode`
- `measuredDecisionRequested`
- `measuredDecisionUsed`
- `measuredDecisionFallback`
- `measuredNoSample`
- `decisionTimeSeconds`
- `appliesToLaterFlow`

旧字段未删除、未重命名。

### 新增 measured-wecmp.csv

新增 `<experimentName>-measured-wecmp.csv`，只导出 EPS leaf-spine directed snapshot，不包含 OCS。

字段：

- `experimentName`
- `sampleTimeSeconds`
- `leaf`
- `spine`
- `direction`
- `capacityMbps`
- `deltaTxBytes`
- `cumulativeTxBytes`
- `sampleThroughputMbps`
- `measuredUtilization`
- `hasSample`

这是 WECMP 视角的 latest measured snapshot 投影；Patch 3 `link-timeseries.csv` 仍是完整 time-series。

## 7. 验证场景

输出目录：`/tmp/tl-ocs-patch4b-measured-snapshot`

所有场景 stdout/stderr 写入同目录 `.log` 文件。

| 场景 | 参数摘要 | 返回码 |
|---|---|---:|
| `patch4b-backward-wecmp-binding` | WECMP binding, `epsWecmpLoadSource=control-plane` | 0 |
| `patch4b-measured-nosample-fallback` | WECMP binding, `epsWecmpLoadSource=measured-snapshot`, `measuredWecmpNoSampleFallback=control-plane` | 0 |
| `patch4b-measured-disabled-timeseries` | WECMP binding, measured mode, fallback control-plane, `linkUtilizationSampleInterval=0` | 0 |
| `patch4b-invalid-load-source` | WECMP binding, `epsWecmpLoadSource=bad-value` | 1 |
| `patch4b-admission-fallback-route-fix` | Admission fallback, control-plane mode | 0 |

Invalid load source 报错：

`[HYBRID-DCN][ERROR] epsWecmpLoadSource must be control-plane or measured-snapshot.`

## 8. CSV 文件存在性

| 场景 | summary | flows | links | link-timeseries | wecmp | measured-wecmp |
|---|---|---|---|---|---|---|
| `patch4b-backward-wecmp-binding` | yes | yes | yes | yes | yes | yes |
| `patch4b-measured-nosample-fallback` | yes | yes | yes | yes | yes | yes |
| `patch4b-measured-disabled-timeseries` | yes | yes | yes | yes | yes | yes |
| `patch4b-admission-fallback-route-fix` | yes | yes | yes | yes | yes | yes |

`patch4b-invalid-load-source` 返回非零，不要求生成 CSV。

## 9. Summary 检查

| 场景 | source | measured enabled | snapshots | updates | decisions | fallback | no-sample | cand with sample | cand without sample | changed selectedSpine | ts rows | overall | invariant |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| `patch4b-backward-wecmp-binding` | control-plane | false | 16 | 224 | 0 | 0 | 0 | 0 | 0 | 0 | 252 | pass | pass |
| `patch4b-measured-nosample-fallback` | measured-snapshot | true | 16 | 224 | 3 | 3 | 3 | 0 | 6 | 0 | 252 | pass | pass |
| `patch4b-measured-disabled-timeseries` | measured-snapshot | true | 0 | 0 | 3 | 3 | 3 | 0 | 6 | 0 | 0 | pass | pass |
| `patch4b-admission-fallback-route-fix` | control-plane | false | 16 | 224 | 0 | 0 | 0 | 0 | 0 | 0 | 252 | pass | pass |

Interpretation:

- Backward compatibility mode does not request measured WECMP decisions.
- Measured mode has no pre-run samples, so all 3 WECMP decisions fallback to control-plane.
- Candidate rows are 3 decisions * 2 spines = 6.
- `measuredWecmpChangedSelectedSpineCount=0`, as expected for Patch 4B.

## 10. wecmp.csv no-sample fallback 检查

For `patch4b-measured-nosample-fallback`, each WECMP candidate row has:

- `loadSource=measured-snapshot`
- `hasMeasuredSample=false`
- `measuredDecisionRequested=true`
- `measuredDecisionUsed=false`
- `measuredDecisionFallback=true`
- `measuredNoSample=true`
- `effectivePathLoadMetric == controlPlanePathLoadMetric`
- `appliesToLaterFlow=false`

Selected spine remains `1` in both control-plane and measured no-sample fallback scenarios.

## 11. Patch 2.6 route fix 回归

| 场景 | flow0 pathType | flow0 frozenSpine | OCS 0-3 a-to-b txBytes | EPS confirming direction |
|---|---|---:|---:|---|
| `patch4b-backward-wecmp-binding` | `eps-fallback` | 1 | 0 | `spine1->leaf3 txBytes=577320` |
| `patch4b-measured-nosample-fallback` | `eps-fallback` | 1 | 0 | `spine1->leaf3 txBytes=577320` |
| `patch4b-measured-disabled-timeseries` | `eps-fallback` | 1 | 0 | `spine1->leaf3 txBytes=577320` |
| `patch4b-admission-fallback-route-fix` | `eps-fallback` | -1 | 0 | `spine0->leaf3 txBytes=577320` |

WECMP binding and admission fallback both keep fallback traffic off selected OCS 0-3.

## 12. Patch 3 link-timeseries 回归

| 场景 | rows | nonzero delta rows | monotonic sampleTime | utilization parseable and >= 0 |
|---|---:|---:|---|---|
| `patch4b-backward-wecmp-binding` | 252 | 8 | true | true |
| `patch4b-measured-nosample-fallback` | 252 | 8 | true | true |
| `patch4b-measured-disabled-timeseries` | 0 | 0 | true | true |
| `patch4b-admission-fallback-route-fix` | 252 | 8 | true | true |

Disabled timeseries keeps Patch 3.1 semantics: `link-timeseries.csv` is header-only, summary has `linkTimeseriesEnabled=false` and `linkTimeseriesSampleRows=0`.

## 13. measured-wecmp.csv 检查

Each generated `measured-wecmp.csv` has 16 data rows: 4 leaves * 2 spines * 2 directions.

| 场景 | rows | hasSample rows | cumulative nonzero rows |
|---|---:|---:|---:|
| `patch4b-backward-wecmp-binding` | 16 | 16 | 8 |
| `patch4b-measured-nosample-fallback` | 16 | 16 | 8 |
| `patch4b-measured-disabled-timeseries` | 16 | 0 | 0 |

For the enabled time-series scenarios, the latest snapshot entries all have `hasSample=true`; only active EPS directed links have nonzero cumulative bytes. Latest `deltaTxBytes` can be 0 because the final sample window may have no traffic after flow completion.

## 14. 结论

Patch 4B passes.

What is proven:

- Backward compatibility is preserved for `epsWecmpLoadSource=control-plane`.
- Measured-snapshot mode is explicit.
- Runtime sampling updates EPS directed measured snapshots without reading post-run CSV.
- Current pre-run WECMP decisions correctly detect no measured sample and fallback to control-plane.
- CSVs clearly expose load source, no-sample, fallback, measured candidate availability, and effective path load.
- Patch 2.6 route fix is preserved.
- Patch 3 `link-timeseries.csv` is preserved.

What is not proven:

- Patch 4B does not prove measured utilization changes `selectedSpine`.
- Patch 4B does not add two-stage later flow.
- Patch 4B does not reroute established flows.

It is appropriate to enter Patch 4C next. Patch 4C should focus narrowly on a two-stage later flow design where measured snapshot can influence only a later new flow's `selectedSpine`, while preserving Patch 2.6 route fix and Patch 3/4B CSV observability. It is still not appropriate to move directly to large-scale paper experiments.

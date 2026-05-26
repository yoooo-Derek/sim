# TL-OCS Patch 5A Experiment Baseline Plan

## 1. 目标和边界

Patch 5A 是论文实验脚本和 baseline 框架的设计审计，不是源码功能开发，也不是批量实验。

目标：

- 梳理当前可用 CSV 和指标能力；
- 设计 baseline 组合；
- 设计场景族、参数模板、输出目录和 experimentName 规范；
- 设计 Patch 5B 可执行的最小脚本集合；
- 设计 smoke test 验收口径；
- 明确哪些内容现在可以跑，哪些仍不能跑。

本任务未运行 ns-3 仿真，未生成实验 CSV，未修改源码，未新增可执行 runner，未写入 `results/raw`。

## 2. 已完成工程能力

已审计文档均存在：

| 文档 | 能力摘要 |
|---|---|
| `docs/tl_ocs_patch1_metrics_validation.md` | 新增 flow FCT、rx duration、path type，以及 summary 派生指标。 |
| `docs/tl_ocs_patch2_link_counters_validation.md` | 新增 direction-level `links.csv` aggregate Tx counters。 |
| `docs/tl_ocs_patch2_6_route_fix_validation.md` | 修复 fallback / residual OCS leakage，显式 EPS route 保持真实数据面路径。 |
| `docs/tl_ocs_patch3_link_timeseries_validation.md` | 新增 direction-level measured Tx utilization time series。 |
| `docs/tl_ocs_patch3_1_timeseries_robustness_audit.md` | 审计默认、禁用、负间隔、短 simTime 等 time-series 边界语义。 |
| `docs/tl_ocs_patch4a_wecmp_measured_utilization_design.md` | 设计 measured utilization 接入 WECMP 的风险和拆分策略。 |
| `docs/tl_ocs_patch4b_measured_snapshot_data_path_validation.md` | 新增 measured snapshot data path、no-sample fallback、`measured-wecmp.csv`。 |
| `docs/tl_ocs_patch4c_later_flow_selected_spine_validation.md` | 证明 runtime measured snapshot 可改变 later new flow 的 `selectedSpine`。 |

当前结构化输出能力：

| 文件 | 主要用途 |
|---|---|
| `<experimentName>-summary.csv` | 每个实验一行汇总：FCT、goodput、OCS/EPS share、route/invariant、link/WECMP/later proof 摘要。 |
| `<experimentName>-flows.csv` | flow 粒度：路径类型、FCT、goodput、fallback、frozenSpine、later proof 字段。 |
| `<experimentName>-links.csv` | direction-level aggregate Tx packets/bytes/utilization。 |
| `<experimentName>-link-timeseries.csv` | direction-level measured Tx utilization sample rows。 |
| `<experimentName>-wecmp.csv` | WECMP candidate spine rows、control-plane / measured decision 字段。 |
| `<experimentName>-measured-wecmp.csv` | EPS directed link measured snapshot projection。 |
| `<experimentName>-ocs-candidates.csv` | TL-OCS candidate edges、selection score、selected/reject reason。 |

## 3. 当前仍不能声称的内容

- 不能声称已经完成论文规模实验。
- 不能声称 measured WECMP 已支持全局动态重路由。
- 不能声称 measured utilization 会改变已建立 flow。
- 不能用 post-run `link-timeseries.csv` 反向影响同一轮路径。
- 不能把 control-plane estimated load 说成 NS-3 measured utilization。
- 不能把 Patch 4C 的 single later-flow proof 直接扩展为大规模 dynamic-arrival workload 结论。
- 不能在没有脚本验收和 schema 检查前直接进入 paper-candidate 批量实验。

## 4. Baseline 设计

### A. EPS-only baseline

目标：不使用 OCS，所有流走 EPS。

参数原则：

- `enableStaticOcs=false`
- `enableMatrixSelect=false`
- `routeMode=global` 或显式 EPS routing 配置，需在 Patch 5B smoke 中确认路径语义；
- 可分别跑 deterministic EPS 和 WECMP EPS：
  - EPS-only deterministic：`enableEpsWecmp=false`, `enableEpsWecmpRouting=false`
  - EPS-only WECMP：`enableEpsWecmp=true`, `enableEpsWecmpRouting=true`

用途：

- 作为基础连通和纯 EPS 性能下界；
- 观察 EPS utilization、FCT、goodput、fallback/residual 路径。

关键指标：

- `avgFctSeconds`, `p99FctSeconds`, `fctSeconds`
- `aggregateGoodputMbps`, `goodputMbps`
- `maxEpsLinkUtilizationApprox`
- `link-timeseries.csv` EPS utilization
- `pathType`, `frozenSpine`

### B. Static OCS baseline

目标：固定 OCS 光路，不使用 TL-OCS community selection。

参数原则：

- `enableStaticOcs=true`
- `enableMatrixSelect=false`
- 设置固定 `ocsLeafA`, `ocsLeafB`
- `routeMode=ocs-forced`
- 可分 admission on/off：
  - direct OCS：`enableOcsAdmissionControl=false`
  - admission fallback：`enableOcsAdmissionControl=true`, 配置 `ocsAdmissionThreshold`

用途：

- 比较固定光路与 TL-OCS 选择的收益差异；
- 验证 OCS hit、fallback ratio、OCS byte share。

关键指标：

- `ocsHitRatio`
- `ocsByteShare`
- `epsFallbackRatio`
- `pathType`
- selected OCS link `txBytes`
- fallback 场景 selected OCS link `txBytes=0` 的 route correctness。

### C. TL-OCS without measured WECMP

目标：使用 TL-OCS OCS scheduling，但 EPS residual/fallback 不使用 measured utilization。

组合：

1. TL-OCS + deterministic EPS fallback
   - `enableMatrixSelect=true`
   - `communityMode=louvain`
   - `selectionMetric=community-excess`
   - `enableEpsWecmp=false`
   - `enableEpsWecmpRouting=false`

2. TL-OCS + control-plane WECMP
   - `enableMatrixSelect=true`
   - `communityMode=louvain`
   - `selectionMetric=community-excess`
   - `enableEpsWecmp=true`
   - `enableEpsWecmpRouting=true`
   - `epsWecmpLoadSource=control-plane`

用途：

- 作为 TL-OCS 本体和 measured WECMP 的主要对照；
- 验证 OCS scheduling 不被 WECMP measured mode 混淆。

### D. TL-OCS with measured WECMP

目标：TL-OCS + runtime measured snapshot WECMP。

当前 Patch 4C 支持的严格口径：

- measured snapshot 只影响 later new flow；
- 不改变已建立 flow；
- 不做 packet-level rerouting；
- 不做多周期动态重路由。

参数原则：

- `enableMatrixSelect=true`
- `communityMode=louvain`
- `selectionMetric=community-excess`
- `enableEpsWecmp=true`
- `enableEpsWecmpRouting=true`
- `epsWecmpLoadSource=measured-snapshot`
- `measuredWecmpNoSampleFallback=control-plane`
- 对 later-flow proof：`enableMeasuredWecmpLaterFlowProof=true`

用途：

- 在 small-smoke 里证明 measured source 被读取并能改变 later new flow 的 selectedSpine；
- 不能作为全局动态 traffic measured-WECMP 论文主结论。

### E. Ablation baselines

建议先设计，Patch 5B smoke 可以少量覆盖，paper-candidate 前再扩展：

- TL-OCS without Louvain community factor：
  - `communityMode=preview` 或将 `communityAlpha` 设为无跨社区折扣语义的配置，需先确认当前参数含义。
- TL-OCS absolute traffic metric：
  - `selectionMetric=absolute`
- TL-OCS excess metric：
  - `selectionMetric=excess`
- TL-OCS community-excess metric：
  - `selectionMetric=community-excess`
- with / without state holding：
  - `enableStateHolding=true/false`
- with / without config update gate：
  - `enableConfigUpdateGate=true/false`
- with / without hold-time gate：
  - `enableHoldTimeGate=true/false`
- with / without OCS admission：
  - `enableOcsAdmissionControl=true/false`

## 5. 场景族设计

### small-smoke

用途：Patch 5B 脚本验收和 CSV schema 检查，不用于论文主图。

建议规模：

- `numLeaves=4`
- `numSpines=2`
- `serversPerLeaf=2`
- `simTime=1.5` 到 `3.0`

必须覆盖：

- OCS hit；
- admission fallback；
- WECMP binding；
- measured no-sample fallback；
- measured later-flow selectedSpine change。

### medium-sanity

用途：检查趋势和脚本可扩展性，仍不是论文最终实验。

建议规模：

- `numLeaves=8`
- `numSpines=4`
- `serversPerLeaf=2` 或 `4`
- `simTime=3.0` 到 `10.0`

风险：

- CSV 行数明显增加；
- `link-timeseries.csv` 行数约为 sample batches * direction counters；
- 需要 Patch 5B 先实现行数和 schema 自动检查。

### paper-candidate

用途：未来论文候选实验，不在 Patch 5A/5B 直接运行。

建议先规划而不执行：

- leaves：16 或 32；
- spines：4 或 8；
- servers per leaf：4 到 8；
- simTime：需按 workload 到达模型重新评估；
- seeds：至少 5 个随机种子，但当前 synthetic matrix 仍偏 deterministic，随机种子机制需要单独审计。

当前支撑状态：

- 控制面公式链路、路径真实性、CSV 观测链路已具备；
- 但大规模 dynamic arrivals、随机 workload seeds、批量运行恢复、统计聚合和绘图仍未工程化；
- 不建议直接跑 paper-candidate。

### traffic pattern family

当前已有或设计应覆盖：

- `skewed`
- `clustered`
- `uniform`
- `skewed-to-clustered`
- `clustered-to-skewed`
- `alternating`

其中 sequence 类 traffic pattern 更适合搭配 multi-period control / state holding / gate ablation，先在 medium-sanity 验证。

### OCS / EPS stress family

- low OCS admission pressure：
  - 较高 `ocsAdmissionThreshold`，更多 OCS admitted flow。
- high OCS admission pressure：
  - 较低 `ocsAdmissionThreshold`，触发 fallback。
- EPS hot-spine diagnostic load：
  - `epsWecmpDiagnosticLoadMode=hot-spine`
  - `epsWecmpDiagnosticLoad>0`
  - `epsWecmpDiagnosticHotSpine=<id>`
- measured WECMP later-flow proof：
  - `enableMeasuredWecmpLaterFlowProof=true`
  - decision time 晚于至少一个 measured sample。

## 6. 参数模板设计

### common small-smoke template

固定建议：

- `--numLeaves=4`
- `--numSpines=2`
- `--serversPerLeaf=2`
- `--simTime=1.5`
- `--enableEcho=false`
- `--enableBulk=false`
- `--enableSecondBulk=false`
- `--enableResidualBulk=false`
- `--enableMatrixFlows=true`
- `--trafficMatrixMode=skewed`
- `--communityMode=louvain`
- `--louvainMode=single-level`
- `--eta=1.0`
- `--communityAlpha=0.5`
- `--ocsPortK=1`
- `--maxSelectedOcsLinks=1`
- `--routeMode=ocs-forced`
- `--enableStructuredResultExport=true`
- `--linkUtilizationSampleInterval=0.1`

每个 baseline 只覆盖必要差异参数。

### baseline-specific overrides

| baseline | 参数差异 |
|---|---|
| EPS-only deterministic | `enableStaticOcs=false`, `enableMatrixSelect=false`, `enableEpsWecmp=false`, `enableEpsWecmpRouting=false` |
| Static OCS direct | `enableStaticOcs=true`, `enableMatrixSelect=false`, `ocsLeafA=0`, `ocsLeafB=3`, `enableOcsAdmissionControl=false` |
| Static OCS admission fallback | Static OCS direct + `enableOcsAdmissionControl=true`, `ocsAdmissionThreshold=20`, `matrixFlowDemand=40` |
| TL-OCS deterministic EPS | `enableStaticOcs=true`, `enableMatrixSelect=true`, `selectionMetric=community-excess`, `enableEpsWecmp=false` |
| TL-OCS control-plane WECMP | TL-OCS deterministic + `enableEpsWecmp=true`, `enableEpsWecmpRouting=true`, `epsWecmpLoadSource=control-plane` |
| TL-OCS measured no-sample | TL-OCS WECMP + `epsWecmpLoadSource=measured-snapshot`, `measuredWecmpNoSampleFallback=control-plane` |
| TL-OCS measured later proof | TL-OCS measured + `enableMeasuredWecmpLaterFlowProof=true`, `measuredWecmpLaterDecisionTime=0.45`, `measuredWecmpLaterFlowStart=0.55` |

## 7. CSV 指标映射

| 论文图表指标 | CSV 字段 | 文件 |
|---|---|---|
| Mean FCT | `avgFctSeconds` | `summary.csv` |
| P99 FCT | `p99FctSeconds` | `summary.csv` |
| Per-flow FCT distribution | `fctSeconds`, `completed`, `pathType` | `flows.csv` |
| Aggregate goodput | `aggregateGoodputMbps`, `totalRxBytes` | `summary.csv` |
| Per-flow goodput | `goodputMbps`, `rxBytes`, `rxDurationSeconds` | `flows.csv` |
| OCS byte share | `ocsByteShare`, `ocsTxBytes`, `epsTxBytes` | `summary.csv` |
| OCS hit ratio | `ocsHitRatio`, `pathType` | `summary.csv`, `flows.csv` |
| OCS observed use | `ocsObservedUse`, `ocsTxBytes` | `summary.csv` |
| Selected OCS link use | `linkType=ocs`, `linkId`, `txBytes`, `utilizationApprox` | `links.csv` |
| EPS fallback ratio | `epsFallbackRatio`, `fallbackFlowCount` | `summary.csv` |
| EPS residual ratio | `residualFlowRatio`, `epsResidualFlowCount` | `summary.csv` |
| Flow path attribution | `pathType`, `fallbackToEps`, `requiresEpsResidualPath`, `frozenSpine` | `flows.csv` |
| Aggregate per-link utilization | `utilizationApprox`, `txBytes`, `capacityGbps` | `links.csv` |
| Time-series link utilization | `sampleTimeSeconds`, `deltaTxBytes`, `sampleThroughputMbps`, `utilizationApprox` | `link-timeseries.csv` |
| EPS max utilization | `maxEpsLinkUtilizationApprox`, `linkTimeseriesMaxEpsUtilizationApprox` | `summary.csv` |
| OCS max utilization | `maxOcsLinkUtilizationApprox`, `linkTimeseriesMaxOcsUtilizationApprox` | `summary.csv` |
| WECMP selected spine | `selectedSpine`, `spineIndex`, `targetProbability`, `updatedProbability` | `wecmp.csv` |
| WECMP load source | `loadSource`, `controlPlanePathLoadMetric`, `effectivePathLoadMetric` | `wecmp.csv` |
| Measured WECMP usage | `measuredDecisionUsed`, `measuredDecisionFallback`, `measuredNoSample`, `hasMeasuredSample` | `wecmp.csv` |
| Measured selectedSpine change | `controlPlaneSelectedSpine`, `measuredWecmpChangedSelectedSpineCount` | `wecmp.csv`, `summary.csv` |
| Later-flow proof | `measuredWecmpLaterProofPassed`, `measuredWecmpLaterChangedSelectedSpineCount`, `isMeasuredLaterProofFlow` | `summary.csv`, `flows.csv` |
| Measured EPS snapshot | `leaf`, `spine`, `direction`, `measuredUtilization`, `hasSample` | `measured-wecmp.csv` |
| OCS candidate ranking | `selectionScore`, `modularityGain`, `communityFactor`, `selected`, `rejectReason` | `ocs-candidates.csv` |
| Route correctness | selected OCS `txBytes`, EPS spine direction `txBytes`, `overallResultConsistency`, `overallAlgorithmInvariant` | `links.csv`, `summary.csv` |

## 8. 输出目录规范

Patch 5B 脚本默认不能写 `results/raw`。

建议目录：

- smoke 临时输出：
  - `/tmp/tl-ocs-experiments/<run-id>/`
- 可保留实验输出：
  - `results/experiments/<date-or-run-id>/`

每个 run-id 下建议结构：

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

如果当前 binary 只接受一个 `structuredResultDir`，Patch 5B 可以把 CSV 和 logs 放同一目录，先不强制拆 `csv/` 子目录；README 中必须说明。

## 9. experimentName 命名规范

建议格式：

`<suite>__<scale>__<traffic>__<baseline>__<admission>__<wecmp>__seed<seed>`

示例：

- `smoke__4l2s2h__skewed__eps_only__no_ocs__det__seed0`
- `smoke__4l2s2h__skewed__static_ocs03__admit_off__det__seed0`
- `smoke__4l2s2h__skewed__tl_ocs__admit_on__cp_wecmp__seed0`
- `smoke__4l2s2h__skewed__tl_ocs__admit_on__measured_later__seed0`

命名要求：

- 使用小写、数字、下划线；
- 不使用空格；
- baseline、traffic、scale、seed 必须可从名称恢复；
- measured later proof 必须在名称中显式包含 `measured_later`，避免误读为全局 measured WECMP。

## 10. Patch 5B 脚本建议

建议后续新增目录：

`scripts/tl_ocs_experiments/`

建议文件：

| 文件 | 职责 |
|---|---|
| `README.md` | 说明运行边界、输出目录、baseline、smoke 验收。 |
| `run_smoke_matrix.sh` | 运行 5 到 8 个 small-smoke 场景，默认写 `/tmp`。 |
| `run_baseline_sanity.sh` | 运行 medium-sanity 子集，默认仍需用户显式传 `RUN_ID` 和输出目录。 |
| `collect_summary.py` | 汇总多个 `*-summary.csv` 到一个 table，不画图。 |
| `validate_outputs.py` | 检查 CSV 存在性、字段、行数、result/invariant、route correctness。 |

Patch 5B 不建议直接新增画图脚本。绘图应等 baseline 输出稳定后进入后续 Patch。

### run_smoke_matrix.sh 设计

职责：

- 创建 run directory；
- 逐场景运行 ns-3；
- stdout/stderr 写 `logs/<experimentName>.log`；
- structured result 写 run directory；
- 每个命令记录到 `manifest.csv`；
- 所有场景完成后调用 `validate_outputs.py`。

默认 smoke 场景建议 6 个：

1. EPS-only deterministic；
2. Static OCS direct hit；
3. Static OCS admission fallback；
4. TL-OCS deterministic EPS fallback；
5. TL-OCS control-plane WECMP；
6. TL-OCS measured later proof。

可选第 7 到 8 个：

- TL-OCS measured no-sample fallback；
- TL-OCS absolute / excess ablation。

### validate_outputs.py 设计

必须检查：

- 每个 structured-export-enabled 实验存在 `summary`, `flows`, `links`, `link-timeseries`；
- WECMP 实验存在 `wecmp`；
- measured WECMP 实验存在 `measured-wecmp`；
- `summary.csv` 包含 `overallResultConsistency` 和 `overallAlgorithmInvariant`；
- 两者均为 `pass`；
- `flows.csv` 有 `pathType`, `fctSeconds`, `rxDurationSeconds`；
- `links.csv` 行数符合 scale 预期；
- `link-timeseries.csv` sample rows > 0，除非显式 disabled；
- fallback route correctness：fallback/later EPS flow 对 selected OCS link txBytes 为 0；
- Patch 4C proof 场景：`measuredWecmpLaterProofPassed=true`。

## 11. smoke test 验收策略

Patch 5B 前的验收策略：

- smoke test 不超过 5 到 8 个小场景；
- 每个场景 `simTime=1.5` 到 `3.0`；
- 输出默认只写 `/tmp`；
- 不统计论文结论；
- 不画最终论文图；
- 不使用 paper-candidate 规模；
- 每个场景必须检查 CSV schema；
- 每个场景必须检查 `overallResultConsistency=pass`；
- 每个场景必须检查 `overallAlgorithmInvariant=pass`；
- route-fix 场景必须检查 selected OCS link no leakage；
- Patch 3 回归必须检查 `link-timeseries.csv`；
- Patch 4C 回归必须检查 measured later proof 场景。

## 12. paper-candidate 风险

直接进入大规模论文实验的风险：

- 输出量风险：`link-timeseries.csv` 行数随 direction counters 和 sample batches 线性增长；
- 时间风险：NS-3 TCP BulkSend 大规模 flow 可能显著增加运行时间；
- schema 风险：没有自动 validation 前，批量结果可能不可用；
- workload 风险：当前 traffic matrix 仍以 synthetic preset 为主，随机 seed 和动态到达模型不足；
- measured WECMP 风险：当前只证明 later new flow，不支持全局在线动态重路由；
- route correctness 风险：规模扩大后仍需自动检查 OCS leakage 和 EPS direction bytes；
- 统计风险：需要 seed、置信区间和失败重试策略，目前尚未设计完成。

## 13. 现在可以跑与不能跑

现在可以在 Patch 5B 脚本中跑：

- small-smoke 的 OCS hit / fallback / WECMP binding；
- Patch 3 link-timeseries schema smoke；
- Patch 4B measured no-sample fallback smoke；
- Patch 4C later-flow selectedSpine change smoke；
- 少量 TL-OCS selection metric ablation smoke。

现在仍不能跑或不应解释为论文结论：

- paper-candidate 大规模 sweep；
- measured WECMP 全局动态重路由实验；
- 多周期 online rerouting 实验；
- 大量随机 seed 批量实验；
- 最终论文图；
- 任何未通过 automated validation 的批量结果。

## 14. 结论

可以进入 Patch 5B。

Patch 5B 应实现一个小而可验收的实验脚本框架：

- `scripts/tl_ocs_experiments/README.md`
- `scripts/tl_ocs_experiments/run_smoke_matrix.sh`
- `scripts/tl_ocs_experiments/validate_outputs.py`
- 可选 `scripts/tl_ocs_experiments/collect_summary.py`

Patch 5B 不应直接做 paper-candidate sweep，不应画最终论文图，不应新增复杂 runner，不应改 TL-OCS / WECMP / route fix 源码。

仍禁止直接进入大规模论文实验。先用 Patch 5B 的 smoke matrix 和 validation 脚本把 baseline 参数、CSV schema、路径真实性、Patch 2.6 / Patch 3 / Patch 4C 回归固定下来，再决定是否进入 medium-sanity。

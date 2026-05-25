# TL-OCS 数据面路径真实性验证

## 1. 验证目的

本阶段只验证当前 NS-3 工程中控制面选出的 OCS/EPS 决策是否真实影响数据面路径，不验证论文性能优越性，不做大规模实验，不画图。

本轮重点检查：

- 控制面 selected OCS edge 是否安装为 NS-3 leaf-to-leaf PointToPoint OCS 链路；
- OCS-covered matrix flow 是否被路由到 OCS；
- admission 不满足时，OCS pair available flow 是否回落到 EPS residual path；
- 启用 EPS-WECMP routing 后，residual flow 是否冻结到某个 spine；
- 当前日志和 CSV 是否足够支撑后续论文级 FCT、p99 FCT、吞吐量、链路利用率、OCS 命中率和 fallback 比例统计。

## 2. 文件修改范围

本轮新增文件：

| 文件 | 操作 | 说明 |
|---|---|---|
| `docs/tl_ocs_data_plane_path_validation.md` | 新增 | 第三阶段数据面路径真实性验证文档 |

本轮未修改任何 `src/` 源码、算法逻辑、脚本、配置、README、PROJECT_CONTEXT、V2.md 或仓库内 `results/raw` 历史文件。

运行日志和 CSV 均输出到 `/tmp/tl-ocs-data-plane-path-validation/`。运行时从 `/tmp/tl-ocs-data-plane-path-validation/ns3-cwd` 启动 `/home/dyn/ns-3.47/ns3`，用于避免程序内固定的 NetAnim 相对路径写入仓库 `results/raw`。

## 3. 运行环境与命令

构建命令：

```bash
cd /home/dyn/ns-3.47
./ns3 build
```

构建结果：返回 0，输出为 `ninja: no work to do.`。

临时目录准备命令：

```bash
mkdir -p /tmp/tl-ocs-data-plane-path-validation/ns3-cwd /tmp/tl-ocs-data-plane-path-validation/sim/results/raw
```

场景 A：OCS 命中路径验证：

```bash
cd /tmp/tl-ocs-data-plane-path-validation/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=stage-a-ocs-hit --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=false --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-data-plane-path-validation" 2>&1 | tee /tmp/tl-ocs-data-plane-path-validation/stage-a-ocs-hit.log; exit ${PIPESTATUS[0]}
```

场景 B：OCS admission fallback 验证：

```bash
cd /tmp/tl-ocs-data-plane-path-validation/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=stage-b-admission-fallback --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-data-plane-path-validation" 2>&1 | tee /tmp/tl-ocs-data-plane-path-validation/stage-b-admission-fallback.log; exit ${PIPESTATUS[0]}
```

场景 C：EPS-WECMP route binding 验证：

```bash
cd /tmp/tl-ocs-data-plane-path-validation/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=stage-c-wecmp-binding --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=true --enableEpsWecmpRouting=true --epsWecmpDiagnosticLoadMode=hot-spine --epsWecmpDiagnosticLoad=50 --epsWecmpDiagnosticHotSpine=0 --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-data-plane-path-validation" 2>&1 | tee /tmp/tl-ocs-data-plane-path-validation/stage-c-wecmp-binding.log; exit ${PIPESTATUS[0]}
```

输出文件：

| 场景 | 日志 | summary CSV | flows CSV | wecmp CSV | ocs-candidates CSV |
|---|---|---|---|---|---|
| A | `/tmp/tl-ocs-data-plane-path-validation/stage-a-ocs-hit.log` | `stage-a-ocs-hit-summary.csv` | `stage-a-ocs-hit-flows.csv` | `stage-a-ocs-hit-wecmp.csv` | `stage-a-ocs-hit-ocs-candidates.csv` |
| B | `/tmp/tl-ocs-data-plane-path-validation/stage-b-admission-fallback.log` | `stage-b-admission-fallback-summary.csv` | `stage-b-admission-fallback-flows.csv` | `stage-b-admission-fallback-wecmp.csv` | `stage-b-admission-fallback-ocs-candidates.csv` |
| C | `/tmp/tl-ocs-data-plane-path-validation/stage-c-wecmp-binding.log` | `stage-c-wecmp-binding-summary.csv` | `stage-c-wecmp-binding-flows.csv` | `stage-c-wecmp-binding-wecmp.csv` | `stage-c-wecmp-binding-ocs-candidates.csv` |

三个场景均返回 0。

## 4. 场景 A：OCS 命中路径验证

目标：验证 selected OCS edge `0-3` 对应的 matrix flow 真实使用 OCS 路径。

关键日志摘录：

| 检查项 | 当前输出 |
|---|---|
| selected OCS edge | `[HYBRID-DCN][RESULT] finalEdge[0] = 0-3 score=57.6471 utility=57.6471 stateHoldingGain=0 installed=true` |
| installed OCS link | `[HYBRID-DCN][OCS-LINK] installedOcsLinks = 1`，`link[0] = 0-3 addrA=10.0.0.65 addrB=10.0.0.66 utility=57.6471` |
| route mode | `[HYBRID-DCN][ROUTE] routeMode = ocs-forced`，`ocsForced = true` |
| OCS host route | `ocsPairHostRoutes = 16`，`ocsPairHostRoutesSkippedForEpsResidual = 0` |
| admission | `enabled = false`，`directOcsFlows = 1`，`fallbackFlows = 0` |
| OCS-covered flow | `matrixFlow[0] pair = 0-3`，`ocsCovered = true`，`requiresEpsResidualPath = false` |
| EPS residual flow | `matrixFlow[1] pair = 0-1` 和 `matrixFlow[2] pair = 0-2`，均 `requiresEpsResidualPath = true` |
| byte counters | `ocsTxBytes = 603892`，`epsTxBytes = 2415568` |
| result consistency | `dataPlaneValidationPass = true`，`overallResultConsistency = pass`，`overallAlgorithmInvariant = pass` |

`summary` CSV 摘要：

| selectedOcsEdges | ocsCoveredFlowCount | epsResidualFlowCount | fallbackFlowCount | wecmpFrozenFlowCount | ocsTxBytes | epsTxBytes | completionRatio |
|---|---:|---:|---:|---:|---:|---:|---:|
| `0-3` | 1 | 2 | 0 | 0 | 603892 | 2415568 | 1 |

`flows` CSV 关键行：

| flowIndex | srcLeaf | dstLeaf | ocsPairInstalled | ocsAdmitted | ocsCovered | fallbackToEps | requiresEpsResidualPath | epsPathFrozen | frozenSpine | rxBytes | goodputMbps |
|---:|---:|---:|---|---|---|---|---|---|---:|---:|---:|
| 0 | 0 | 3 | true | true | true | false | false | false | -1 | 524288 | 9796.02 |
| 1 | 0 | 1 | false | false | false | false | true | false | -1 | 524288 | 11150.1 |
| 2 | 0 | 2 | false | false | false | false | true | false | -1 | 524288 | 11150.1 |

`ocs-candidates` CSV 摘要：

| leafA | leafB | traffic | expected | modularityGain | communityFactor | selectionScore | selected | rejectReason |
|---:|---:|---:|---:|---:|---:|---:|---|---|
| 0 | 3 | 100 | 42.3529 | 57.6471 | 1 | 57.6471 | true | selected |
| 1 | 2 | 30 | 7.35294 | 22.6471 | 1 | 22.6471 | false | max-selected-links |

结论：场景 A 通过。控制面选择的 `0-3` 被安装为 OCS link，`0-3` matrix flow 在 flows CSV 中标记为 `ocsPairInstalled=true`、`ocsAdmitted=true`、`ocsCovered=true`、`requiresEpsResidualPath=false`，并且全局 OCS MacTx 字节大于 0。当前证据足以做小规模路径真实性 sanity check，但仍不是 per-flow NetDevice 级路径证明。

## 5. 场景 B：OCS admission fallback 验证

目标：验证 OCS 光路存在，但 `ocsAdmissionThreshold=20` 小于 `matrixFlowDemand=40` 时，`0-3` flow 回落 EPS。

关键日志摘录：

| 检查项 | 当前输出 |
|---|---|
| selected/installed OCS link | `0-3`，`installedOcsLinks = 1`，`link[0] = 0-3 ... utility=57.6471` |
| route mode | `routeMode = ocs-forced`，`ocsForced = true` |
| OCS host route | `ocsPairHostRoutes = 12`，`ocsPairHostRoutesSkippedForEpsResidual = 4` |
| admission threshold | `threshold = 20` |
| matrix demand | `matrixFlowDemand = 40` |
| OCS pair available | `ocsPairAvailableFlows = 1` |
| admitted flows | `admissionControlledAdmittedFlows = 0`，`admittedFlows = 0` |
| fallback flows | `fallbackFlows = 1`，`residualFlows = 3` |
| fallback data plane | `fallbackDataPlaneMode = eps-residual-path` |
| fallback flow | `matrixFlow[0] pair = 0-3`，`ocsPairAvailable = true`，`ocsAdmitted = false`，`epsFallback = true` |
| flow path | `matrixFlow[0] ocsCovered = false`，`requiresEpsResidualPath = true`，`residualPathReason = admission-fallback` |
| byte counters | `ocsTxBytes = 603892`，`epsTxBytes = 2415568` |
| result consistency | `dataPlaneValidationPass = true`，`overallResultConsistency = pass`，`overallAlgorithmInvariant = pass` |

`summary` CSV 摘要：

| selectedOcsEdges | ocsCoveredFlowCount | epsResidualFlowCount | fallbackFlowCount | wecmpFrozenFlowCount | ocsTxBytes | epsTxBytes | completionRatio |
|---|---:|---:|---:|---:|---:|---:|---:|
| `0-3` | 0 | 3 | 1 | 0 | 603892 | 2415568 | 1 |

`flows` CSV 关键行：

| flowIndex | srcLeaf | dstLeaf | ocsPairInstalled | admissionMode | ocsAdmitted | ocsCovered | fallbackToEps | fallbackDataPlaneMode | requiresEpsResidualPath | residualPathReason | rxBytes |
|---:|---:|---:|---|---|---|---|---|---|---|---|---:|
| 0 | 0 | 3 | true | controlled | false | false | true | admission-direct-eps-fallback | true | admission-fallback | 524288 |
| 1 | 0 | 1 | false | controlled | false | false | false | eps-residual-path | true | ordinary-residual | 524288 |
| 2 | 0 | 2 | false | controlled | false | false | false | eps-residual-path | true | ordinary-residual | 524288 |

结论：场景 B 通过，但有一个重要限制。日志和 CSV 明确显示 `0-3` OCS pair available 但未 admitted，且 `requiresEpsResidualPath=true`、`residualPathReason=admission-fallback`，因此控制决策和路由安装层面已经回落 EPS。与此同时，聚合 `ocsTxBytes` 仍为 603892，说明当前全局 OCS MacTx 字节不能单独用于证明某个 fallback flow 没有经过 OCS；严格 per-flow 路径归因还需要更细粒度 trace。

## 6. 场景 C：EPS-WECMP route binding 验证

目标：验证 residual flow 在启用 EPS-WECMP routing 后是否绑定到某个 spine。

关键日志摘录：

| 检查项 | 当前输出 |
|---|---|
| selected/installed OCS link | `0-3`，`installedOcsLinks = 1` |
| admission | `threshold = 20`，`matrixFlowDemand = 40`，`fallbackFlows = 1`，`residualFlows = 3` |
| WECMP enabled | `[HYBRID-DCN][WECMP] enabled = true` |
| WECMP source | `source = control-plane-estimated-residual-load` |
| NS-3 measured utilization | `ns3MeasuredUtilization = false` |
| observedTraffic semantic | `residual-demand-weighted-by-current-wecmp-probability` |
| diagnostic load | `diagnosticLoadMode = hot-spine`，`diagnosticLoad = 50`，`diagnosticHotSpine = 0`，`diagnosticTotalInjected = 200` |
| WECMP decisions | `residualDecisions = 3` |
| route bindings | `[HYBRID-DCN][WECMP-ROUTE] enabled = true`，`bindings = 3` |
| selected spine | binding 0/1/2 均 `selectedSpine = 1`，`installed = true`，`pathFrozen = true` |
| flow path frozen | `epsFrozenFlows = 3`，`epsUnfrozenResidualFlows = 0` |
| OCS host route skipped | `ocsPairHostRoutes = 12`，`ocsPairHostRoutesSkippedForEpsResidual = 4` |

`flows` CSV 关键行：

| flowIndex | srcLeaf | dstLeaf | ocsPairInstalled | ocsAdmitted | ocsCovered | fallbackToEps | requiresEpsResidualPath | residualPathReason | epsPathFrozen | frozenSpine | rxBytes |
|---:|---:|---:|---|---|---|---|---|---|---|---:|---:|
| 0 | 0 | 3 | true | false | false | true | true | admission-fallback | true | 1 | 524288 |
| 1 | 0 | 1 | false | false | false | false | true | ordinary-residual | true | 1 | 524288 |
| 2 | 0 | 2 | false | false | false | false | true | ordinary-residual | true | 1 | 524288 |

`wecmp` CSV 摘要：

| decisionIndex | pair | residualDemand | selectedSpine | spineIndex | pathLoadMetric | candidatePathLoad | updatedProbability |
|---:|---|---:|---:|---:|---:|---:|---:|
| 0 | 0-3 | 80 | 1 | 0 | 0.39 | 130 | 0.302845 |
| 0 | 0-3 | 80 | 1 | 1 | 0.24 | 80 | 0.697155 |
| 1 | 0-1 | 40 | 1 | 0 | 0.39 | 130 | 0.302845 |
| 1 | 0-1 | 40 | 1 | 1 | 0.24 | 80 | 0.697155 |
| 2 | 0-2 | 40 | 1 | 0 | 0.39 | 130 | 0.302845 |
| 2 | 0-2 | 40 | 1 | 1 | 0.24 | 80 | 0.697155 |

结论：场景 C 通过。WECMP 产生 3 个 residual decisions，并安装 3 个 route bindings；flows CSV 中所有 residual/fallback flow 均 `epsPathFrozen=true`、`frozenSpine=1`。这说明当前代码的 WECMP route binding 会影响 residual flow 的 spine 选择。但是日志同时明确写出 `source=control-plane-estimated-residual-load` 和 `ns3MeasuredUtilization=false`，因此不能声称 WECMP 基于真实 NS-3 链路利用率。

## 7. 当前数据面真实性结论

| 问题 | 结论 | 依据 |
|---|---|---|
| selected OCS edge 是否真的安装为 NS-3 链路 | 是 | 三个场景均输出 `installedOcsLinks = 1`，`link[0] = 0-3 addrA=10.0.0.65 addrB=10.0.0.66` |
| OCS-covered flow 是否真的被路由到 OCS | 小规模验证通过 | 场景 A 中 `0-3` flow 为 `ocsCovered=true`、`requiresEpsResidualPath=false`，OCS host route 数为 16，`ocsTxBytes > 0` |
| fallback/residual flow 是否真的走 EPS | 小规模验证基本通过 | 场景 B/C 中 fallback flow 为 `requiresEpsResidualPath=true`，OCS host route skip 数为 4，EPS Tx 字节大于 0 |
| WECMP 是否真的影响 residual flow 的 spine 选择 | 是，在 route binding 层面成立 | 场景 C 中 `bindings = 3`，三个 flow 均 `epsPathFrozen=true`、`frozenSpine=1` |
| 当前 WECMP 是否基于真实 NS-3 per-link utilization | 否 | 日志明确输出 `source=control-plane-estimated-residual-load`、`ns3MeasuredUtilization=false` |
| 当前输出是否足够支持论文级 FCT/p99/utilization 图 | 不足 | 目前适合小规模 sanity check；缺少 per-flow/per-link 数据面归因、per-link utilization time series、直接 FCT/p99 汇总字段 |

需要特别注意：当前 `flows` CSV 没有名为 `fct` 的列，只有 `firstRx`、`lastRx`、`goodputMbps`；日志中每个 matrix flow 有 `duration` 字段。后续论文级 FCT 统计应固化为 CSV schema，而不是只依赖日志文本。

## 8. 缺失字段与后续最小补丁建议

| 缺失字段 | 影响 | 建议最小补丁 | 是否必须在论文实验前完成 |
|---|---|---|---|
| per-flow actual NetDevice path / hop sequence | 不能严格证明单个 flow 的每个 packet 经过 OCS 还是 EPS | 给 matrix flow 增加 path attribution trace，记录 flowIndex、srcLeaf、dstLeaf、路径类型、出入口 NetDevice 或 hop sequence | 是 |
| per-link OCS/EPS TxBytes | 当前只有聚合 OCS/EPS 字节，无法解释场景 B 中 fallback 后仍有 OCS 聚合字节的来源 | 为每条 OCS link、每条 leaf-spine EPS link 导出 Tx/Rx byte counters | 是 |
| per-link utilization time series | 不能支撑论文链路利用率曲线或 WECMP 真实反馈 | 按固定 interval 采样 NetDevice counters，导出 linkId、time、txBytes、rxBytes、utilization | 是 |
| CSV 中直接 FCT 字段 | 当前 flows CSV 没有 `fct`，需要从 `firstRx/lastRx` 或日志 duration 推导 | 在 flows CSV 增加 `fct` 或 `flowCompletionTime`，并明确单位 | 是 |
| average FCT / p99 FCT summary | 不能直接画论文平均 FCT 和 99% FCT 图 | summary CSV 增加 `avgFct`、`p99Fct`、样本数和 completion 过滤规则 | 是 |
| OCS hit ratio / EPS fallback ratio | 当前可以由 flows CSV 推导，但 summary 不直接给比例 | summary CSV 增加 `ocsHitRatio`、`epsFallbackRatio`、`residualRatio` | 建议完成 |
| reconfiguration count | 单周期路径真实性验证不覆盖动态重构统计 | 多周期结果导出 `reconfigurationCount`、每 epoch selected edges、changed edges | 是，多周期论文实验前完成 |
| WECMP measured utilization input | 当前 WECMP load 是控制面估计，不是 NS-3 实测 | 将 NetDevice sampled utilization 接入 WECMP telemetry，或在论文中明确为控制面估计 | 是，如果论文声称基于真实链路状态 |
| per-flow route binding interface detail | 当前知道 `frozenSpine`，但没有导出具体 IPv4 route/interface index | 在 WECMP route binding CSV 增加 output interface、next hop、installed host route key | 建议完成 |

## 9. 是否可以进入下一阶段

建议可以进入“指标统计与 CSV schema 修补”阶段，但在论文级实验前应先补齐数据面 trace 字段。

本阶段确认：

- `W -> A -> B -> G -> selected edge` 的控制面结果能安装为 OCS link；
- OCS-covered matrix flow 在小规模场景下会被标记并路由到 OCS；
- admission fallback 会把 OCS pair available 但未 admitted 的 flow 标记为 EPS residual path；
- EPS-WECMP route binding 能冻结 residual flow 到指定 spine；
- 当前 WECMP 仍是控制面估计 residual load，不是 NS-3 measured utilization。

下一阶段最小建议是先修补指标与 CSV schema，而不是立即跑论文性能实验。

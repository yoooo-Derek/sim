# TL-OCS 论文指标与 CSV Schema 设计

## 1. 设计目的

本轮只做论文指标与 CSV schema 设计，不修改源码，不运行仿真，不改变 TL-OCS 算法逻辑。

目标是为下一阶段最小源码补丁提供可审查方案，使当前工程逐步支持论文级实验常用指标：

- FCT、average FCT、p99 FCT；
- flow goodput、总吞吐量；
- OCS/EPS 字节占比、OCS hit ratio、EPS fallback ratio、residual ratio；
- per-flow path type、per-flow route spine；
- per-link TxBytes、per-link utilization time series；
- WECMP selected spine、probability、measured utilization；
- multi-period reconfiguration count、changed edge count、selected edge set per epoch。

## 2. 当前 CSV 输出审计

当前 structured export 集中在 `src/main/hybrid-dcn-main.cc` 的 `runStructuredResultExport` lambda，路径构造在约 `6387-6394`，四类 CSV 写出在约 `6434-6696`。

当前已有 CSV：

| CSV | 当前文件名模式 | 写出区域 | 当前用途 |
|---|---|---|---|
| summary | `<experimentName>-summary.csv` | `src/main/hybrid-dcn-main.cc:6434-6504` | 单行实验摘要、控制面和聚合数据面结果 |
| flows | `<experimentName>-flows.csv` | `src/main/hybrid-dcn-main.cc:6510-6592` | matrix flow 级路径标记、收包、完成比例、goodput |
| wecmp | `<experimentName>-wecmp.csv` | `src/main/hybrid-dcn-main.cc:6598-6645` | WECMP decision 中每个 candidate spine 的概率和负载估计 |
| ocs-candidates | `<experimentName>-ocs-candidates.csv` | `src/main/hybrid-dcn-main.cc:6650-6696` | TL-OCS candidate edge 的模块度收益、选择分数和 reject reason |

当前没有导出：

- `links.csv`
- `link_timeseries.csv`
- `epochs.csv`

### 2.1 summary 当前字段

当前字段来自 `/tmp/tl-ocs-data-plane-path-validation/stage-a-ocs-hit-summary.csv` 表头和源码 `6434-6504`：

| 字段 | 来源 | 说明 |
|---|---|---|
| experimentName | 命令行参数 | 实验名 |
| presetScenario | 命令行参数 | preset |
| trafficMatrixMode | 命令行参数 | synthetic matrix mode |
| trafficMatrixSource | 命令行参数 | 当前验证中为 `synthetic` |
| enableEwmaSmoothing | 命令行参数 | 是否启用 EWMA |
| trafficGraphThreshold | 命令行参数 | theta_f |
| configScoreMode | 命令行参数 | 配置目标函数模式 |
| selectionMetric | 命令行参数 | candidate selection metric |
| communityMode | 命令行参数 | community mode |
| activeCommunityCount | `activeCommunityCount` | 社区数量 |
| modularityQ | `louvainResult.modularityQ` | Louvain modularity |
| selectedOcsEdges | `formatEdgeSet(selectedOcsEdges)` | 最终 edge set |
| candidateConfigScore | `candidateConfigScore` | candidate config score |
| previousConfigScore | `previousConfigScore` | previous config score |
| configScoreImprovement | `configScoreImprovement` | 配置提升 |
| ocsCoveredFlowCount | `resultCoveredFlows` | OCS-covered matrix flow 数 |
| epsResidualFlowCount | `resultResidualFlows` | EPS residual matrix flow 数 |
| fallbackFlowCount | `admissionFallbackFlows` | admission fallback flow 数 |
| wecmpFrozenFlowCount | `resultWecmpFrozenFlows` | WECMP frozen flow 数 |
| ocsTxBytes | `g_ocsTxBytes` | 聚合 OCS MacTx bytes |
| epsTxBytes | `g_epsTxBytes` | 聚合 EPS MacTx bytes |
| ocsObservedUse | `resultOcsObservedUse` | OCS 聚合计数是否大于 0 |
| epsObservedUse | `resultEpsObservedUse` | EPS 聚合计数是否大于 0 |
| matrixFlowCount | `matrixFlowSpecs.size()` | matrix flow 数 |
| completedFlows | `resultCompletedFlows` | 已完成 flow 数 |
| completionRatio | `completedFlows / matrixFlowCount` | flow 完成比例 |
| avgGoodputMbps | `resultMatrixAggregateGoodputMbps` | 按 `totalRxBytes / (simTime - matrixFlowStart)` 算的 aggregate goodput |
| overallResultConsistency | `overallResultConsistencyStatus` | 结果一致性检查 |
| overallAlgorithmInvariant | 入参 `overallAlgorithmInvariantStatus` | 算法 invariant 检查 |

主要缺口：

- 没有 `avgFctSeconds`、`p99FctSeconds`；
- 没有 `totalRxBytes` 字段，虽然日志有 `matrixTotalRxBytes`；
- 没有 `ocsByteShare`，只有 `ocsTxBytes/epsTxBytes`；
- 没有 `ocsHitRatio`、`epsFallbackRatio`、`residualFlowRatio`；
- 没有 per-link 统计和 utilization。

### 2.2 flows 当前字段

当前字段来自源码 `6510-6592` 和 CSV 表头：

| 字段 | 来源 | 说明 |
|---|---|---|
| flowIndex | 循环下标 | flow id |
| name | `MatrixBulkFlowSpec::name` | flow 名 |
| srcLeaf / dstLeaf | `MatrixBulkFlowSpec` | ToR pair |
| rawDemand | `rawTrafficMatrix[srcLeaf][dstLeaf]` | 原始 directed W 中对应项 |
| controlDemand | `controlTrafficMatrix[srcLeaf][dstLeaf]` | 控制面矩阵对应项 |
| ocsPairInstalled | `isOcsPairInstalled` | 该 pair 是否有 OCS link |
| admissionMode | `enableOcsAdmissionControl` | `controlled` 或 `disabled-direct-ocs` |
| ocsAdmitted | `MatrixBulkFlowSpec::ocsAdmitted` | 是否 admitted |
| ocsCovered | `MatrixBulkFlowSpec::ocsCovered` | 是否走 OCS-covered path |
| fallbackToEps | `MatrixBulkFlowSpec::epsFallback` | 是否 admission fallback |
| fallbackDataPlaneMode | `MatrixBulkFlowSpec::fallbackDataPlaneMode` | fallback 数据面模式 |
| fallbackEventMapped | `MatrixBulkFlowSpec::fallbackEventMapped` | fallback event 是否映射 |
| fallbackMappingType | `MatrixBulkFlowSpec::fallbackMappingType` | fallback 映射类型 |
| plannedResidualDemand | `MatrixBulkFlowSpec` | 控制面 planned residual |
| realResidualDemand | `MatrixBulkFlowSpec` | 当前 flow 的实际 residual demand |
| wecmpResidualDemand | `MatrixBulkFlowSpec` | WECMP 使用的 residual demand |
| requiresEpsResidualPath | `requiresEpsResidualPath(spec)` | 是否需要 EPS residual path |
| residualPathReason | `MatrixBulkFlowSpec::residualPathReason` | residual 原因 |
| epsPathFrozen | `MatrixBulkFlowSpec::epsPathFrozen` | WECMP 是否冻结路径 |
| frozenSpine | `MatrixBulkFlowSpec::frozenSpine` | WECMP 选中的 spine |
| packetSinkPort | `MatrixBulkFlowSpec::port` | sink port |
| startTime | `MatrixBulkFlowSpec::startTime` | BulkSend start time |
| expectedBytes | `MatrixBulkFlowSpec::expectedBytes` | MaxBytes |
| rxBytes | `MatrixBulkFlowStats::rxBytes` | PacketSink 收到字节 |
| completed | `isMatrixFlowCompleted` | 是否完成 |
| completionRatio | `computeMatrixFlowCompletionRatio` | rx/expected |
| firstRx | `MatrixBulkFlowStats::firstRxTime` | 第一次 sink Rx 时间 |
| lastRx | `MatrixBulkFlowStats::lastRxTime` | 最后一次 sink Rx 时间 |
| goodputMbps | `rxBytes * 8 / (lastRx-firstRx)` | flow goodput |

主要缺口：

- 没有显式 `fctSeconds`；日志中 `RESULT matrixFlow[...] duration=...` 有同义计算，但 CSV 没有；
- 没有统一的 `pathType`，需要从 `ocsCovered/requiresEpsResidualPath/fallbackToEps/epsPathFrozen` 推导；
- 没有实际 NetDevice hop/path attribution；
- `frozenSpine` 有 route spine，但没有 output interface 或 next hop。

相关源码：

- `MatrixBulkFlowSpec`：`src/main/hybrid-dcn-main.cc:50-72`
- `MatrixBulkFlowStats`：`src/main/hybrid-dcn-main.cc:94-100`
- `MatrixBulkSinkRxTrace`：`src/main/hybrid-dcn-main.cc:103-125`
- matrix flow app 安装：约 `3327-3830`
- matrix flow 结果统计：约 `5705-6140`

### 2.3 wecmp 当前字段

当前字段来自源码 `6598-6645`：

| 字段 | 来源 | 说明 |
|---|---|---|
| decisionIndex | `epsWecmpDecisions` 下标 | WECMP decision id |
| srcLeaf / dstLeaf | `EpsWecmpDecision` | residual pair |
| residualDemand | `EpsWecmpDecision::residualDemand` | residual demand |
| selectedSpine | `EpsWecmpDecision::selectedSpine` | 最终选中 spine |
| spineIndex | `EpsWecmpLinkState::spineIndex` | candidate spine |
| pathLoadMetric | `EpsWecmpLinkState::pathLoadMetric` | 当前负载指标 |
| candidatePathLoad | `EpsWecmpLinkState::candidatePathLoad` | candidate path load |
| attractiveness | `EpsWecmpLinkState::attractiveness` | 吸引度 |
| normalizedAttractiveness | `EpsWecmpLinkState::normalizedAttractiveness` | 归一化吸引度 |
| targetProbability | `EpsWecmpLinkState::targetProbability` | 目标概率 |
| previousProbability | `EpsWecmpLinkState::previousProbability` | 起始概率 |
| updatedProbability | `EpsWecmpLinkState::updatedProbability` | 更新后概率 |
| probabilityDelta | `EpsWecmpLinkState::probabilityDelta` | 总变化 |
| boundedProbabilityDelta | `EpsWecmpLinkState::boundedProbabilityDelta` | 单步有界变化 |

注意：`src/eps/eps-wecmp-state.h` 已明确注释 `observedTraffic` 和 `EpsPhysicalLinkState` 当前存的是 control-plane estimated residual load，不是 NS-3 measured per-link bytes。

主要缺口：

- CSV 没有 `observedTraffic`、`utilization`、`smoothedUtilization`，虽然日志有；
- 没有 `loadSource` 或 `ns3MeasuredUtilization` 字段；
- 没有 route binding CSV，仅在日志和 flows CSV 中体现 `frozenSpine`。

### 2.4 ocs-candidates 当前字段

当前字段来自源码 `6650-6696`：

| 字段 | 来源 | 说明 |
|---|---|---|
| candidateIndex | 下标 | candidate id |
| leafA / leafB | `OcsCandidateEdge` | candidate pair |
| traffic | `OcsCandidateEdge::traffic` | A_ij 或控制矩阵 traffic |
| expected | `OcsCandidateEdge::expected` | P_ij |
| modularityGain | `OcsCandidateEdge::modularityGain` | B_ij |
| utility | `OcsCandidateEdge::utility` | G 或 utility |
| communityFactor | `OcsCandidateEdge::communityFactor` | h(c_i,c_j) |
| stateHoldingGain | `OcsCandidateEdge::stateHoldingGain` | lambda 项 |
| selectionScore | `OcsCandidateEdge::selectionScore` | 最终选边 score |
| selected | `isEdgeInSet(replaySelectedEdges, ...)` | 是否被选 |
| rejectReason | `replayRejectReasons` | 拒绝原因 |

主要缺口：

- 缺少 `baseUtility`、`communityUtility`、`intraCommunity`，这些在 `src/ocs/ocs-state.h` 的 `OcsCandidateEdge` 中已有；
- 单周期足够，multi-period 时需要 epoch 维度。

## 3. 论文指标需求与当前支持状态

| 指标 | 论文用途 | 当前是否已有 | 当前字段来源 | 是否需要新增字段 | 是否需要新增 trace | 优先级 |
|---|---|---|---|---|---|---|
| flow completion time，FCT | FCT 分布、平均值、p99 | 部分已有 | 日志 `duration`；CSV 有 `firstRx/lastRx` | 是，flows 增加 `fctSeconds` | 否 | P1 |
| average FCT | 平均 FCT 图 | 可推导但未导出 | 由 completed flows 的 `fctSeconds` 计算 | 是，summary 增加 `avgFctSeconds` | 否 | P1 |
| p99 FCT | tail latency 图 | 可推导但未导出 | 由 completed flows 的 `fctSeconds` 计算 | 是，summary 增加 `p99FctSeconds` | 否 | P1 |
| flow goodput | 单 flow 吞吐 | 已有 | flows `goodputMbps` | 否 | 否 | P1 |
| 总吞吐量 | aggregate throughput | 部分已有 | summary `avgGoodputMbps`，日志 `matrixAggregateGoodputMbps` | 建议重命名/新增 `aggregateGoodputMbps` | 否 | P1 |
| OCS TxBytes | OCS 使用量 | 已有，聚合 | summary `ocsTxBytes`，全局 `g_ocsTxBytes` | 否 | 否 | P1 |
| EPS TxBytes | EPS 使用量 | 已有，聚合 | summary `epsTxBytes`，全局 `g_epsTxBytes` | 否 | 否 | P1 |
| OCS 承载字节占比 | OCS offload/share 图 | 可推导但未导出 | `ocsTxBytes/(ocsTxBytes+epsTxBytes)` | 是，summary 增加 `ocsByteShare` | 否 | P1 |
| OCS hit ratio | OCS 命中率 | 可推导但未导出 | `ocsCoveredFlowCount/matrixFlowCount` | 是，summary 增加 `ocsHitRatio` | 否 | P1 |
| EPS fallback ratio | fallback 比例 | 可推导但未导出 | `fallbackFlowCount/matrixFlowCount` | 是，summary 增加 `epsFallbackRatio` | 否 | P1 |
| residual flow ratio | EPS residual 比例 | 可推导但未导出 | `epsResidualFlowCount/matrixFlowCount` | 是，summary 增加 `residualFlowRatio` | 否 | P1 |
| per-flow path type | 路径分类 | 可推导但未导出 | flows 中 `ocsCovered/requiresEpsResidualPath/fallbackToEps` | 是，flows 增加 `pathType` | 否 | P1 |
| per-flow route spine | WECMP 路由解释 | 已有 | flows `frozenSpine` | 否，可保留 | 否 | P1 |
| per-link OCS TxBytes | OCS link 利用率 | 未实现 | 当前只有聚合 `g_ocsTxBytes` | 是，links CSV | 是，per-link MacTx callback | P2 |
| per-link EPS TxBytes | EPS link 利用率 | 未实现 | 当前只有聚合 `g_epsTxBytes` | 是，links CSV | 是，per-link MacTx callback | P2 |
| per-link utilization time series | 链路利用率曲线 | 未实现 | 无 | 是，link_timeseries CSV | 是，定时采样 per-link counter | P3 |
| max link utilization | 链路瓶颈 | 未实现 | 需要 links/time series | 是，summary 或后处理 | 是 | P3 |
| average link utilization | 链路利用率均值 | 未实现 | 需要 links/time series | 是，summary 或后处理 | 是 | P3 |
| link utilization standard deviation | 负载均衡程度 | 未实现 | 需要 links/time series | 是，summary 或后处理 | 是 | P3 |
| WECMP selected spine | WECMP 决策 | 已有 | wecmp `selectedSpine`，flows `frozenSpine` | 否 | 否 | P1 |
| WECMP updated probability | WECMP 权重变化 | 已有 | wecmp `updatedProbability` | 否 | 否 | P1 |
| WECMP measured utilization | 真实链路状态反馈 | 未实现 | 当前是 estimated residual load | 是，wecmp 增加 measured 字段 | 是，接入 sampled EPS utilization | P4 |
| reconfiguration count | OCS 重构开销 | 日志部分有 changed edges | `ControlEpochSummary` 有 candidate/previous changed edges | 是，epochs CSV 和 summary 总数 | 否 | P5 |
| changed edge count per epoch | 每周期重构 | 日志有，CSV 无 | `ControlEpochSummary::candidateChangedEdges` 等 | 是，epochs CSV | 否 | P5 |
| selected OCS edge set per epoch | 多周期配置序列 | 日志有数量，edge set 需要导出 | `OcsControllerDecision` 保留 selected edges；summary 中只有数量 | 是，epochs CSV | 否 | P5 |

## 4. 建议 CSV Schema

设计原则：

- Patch 1 只增加可由现有 `MatrixBulkFlowSpec/Stats` 和聚合 counter 推导的字段；
- per-link 和 measured utilization 后置，避免先改 WECMP 语义；
- 兼容当前 CSV：优先追加字段，不删除或改名已有字段；
- 新 CSV 先独立导出，不改变现有四个 CSV 的读法。

### 4.1 summary.csv

| CSV 文件 | 字段名 | 类型 | 单位 | 含义 | 当前是否已有 | 来源或新增方式 |
|---|---|---|---|---|---|---|
| summary.csv | experimentName | string | - | 实验名 | 是 | 现有 |
| summary.csv | presetScenario | string | - | preset | 是 | 现有 |
| summary.csv | trafficMatrixMode | string | - | 流量矩阵模式 | 是 | 现有 |
| summary.csv | selectedOcsEdges | string | - | 最终 OCS edge set | 是 | 现有 |
| summary.csv | matrixFlowCount | uint32 | flows | matrix flow 总数 | 是 | 现有 |
| summary.csv | completedFlowCount | uint32 | flows | 完成 flow 数 | 部分，现名 `completedFlows` | Patch 1 追加，值同 `resultCompletedFlows` |
| summary.csv | completionRatio | double | ratio | 完成比例 | 是 | 现有 |
| summary.csv | totalRxBytes | uint64 | bytes | matrix flows 总 Rx bytes | 日志有，CSV 无 | Patch 1，`resultTotalMatrixRxBytes` |
| summary.csv | aggregateGoodputMbps | double | Mbps | 总 goodput | 部分，现名 `avgGoodputMbps` | Patch 1 追加兼容字段 |
| summary.csv | avgFctSeconds | double | s | completed flow 平均 FCT | 否 | Patch 1，由 per-flow fct 计算 |
| summary.csv | p99FctSeconds | double | s | completed flow p99 FCT | 否 | Patch 1，排序计算 |
| summary.csv | ocsTxBytes | uint64 | bytes | 聚合 OCS Tx bytes | 是 | 现有 |
| summary.csv | epsTxBytes | uint64 | bytes | 聚合 EPS Tx bytes | 是 | 现有 |
| summary.csv | ocsByteShare | double | ratio | `ocsTxBytes/(ocsTxBytes+epsTxBytes)` | 否 | Patch 1 |
| summary.csv | ocsCoveredFlowCount | uint32 | flows | OCS-covered flow 数 | 是 | 现有 |
| summary.csv | epsResidualFlowCount | uint32 | flows | residual flow 数 | 是 | 现有 |
| summary.csv | fallbackFlowCount | uint32 | flows | fallback flow 数 | 是 | 现有 |
| summary.csv | ocsHitRatio | double | ratio | `ocsCoveredFlowCount/matrixFlowCount` | 否 | Patch 1 |
| summary.csv | epsFallbackRatio | double | ratio | `fallbackFlowCount/matrixFlowCount` | 否 | Patch 1 |
| summary.csv | residualFlowRatio | double | ratio | `epsResidualFlowCount/matrixFlowCount` | 否 | Patch 1 |
| summary.csv | maxLinkUtilization | double | ratio | 链路最大利用率 | 否 | later，Patch 3 后可导出 |
| summary.csv | avgLinkUtilization | double | ratio | 链路平均利用率 | 否 | later，Patch 3 后可导出 |
| summary.csv | linkUtilizationStddev | double | ratio | 链路利用率标准差 | 否 | later，Patch 3 后可导出 |
| summary.csv | reconfigurationCount | uint32 | events | 总重构次数 | 否 | later，Patch 5 |

### 4.2 flows.csv

| CSV 文件 | 字段名 | 类型 | 单位 | 含义 | 当前是否已有 | 来源或新增方式 |
|---|---|---|---|---|---|---|
| flows.csv | flowIndex | uint32 | - | flow id | 是 | 现有 |
| flows.csv | srcLeaf / dstLeaf | uint32 | - | ToR pair | 是 | 现有 |
| flows.csv | rawDemand / controlDemand | double | matrix unit | 流量矩阵需求 | 是 | 现有 |
| flows.csv | ocsPairInstalled | bool | - | pair 是否有 OCS link | 是 | 现有 |
| flows.csv | ocsAdmitted | bool | - | 是否通过 OCS admission | 是 | 现有 |
| flows.csv | ocsCovered | bool | - | 是否 OCS-covered | 是 | 现有 |
| flows.csv | fallbackToEps | bool | - | 是否 fallback | 是 | 现有 |
| flows.csv | requiresEpsResidualPath | bool | - | 是否需要 EPS residual path | 是 | 现有 |
| flows.csv | pathType | string | - | `ocs` / `eps-fallback` / `eps-residual` | 否 | Patch 1，根据现有 bool 派生 |
| flows.csv | epsPathFrozen | bool | - | WECMP 是否冻结路径 | 是 | 现有 |
| flows.csv | frozenSpine | int32 | spine id | 绑定 spine；-1 表示无 | 是 | 现有 |
| flows.csv | expectedBytes | uint64 | bytes | 目标字节 | 是 | 现有 |
| flows.csv | rxBytes | uint64 | bytes | 收到字节 | 是 | 现有 |
| flows.csv | completed | bool | - | 是否完成 | 是 | 现有 |
| flows.csv | firstRx / lastRx | double | s | 首包/末包 Rx 时间 | 是 | 现有 |
| flows.csv | fctSeconds | double | s | `lastRx-firstRx` | 否 | Patch 1 |
| flows.csv | goodputMbps | double | Mbps | flow goodput | 是 | 现有 |
| flows.csv | actualPathDeviceIds | string | - | 实际经过设备 | 否 | later，需 per-flow path trace |

### 4.3 links.csv

`links.csv` 建议从 Patch 2 开始实现。

| CSV 文件 | 字段名 | 类型 | 单位 | 含义 | 当前是否已有 | 来源或新增方式 |
|---|---|---|---|---|---|---|
| links.csv | linkId | string | - | 稳定链路 id，如 `eps-l0-s1-a` | 否 | Patch 2 新增 link counter registry |
| links.csv | linkType | string | - | `ocs` / `eps-leaf-spine` / `server-leaf` | 否 | Patch 2，先只做 OCS 和 leaf-spine EPS |
| links.csv | endpointA / endpointB | string | - | leaf/spine 端点 | 否 | Patch 2 |
| links.csv | direction | string | - | NetDevice 方向 | 否 | Patch 2 |
| links.csv | capacityGbps | double | Gbps | 链路容量 | 否 | Patch 2，从 DataRate 参数记录 |
| links.csv | txPackets | uint64 | packets | MacTx packets | 否 | Patch 2，per-link callback |
| links.csv | txBytes | uint64 | bytes | MacTx bytes | 否 | Patch 2，per-link callback |
| links.csv | utilizationApprox | double | ratio | `txBytes*8/(simTime*capacity)` | 否 | Patch 2，粗粒度近似 |

### 4.4 link_timeseries.csv

`link_timeseries.csv` 建议从 Patch 3 开始实现，先只测量，不接入 WECMP。

| CSV 文件 | 字段名 | 类型 | 单位 | 含义 | 当前是否已有 | 来源或新增方式 |
|---|---|---|---|---|---|---|
| link_timeseries.csv | timeSeconds | double | s | 采样时间 | 否 | Patch 3，`Simulator::Schedule` |
| link_timeseries.csv | linkId | string | - | 链路 id | 否 | Patch 3，复用 Patch 2 registry |
| link_timeseries.csv | linkType | string | - | OCS/EPS | 否 | Patch 3 |
| link_timeseries.csv | intervalTxBytes | uint64 | bytes | 本采样窗口 Tx bytes | 否 | Patch 3 |
| link_timeseries.csv | cumulativeTxBytes | uint64 | bytes | 累计 Tx bytes | 否 | Patch 3 |
| link_timeseries.csv | intervalUtilization | double | ratio | 窗口利用率 | 否 | Patch 3 |
| link_timeseries.csv | capacityGbps | double | Gbps | 链路容量 | 否 | Patch 3 |

### 4.5 wecmp.csv

| CSV 文件 | 字段名 | 类型 | 单位 | 含义 | 当前是否已有 | 来源或新增方式 |
|---|---|---|---|---|---|---|
| wecmp.csv | decisionIndex | uint32 | - | decision id | 是 | 现有 |
| wecmp.csv | srcLeaf / dstLeaf | uint32 | - | residual pair | 是 | 现有 |
| wecmp.csv | residualDemand | double | matrix unit | residual demand | 是 | 现有 |
| wecmp.csv | selectedSpine | uint32 | spine id | 最终 spine | 是 | 现有 |
| wecmp.csv | spineIndex | uint32 | spine id | candidate spine | 是 | 现有 |
| wecmp.csv | pathLoadMetric | double | current semantic | 负载指标 | 是 | 现有 |
| wecmp.csv | candidatePathLoad | double | current semantic | candidate load | 是 | 现有 |
| wecmp.csv | updatedProbability | double | probability | 更新后概率 | 是 | 现有 |
| wecmp.csv | observedTraffic | double | current semantic | 当前估计流量 | 日志有，CSV 无 | Patch 4 前可先追加说明字段，但语义仍是 estimated |
| wecmp.csv | utilization | double | current semantic | 当前 estimated utilization | 日志有，CSV 无 | later |
| wecmp.csv | smoothedUtilization | double | current semantic | 平滑 estimated utilization | 日志有，CSV 无 | later |
| wecmp.csv | loadSource | string | - | `control-plane-estimated-residual-load` / `ns3-measured` | 否 | Patch 4 |
| wecmp.csv | ns3MeasuredUtilization | bool | - | 是否真实测量 | 否 | Patch 4 |

### 4.6 ocs-candidates.csv

| CSV 文件 | 字段名 | 类型 | 单位 | 含义 | 当前是否已有 | 来源或新增方式 |
|---|---|---|---|---|---|---|
| ocs-candidates.csv | candidateIndex | uint32 | - | candidate id | 是 | 现有 |
| ocs-candidates.csv | leafA / leafB | uint32 | - | candidate pair | 是 | 现有 |
| ocs-candidates.csv | traffic | double | matrix unit | A_ij | 是 | 现有 |
| ocs-candidates.csv | expected | double | matrix unit | P_ij | 是 | 现有 |
| ocs-candidates.csv | modularityGain | double | matrix unit | B_ij | 是 | 现有 |
| ocs-candidates.csv | baseUtility | double | score | `[B]^+` 或基础 utility | 否 | later，字段已在 `OcsCandidateEdge` |
| ocs-candidates.csv | communityFactor | double | ratio | h(c_i,c_j) | 是 | 现有 |
| ocs-candidates.csv | communityUtility | double | score | community-adjusted utility | 否 | later，字段已在 `OcsCandidateEdge` |
| ocs-candidates.csv | stateHoldingGain | double | score | lambda 项 | 是 | 现有 |
| ocs-candidates.csv | selectionScore | double | score | 最终 score | 是 | 现有 |
| ocs-candidates.csv | selected | bool | - | 是否被选 | 是 | 现有 |
| ocs-candidates.csv | rejectReason | string | - | 拒绝原因 | 是 | 现有 |

### 4.7 epochs.csv

`epochs.csv` 建议 Patch 5 实现。

| CSV 文件 | 字段名 | 类型 | 单位 | 含义 | 当前是否已有 | 来源或新增方式 |
|---|---|---|---|---|---|---|
| epochs.csv | epoch | uint32 | epoch | 控制周期 | 日志有，CSV 无 | Patch 5，`controlEpochSummaries` |
| epochs.csv | trafficMatrixMode | string | - | 周期矩阵模式 | 日志有 | Patch 5 |
| epochs.csv | selectedOcsEdges | string | - | 周期 selected edge set | 部分，当前 summary 只有数量 | Patch 5，需保存 edge set |
| epochs.csv | selectedOcsEdgeCount | uint32 | edges | selected edge 数 | 日志有 | Patch 5 |
| epochs.csv | candidateChangedEdges | uint32 | edges | candidate vs previous changed edges | 日志有 | Patch 5 |
| epochs.csv | previousChangedEdges | uint32 | edges | previous score changed edges | 日志有 | Patch 5 |
| epochs.csv | reconfigurationCountDelta | uint32 | events | 本周期是否重构/变更数 | 未导出 | Patch 5 |
| epochs.csv | cumulativeReconfigurationCount | uint32 | events | 累计重构数 | 未导出 | Patch 5 |
| epochs.csv | holdTimeActive | bool | - | hold gate 是否生效 | 日志有 | Patch 5 |
| epochs.csv | minSelectedEdgeAge / maxSelectedEdgeAge | uint32 | cycles | edge age | 日志有 | Patch 5 |
| epochs.csv | decision | string | - | install/hold decision | 日志有 | Patch 5 |

## 5. 最小补丁拆分

| Patch | 目标 | 修改文件 | 修改函数/区域 | 新增字段 | 验收命令 | 验收标准 | 风险 |
|---|---|---|---|---|---|---|---|
| Patch 1：FCT 与 summary 派生指标 | 不改变算法，只补论文基本 flow/summary 指标 | `src/main/hybrid-dcn-main.cc` | `isMatrixFlowCompleted` 附近、结果统计 `5705-6140`、structured export `6434-6592` | flows: `pathType`,`fctSeconds`; summary: `completedFlowCount`,`totalRxBytes`,`aggregateGoodputMbps`,`avgFctSeconds`,`p99FctSeconds`,`ocsByteShare`,`ocsHitRatio`,`epsFallbackRatio`,`residualFlowRatio` | 4-ToR 小规模命令，开启 `enableMatrixFlows=true enableStructuredResultExport=true` | CSV 表头包含新字段；现有三个 stage 验证仍返回 0；新字段可由现有日志手算复核 | p99 样本少时语义要注明；只统计 completed flows |
| Patch 2：per-link aggregate counters | 给 OCS 和 leaf-spine EPS 链路导出聚合 TxBytes | `src/main/hybrid-dcn-main.cc` | 全局 counter 结构；EPS link install `3207-3218`；OCS link install `3238-3255`；structured export 新增 `links.csv` | `links.csv` 全字段 | 小规模 A/B/C 验证 | `links.csv` 有每条 OCS 和 EPS leaf-spine 方向记录；聚合 sum 与 `ocsTxBytes/epsTxBytes` 可解释 | 需要替换无上下文 callback 为 bound callback，注意不要破坏现有聚合计数 |
| Patch 3：per-link utilization time series | 定时采样 link counters，先只测量不接 WECMP | `src/main/hybrid-dcn-main.cc` | link counter registry；`Simulator::Run` 前约 `5545` 安排 sampler；structured export 新增 `link_timeseries.csv` | `timeSeconds`,`linkId`,`intervalTxBytes`,`intervalUtilization` 等 | 4-ToR 短仿真，采样间隔如 0.1s | time series 非空；每条 link interval 利用率有限且非负 | 采样窗口和 TCP burst 可能导致短时尖峰，论文解释需固定 interval |
| Patch 4：WECMP measured utilization integration | 将真实 sampled EPS utilization 接入 WECMP，保留 estimated 模式 | `src/main/hybrid-dcn-main.cc`，可能涉及 `src/eps/eps-wecmp-state.h` | WECMP telemetry `1950-2175`；wecmp export `6598-6645` | `loadSource`,`ns3MeasuredUtilization`,`measuredUtilization` | 小规模 C，diagnostic load 与 measured mode 各跑一次 | measured mode 下日志/CSV 明确使用 NS-3 sampled EPS utilization；estimated mode 兼容 | 会改变 WECMP 决策语义，不应先做 |
| Patch 5：multi-period reconfiguration metrics | 导出 epoch 级 OCS 重构指标 | `src/main/hybrid-dcn-main.cc` | `ControlEpochSummary` `126-154`；multi-period summaries `4683-4768`；structured export 新增 `epochs.csv` | `epochs.csv` 全字段，summary `reconfigurationCount` | 小规模 multi-period control 命令 | 每 epoch 有 selected edge set、changed edges、decision、edge age；累计重构数可复核 | 需要保存 edge set 字符串，避免只导出数量 |

## 6. 推荐下一步

推荐下一轮先执行 Patch 1：FCT 与 summary 派生指标。

理由：

- Patch 1 完全基于现有 `MatrixBulkFlowStats` 和聚合 counter，`MatrixBulkSinkRxTrace` 已经记录 `rxBytes/firstRxTime/lastRxTime`；
- 不需要改拓扑、路由、OCS 安装、WECMP 算法，也不需要新增 trace；
- 代码改动集中在结果统计和 structured export 区域，最容易审查和回滚；
- 能立即补齐论文最基础的 FCT、p99 FCT、吞吐、OCS hit ratio、fallback ratio、residual ratio；
- 可以用已有 4-ToR 验证命令快速验收。

不建议现在直接做 WECMP measured utilization integration。

原因：

- 当前 `EpsWecmpLinkState::observedTraffic` 和 `EpsPhysicalLinkState::observedTraffic` 明确是 control-plane estimated residual load；
- measured utilization 需要先有 per-link counter 和 time series，否则 WECMP 没有可靠真实输入；
- 直接接入 measured utilization 会改变 WECMP 决策语义，属于算法/控制闭环行为变化，应该在 Patch 2/3 的测量链路稳定后再做；
- 论文中如果要声称 “基于真实链路状态的 WECMP”，Patch 4 必须完成；但它不是最小无风险第一步。

## 7. 风险与注意事项

1. 当前 `ocsTxBytes` 和 `epsTxBytes` 是 aggregate MacTx counter，不是 per-link、per-flow 归因；不能直接证明某个 flow 的完整路径。
2. 当前 WECMP 的 `observedTraffic/utilization/smoothedUtilization` 是控制面 residual demand 估计，不是 NS-3 measured utilization。
3. 当前 flows CSV 没有 `fctSeconds`，只能由 `lastRx-firstRx` 推导；论文实验前应固化字段和单位。
4. 当前 p99 FCT 对小规模 3-flow sanity check 没有统计意义，但 schema 应先统一，后续大规模实验复用。
5. 当前多周期日志有 epoch summary，但 CSV 没有 epochs schema；重构次数不能只靠日志文本支撑论文图。
6. per-link utilization 必须明确时间窗口、容量和方向；否则不同实验之间不可比。
7. Patch 1 不会解决真实链路利用率问题，它只补基本论文指标字段。
8. Patch 2/3 只做测量，不应顺手改 WECMP 决策；Patch 4 才讨论接入 measured utilization。

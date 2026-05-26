# TL-OCS Patch 3 Link Timeseries 验证

## 1. Patch 3 目标

Patch 3 在 Patch 2 / Patch 2.6 的方向级 `LinkCounter` aggregate counters 基础上，新增 measured Tx counter 的周期性采样，并导出 `<experimentName>-link-timeseries.csv`。

本补丁只导出观测指标，不把 sampled utilization 接入 EPS-WECMP 控制。

## 2. 修改范围

本轮只修改以下文件：

| 文件 | 修改内容 |
|---|---|
| `src/main/hybrid-dcn-main.cc` | 为 `LinkCounter` 增加采样状态；新增 `LinkUtilizationSample`；新增 `linkUtilizationSampleInterval` 参数；用 `Simulator::Schedule` 周期性采样方向级 Tx counter；新增 link time-series CSV 导出；summary CSV 追加 time-series 摘要字段。 |
| `docs/tl_ocs_patch3_link_timeseries_validation.md` | 本验证文档。 |

未修改内容：

- 未修改 TL-OCS 控制算法：`W -> A -> A_bar -> d/M -> P -> B -> Louvain -> G_ij -> OCS edge selection`；
- 未修改 OCS admission 判定；
- 未修改 EPS fallback / residual 判定；
- 未修改 EPS-WECMP selectedSpine / frozenSpine decision；
- 未修改 Patch 2.6 route fix；
- 未修改拓扑规模、链路速率、链路时延；
- 未修改 BulkSend / matrix flow generation；
- 未删除或重命名 `flows.csv`、`summary.csv`、`links.csv` 既有字段；
- 未修改 `results/raw` 历史文件；
- 未把 sampled utilization 接入 WECMP。

## 3. 新增参数

| 参数 | 类型 | 默认值 | 语义 |
|---|---:|---:|---|
| `linkUtilizationSampleInterval` | double | `0.1` | measured Tx utilization 采样间隔，单位秒；`<= 0` 时禁用 time-series 采样。 |

采样公式：

- `sampleThroughputMbps = deltaTxBytes * 8 / intervalSeconds / 1e6`
- `utilizationApprox = sampleThroughputMbps / capacityMbps`

这里的 `utilizationApprox` 是 NS-3 `MacTx` counter 在采样窗口内的近似链路利用率，不是 control-plane estimated load。

## 4. link-timeseries.csv schema

新增文件：`<experimentName>-link-timeseries.csv`

字段：

| 字段 | 语义 |
|---|---|
| `experimentName` | 实验名。 |
| `sampleIndex` | 采样批次编号，同一时刻每条方向级 counter 各一行。 |
| `sampleTimeSeconds` | 当前采样仿真时间。 |
| `intervalSeconds` | 本 counter 距离上次采样的时间窗口。 |
| `linkType` | `eps-leaf-spine` 或 `ocs`。 |
| `linkId` | 与 `links.csv` 对应的方向级 link id。 |
| `direction` | 与 `links.csv` 一致的方向语义。 |
| `srcNode` | Tx 方向源节点标签。 |
| `dstNode` | Tx 方向目的节点标签。 |
| `capacityMbps` | counter 对应链路容量，Mbps。 |
| `deltaTxPackets` | 当前采样窗口内 Tx packets 增量。 |
| `deltaTxBytes` | 当前采样窗口内 Tx bytes 增量。 |
| `cumulativeTxPackets` | 当前累计 Tx packets。 |
| `cumulativeTxBytes` | 当前累计 Tx bytes。 |
| `sampleThroughputMbps` | 当前采样窗口 measured Tx throughput。 |
| `utilizationApprox` | 当前采样窗口 measured Tx utilization approximation。 |

## 5. summary.csv 新增字段

summary CSV 只追加字段：

| 字段 | 语义 |
|---|---|
| `linkUtilizationSampleIntervalSeconds` | 本轮采样间隔。 |
| `linkTimeseriesEnabled` | `linkUtilizationSampleInterval > 0`。 |
| `linkTimeseriesSampleRows` | `link-timeseries.csv` 数据行数，不含 header。 |
| `linkTimeseriesNonzeroSampleRows` | `deltaTxBytes > 0` 的采样行数。 |
| `linkTimeseriesMaxUtilizationApprox` | 所有采样行的最大 measured utilization。 |
| `linkTimeseriesAvgUtilizationApprox` | 所有采样行的平均 measured utilization。 |
| `linkTimeseriesMaxEpsUtilizationApprox` | `linkType=eps-leaf-spine` 采样行最大 measured utilization。 |
| `linkTimeseriesMaxOcsUtilizationApprox` | `linkType=ocs` 采样行最大 measured utilization。 |

## 6. 验证命令

构建：

```bash
cd /home/dyn/ns-3.47
./ns3 build
```

验证目录：

```bash
mkdir -p /tmp/tl-ocs-patch3-link-timeseries/ns3-cwd
```

场景 1：OCS hit 回归：

```bash
cd /tmp/tl-ocs-patch3-link-timeseries/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch3-ocs-hit --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=false --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch3-link-timeseries --linkUtilizationSampleInterval=0.1" > /tmp/tl-ocs-patch3-link-timeseries/patch3-ocs-hit.log 2>&1
```

场景 2：Admission fallback 回归：

```bash
cd /tmp/tl-ocs-patch3-link-timeseries/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch3-admission-fallback --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch3-link-timeseries --linkUtilizationSampleInterval=0.1" > /tmp/tl-ocs-patch3-link-timeseries/patch3-admission-fallback.log 2>&1
```

场景 3：WECMP binding 回归：

```bash
cd /tmp/tl-ocs-patch3-link-timeseries/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch3-wecmp-binding --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=true --enableEpsWecmpRouting=true --epsWecmpDiagnosticLoadMode=hot-spine --epsWecmpDiagnosticLoad=50 --epsWecmpDiagnosticHotSpine=0 --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch3-link-timeseries --linkUtilizationSampleInterval=0.1" > /tmp/tl-ocs-patch3-link-timeseries/patch3-wecmp-binding.log 2>&1
```

构建和三个场景均返回 0。

## 7. link-timeseries.csv 检查

三个场景均生成 `<experimentName>-link-timeseries.csv`，header 为：

```text
experimentName,sampleIndex,sampleTimeSeconds,intervalSeconds,linkType,linkId,direction,srcNode,dstNode,capacityMbps,deltaTxPackets,deltaTxBytes,cumulativeTxPackets,cumulativeTxBytes,sampleThroughputMbps,utilizationApprox
```

| 场景 | 数据行 | nonzero delta rows | sampleTimeSeconds 单调非降 | utilizationApprox 可解析且 >= 0 |
|---|---:|---:|---|---|
| `patch3-ocs-hit` | 252 | 8 | 是 | 是 |
| `patch3-admission-fallback` | 252 | 8 | 是 | 是 |
| `patch3-wecmp-binding` | 252 | 8 | 是 | 是 |

252 行来自 14 个采样时刻乘以 18 个方向级 counters。

## 8. links.csv aggregate counter 回归

三个场景的 `links.csv` 均为 18 个方向级 counters：

- EPS leaf-spine：`4 * 2 * 2 = 16`
- OCS：`1 * 2 = 2`
- 合计：18

关键方向级 txBytes：

| 场景 | OCS 0-3 a-to-b | EPS leaf0->spine0 | EPS spine0->leaf3 | EPS leaf0->spine1 | EPS spine1->leaf3 |
|---|---:|---:|---:|---:|---:|
| `patch3-ocs-hit` | 577320 | 1154640 | 0 | 0 | 0 |
| `patch3-admission-fallback` | 0 | 1731960 | 577320 | 0 | 0 |
| `patch3-wecmp-binding` | 0 | 0 | 0 | 1731960 | 577320 |

`links.csv` 的 `utilizationApprox` 仍是全仿真期间 aggregate txBytes 粗粒度平均利用率；Patch 3 未改变该字段语义。

## 9. Patch 2.6 route fix 回归

| 场景 | flow0 pathType | flow0 frozenSpine | flow0 rxBytes | 路径真实性结论 |
|---|---|---:|---:|---|
| `patch3-ocs-hit` | `ocs` | -1 | 524288 | selected OCS 0-3 a-to-b `txBytes=577320`，OCS admitted flow 仍真实走 OCS。 |
| `patch3-admission-fallback` | `eps-fallback` | -1 | 524288 | selected OCS 0-3 a-to-b `txBytes=0`，`spine0->leaf3 txBytes=577320`，fallback 仍真实走 EPS。 |
| `patch3-wecmp-binding` | `eps-fallback` | 1 | 524288 | selected OCS 0-3 a-to-b `txBytes=0`，`spine1->leaf3 txBytes=577320`，WECMP fallback 仍真实走 frozenSpine。 |

三个场景 summary 均为：

| 场景 | overallResultConsistency | overallAlgorithmInvariant |
|---|---|---|
| `patch3-ocs-hit` | `pass` | `pass` |
| `patch3-admission-fallback` | `pass` | `pass` |
| `patch3-wecmp-binding` | `pass` | `pass` |

## 10. summary.csv time-series 字段检查

| 场景 | enabled | rows | nonzero | max | avg | maxEps | maxOcs |
|---|---|---:|---:|---:|---:|---:|---:|
| `patch3-ocs-hit` | true | 252 | 8 | 0.00230928 | 2.10883e-05 | 0.00230928 | 0.000461856 |
| `patch3-admission-fallback` | true | 252 | 8 | 0.00346392 | 2.87568e-05 | 0.00346392 | 0 |
| `patch3-wecmp-binding` | true | 252 | 8 | 0.00346392 | 2.87568e-05 | 0.00346392 | 0 |

Patch 1 / Patch 2 既有 summary 字段仍保留，包括 `completedFlowCount`、`totalRxBytes`、`aggregateGoodputMbps`、`avgFctSeconds`、`p99FctSeconds`、`ocsByteShare`、`ocsHitRatio`、`epsFallbackRatio`、`residualFlowRatio`、`linkCounterCount`、`ocsLinkCounterCount`、`epsLinkCounterCount`、`maxOcsLinkUtilizationApprox`、`maxEpsLinkUtilizationApprox`、`avgEpsLinkUtilizationApprox`、`epsLinkUtilizationStddevApprox`。

## 11. 结论

Patch 3 通过。

本补丁实现了 per-link measured Tx utilization time series 导出，且保持 Patch 2.6 route fix：

- OCS hit 仍真实走 OCS；
- admission fallback 未重新泄漏到 OCS；
- WECMP fallback 仍真实走 frozenSpine；
- `links.csv` 方向级 aggregate counter 数量保持 18；
- `overallResultConsistency=pass`；
- `overallAlgorithmInvariant=pass`。

可以进入下一阶段的判断，但下一阶段仍应是“是否进入 Patch 4：WECMP measured utilization integration”。Patch 3 不能解释为 Patch 4，也不能声称 WECMP 已基于 measured utilization。

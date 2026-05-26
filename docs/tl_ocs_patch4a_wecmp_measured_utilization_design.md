# TL-OCS Patch 4A WECMP Measured Utilization Integration Design

## 1. 目标和边界

Patch 4A 是设计审计，不是 Patch 4 实现。本任务目标是审计当前 EPS-WECMP 控制面和 Patch 3 measured link utilization 数据通路，判断 measured utilization 应如何安全接入 WECMP，并给出 Patch 4B 可执行的小补丁设计。

本任务未修改源码，未运行仿真实验，只新增本设计文档。

未修改内容：

- 未修改 TL-OCS 控制算法、Louvain、`B_ij`、`G_ij` 或 OCS candidate selection；
- 未修改 OCS admission；
- 未修改 EPS fallback / residual 判定；
- 未修改 EPS-WECMP decision / selectedSpine / frozenSpine 逻辑；
- 未修改 Patch 2.6 route fix；
- 未修改 Patch 3 link-timeseries 导出实现；
- 未修改拓扑、链路速率、链路时延；
- 未修改 BulkSend / matrix flow generation；
- 未删除、重命名或改变已有 CSV 字段；
- 未接入 WECMP measured utilization；
- 未写入 `results/raw`。

## 2. 当前 EPS-WECMP 控制面代码地图

相关结构体在 `src/eps/eps-wecmp-state.h`：

- `EpsPhysicalLinkState`：按 `leafIndex, spineIndex` 记录 EPS 物理链路状态，字段为 `observedTraffic`、`utilization`、`smoothedUtilization`。注释明确说明当前存的是 control-plane estimated residual load，不是 NS-3 measured bytes。
- `EpsWecmpPairState`：按 leaf pair 记录每个 spine 的 probability 和 smoothed utilization。
- `EpsWecmpLinkState`：一次 WECMP decision 中每个 candidate spine 的 load、probability 和 attractiveness。
- `EpsWecmpDecision`：记录 `srcLeaf`、`dstLeaf`、`residualDemand`、`selectedSpine` 和 candidate spine states。

主要实现位置在 `src/main/hybrid-dcn-main.cc`：

| 区域 | 作用 |
|---|---|
| `epsWecmp*` 参数定义和校验 | 启用 WECMP、rho/gamma/kappa/maxDelta/capacity/pathMetric、diagnostic load、route binding。 |
| `getOrCreateEpsWecmpPairState` | 为 leaf pair 初始化 per-spine probability 和 smoothed utilization。 |
| `resetEpsPhysicalObservedTraffic` | 清空 control-plane observed traffic。 |
| `accumulateEpsResidualTraffic` | 将 residual demand 按当前 pair probability 分摊到 `epsPhysicalLinkStates[srcLeaf][spine]` 和 `epsPhysicalLinkStates[dstLeaf][spine]`。 |
| `applyEpsWecmpDiagnosticLoad` | 将 synthetic diagnostic load 注入 hot spine 的 `observedTraffic`。 |
| `updateEpsPhysicalSmoothedUtilization` | 用 `observedTraffic / epsWecmpCapacity` 生成 control-plane utilization 并 EWMA 平滑。 |
| `runEpsWecmpUpdateForPair` | 基于 path metric、smoothed utilization、attractiveness 和 bounded probability update 选择 `selectedSpine`。 |
| matrix flow WECMP block | 在 `Simulator::Run()` 前，为 residual / fallback matrix flows 生成 `epsWecmpDecisions`。 |
| route binding block | 在 `Simulator::Run()` 前，用 `selectedSpine` 安装 explicit EPS host route，并设置 `epsPathFrozen` / `frozenSpine`。 |

当前 WECMP 输入负载是：

1. residual / fallback flow 的 `wecmpResidualDemand`；
2. 按当前 WECMP probability 分摊得到的 control-plane `observedTraffic`；
3. 可选 synthetic diagnostic load；
4. EWMA 平滑后的 control-plane `smoothedUtilization`。

当前 WECMP 明确不是 measured utilization：

- 日志输出 `source = control-plane-estimated-residual-load`；
- 日志输出 `ns3MeasuredUtilization = false`；
- `eps-wecmp-state.h` 注释说明 `observedTraffic` 不是 NS-3 measured per-link bytes。

当前 `selectedSpine` 产生位置：

1. `runEpsWecmpUpdateForPair` 根据 candidate spine 的 updated probability 选择最大 probability 的 spine，平局选较小 spine index；
2. matrix flow WECMP block 将 decision index 写入 `matrixFlowWecmpDecisionIndex`；
3. route binding block 读取 `epsWecmpDecisions[decisionIndex].selectedSpine`；
4. explicit host routes 安装后，flow spec 设置 `epsPathFrozen=true`、`frozenSpine=selectedSpine`。

当前 `wecmp.csv` 字段：

- `decisionIndex`
- `srcLeaf`
- `dstLeaf`
- `residualDemand`
- `selectedSpine`
- `spineIndex`
- `pathLoadMetric`
- `candidatePathLoad`
- `attractiveness`
- `normalizedAttractiveness`
- `targetProbability`
- `previousProbability`
- `updatedProbability`
- `probabilityDelta`
- `boundedProbabilityDelta`

当前 WECMP decision 发生在 NS-3 数据面运行前。它不能读取 runtime measured utilization，因为 Patch 3 samples 只有在 `Simulator::Run()` 期间才由 scheduled callback 产生。

## 3. Patch 3 measured utilization 代码地图

Patch 3 measured link utilization 当前实现位置在 `src/main/hybrid-dcn-main.cc`：

| 结构 / 函数 | 作用 |
|---|---|
| `LinkCounter` | 每个方向级 link 的 cumulative Tx packets/bytes、capacity、endpoint、direction 和 last sample state。 |
| `LinkTxTrace` | 绑定到 PointToPoint `MacTx` trace，每次 Tx packet 增加 `txPackets` 和 `txBytes`。 |
| `LinkUtilizationSample` | 存储一次采样窗口的 `deltaTxPackets`、`deltaTxBytes`、cumulative counters、throughput 和 utilization。 |
| `SampleLinkUtilizationTimeSeries` | 在 `Simulator::Run()` 期间周期性读取 `LinkCounter`，计算 sample throughput 和 utilization。 |
| `link-timeseries.csv` export | 仿真结束后导出全部采样行。 |
| summary time-series fields | 仿真结束后汇总 rows/nonzero/max/avg utilization。 |

`LinkCounter` 当前记录的方向级链路：

- EPS leaf-spine：每个 leaf-spine link 两个方向，`leaf-to-spine` 和 `spine-to-leaf`，`linkType=eps-leaf-spine`；
- selected OCS：每条 selected OCS link 两个方向，`linkType=ocs`；
- 不包含 server-leaf link counter。

Patch 3 sample 公式：

- `deltaTxPackets = txPackets - lastSampleTxPackets`
- `deltaTxBytes = txBytes - lastSampleTxBytes`
- `sampleThroughputMbps = deltaTxBytes * 8 / intervalSeconds / 1e6`
- `utilizationApprox = sampleThroughputMbps / capacityMbps`

当前 measured utilization 只是观测结果。它不参与任何 route、WECMP probability、selectedSpine 或 frozenSpine 决策。`link-timeseries.csv` 也是仿真结束后写出，不能作为同一轮仿真中已发生决策的输入。

## 4. 可行性分析

Measured utilization 可以接入 EPS-WECMP，但必须改变决策时机或引入在线 snapshot 数据结构。当前代码中，WECMP decision 和 explicit EPS host route 都在 `Simulator::Run()` 前完成；Patch 3 measured samples 在 `Simulator::Run()` 中产生。因此不能把现有 `link-timeseries.csv` 或 post-run summary 直接接入当前 pre-run WECMP decision。

可行的接入点有两个层级：

1. 数据通路层：在 runtime sampling callback 中，同时维护一个 per-EPS-directed-link latest measured utilization snapshot，供后续在线 decision 读取。
2. 决策层：只对“后续新流”在某个 scheduled time 之后运行 WECMP decision，并安装对应 explicit host routes；已经启动并冻结路径的 flow 不变。

如果不引入后续新流或 scheduled online route binding，那么 measured utilization 只能被证明为“被采集和导出”，不能证明“影响 selectedSpine”。这决定了 Patch 4B 是否需要拆分。

## 5. 风险分析

### 时间尺度风险

当前 residual / fallback matrix flows 的 WECMP decision 在 `Simulator::Run()` 前完成，当时没有 runtime measured sample。若直接在该位置读取 measured utilization，只会读到初始化值 0，或者错误使用仿真结束后的数据。

若新流路径在仿真开始前一次性安装，runtime measured utilization 只能作为事后指标，不能影响这些已冻结路径。

### 因果性风险

不能用仿真结束后的 `link-timeseries.csv` 或 summary 反向决定同一轮已经启动的 flow 路径。这会构成 look-ahead / causal violation。

Patch 4B 必须明确 measured mode 的数据来自当前仿真时间之前已经发生的 samples，而不是 post-run CSV。

### 路由稳定性风险

Patch 2.6 的核心是 explicit EPS host route 和 `frozenSpine`。Measured WECMP 不能破坏该机制：

- 已建立 flow 不重路由；
- 已设置 `frozenSpine` 的 flow 不被重新选择 spine；
- 新 flow 如果用 measured mode，也应通过同一套 explicit EPS host route 安装路径；
- fallback / residual flow 仍不能泄漏到 selected OCS link。

### 指标语义风险

必须区分三种量：

- control-plane estimated residual load：当前 `observedTraffic`；
- synthetic diagnostic load：用于诊断的热 spine 注入；
- NS-3 measured Tx utilization：来自 `MacTx` counter 的 runtime measured value。

不能把当前 `observedTraffic` 叫做 measured utilization，也不能把 post-run CSV 指标说成 online measured load。

### 工程复杂度风险

不应在 Patch 4B 一次性引入多周期动态重路由、packet-level rerouting 或已建立 flow 迁移。NS-3 static host route 是 host-pair 粒度，当前工程还不支持同一 src/dst host pair 按 TCP port 同时分到不同 spine。Patch 4B 应避免让两个不同阶段的 flow 共享同一 src/dst host pair 却要求不同路径。

## 6. Patch 4B 最小可行设计

推荐 Patch 4B 先实现 measured utilization 数据通路和 CSV 证明，默认行为保持完全兼容。若实现 selectedSpine 改变，应限制为一个非常小的二阶段新流场景，并保证只影响后续新流。

### 新参数建议

| 参数 | 默认值 | 语义 |
|---|---|---|
| `epsWecmpLoadSource` | `control-plane` | 可选 `control-plane`、`measured-snapshot`。默认保持旧 WECMP 行为。 |
| `measuredWecmpSampleInterval` | `0.1` 或复用 `linkUtilizationSampleInterval` | 若只保留一套 sampling，优先复用 Patch 3 sample interval，避免双重采样。若后续需要不同控制频率，再新增独立参数。 |
| `measuredWecmpWarmupTime` | `0.0` 或 `0.2` | measured mode 在该时间之前不做 measured decision；没有足够 sample 时 fallback 到 control-plane 或标记 no-measurement。 |
| `enableMeasuredWecmpRouting` | `false` | 仅当显式开启时允许后续新流基于 measured snapshot 安装 EPS route。也可以用 `epsWecmpLoadSource=measured-snapshot && enableEpsWecmpRouting=true` 替代，避免参数过多。 |
| `measuredWecmpNoSampleFallback` | `control-plane` | 可选 `control-plane`、`zero`、`error`；推荐默认 `control-plane`。 |

参数原则：

- 默认 `epsWecmpLoadSource=control-plane`，保证旧实验兼容；
- measured mode 必须显式开启；
- 没有 enough samples 时不能伪造 measured load，应记录 fallback 或 no-measurement。

### 数据结构建议

新增 runtime-only measured snapshot，而不是从 CSV 读回：

- `MeasuredEpsLinkSnapshot { leafIndex, spineIndex, leafToSpineUtilization, spineToLeafUtilization, lastSampleTime, hasLeafToSpineSample, hasSpineToLeafSample }`
- 或两个矩阵：
  - `epsMeasuredLeafToSpineUtilization[leaf][spine]`
  - `epsMeasuredSpineToLeafUtilization[spine][leaf]`

需要从 `LinkCounter` 映射到 EPS directed link：

- `linkType == "eps-leaf-spine"`；
- `endpointAType == "leaf" && endpointBType == "spine"` 表示 leaf-to-spine；
- `endpointAType == "spine" && endpointBType == "leaf"` 表示 spine-to-leaf；
- capacity 使用 counter 自带 `capacityGbps`；
- OCS counters 不进入 WECMP measured EPS load。

Path-level measured load 建议沿用现有 `epsWecmpPathMetric`：

- `max`：`max(srcLeaf->spine utilization, spine->dstLeaf utilization)`；
- `average`：两段 directed utilization 的平均；
- 如果其中任一方向没有 sample，标记 `hasMeasuredSample=false` 并按 fallback 策略处理。

不要复用 `EpsPhysicalLinkState::observedTraffic` 直接混装不同语义，除非同时新增 `loadSource` 字段并清晰命名。更安全的做法是保留 control-plane state，新增 measured snapshot，再在 decision 组装阶段选择输入源。

### 决策时机建议

当前架构下，pre-run WECMP decision 不能假装使用 runtime measured utilization。Patch 4B 有两个可选范围：

1. 数据通路 Patch 4B：在 Patch 3 sampling callback 中维护 latest EPS measured snapshot，并在 CSV / logs 中证明 snapshot 可用；WECMP decision 仍默认 control-plane。
2. 二阶段新流 Patch 4B：新增一个 scheduled later matrix flow 或 later residual flow，在 `measuredWecmpWarmupTime` 之后运行 measured WECMP decision，只给该后续新流安装 explicit EPS host route。

如果选择方案 2，需要避免已建立流重路由，并避免同一 host pair 的 route 覆盖问题。建议使用不同 server offset 或不同 dst leaf pair，让 later flow 的 host pair 不与早期 flow 冲突。

### CSV 追加字段建议

summary 追加字段：

- `epsWecmpLoadSource`
- `measuredWecmpEnabled`
- `measuredWecmpWarmupTime`
- `measuredWecmpSampleIntervalSeconds`
- `measuredWecmpSnapshotCount`
- `measuredWecmpDecisionCount`
- `measuredWecmpFallbackDecisionCount`
- `measuredWecmpNoSampleDecisionCount`
- `measuredWecmpChangedSelectedSpineCount`

wecmp.csv 只追加字段，不改旧字段：

- `loadSource`
- `hasMeasuredSample`
- `measuredSrcToSpineUtilization`
- `measuredSpineToDstUtilization`
- `measuredPathUtilization`
- `controlPlanePathLoadMetric`
- `effectivePathLoadMetric`
- `noSampleFallbackMode`
- `decisionTimeSeconds`
- `appliesToLaterFlow`

可选新增 `measured-wecmp.csv`，用于逐 directed EPS link 输出 latest snapshot：

- `experimentName`
- `sampleTimeSeconds`
- `leaf`
- `spine`
- `direction`
- `capacityMbps`
- `deltaTxBytes`
- `sampleThroughputMbps`
- `measuredUtilization`
- `hasSample`

如果新增 `measured-wecmp.csv`，仍应保留 Patch 3 `link-timeseries.csv` 不变。

## 7. Patch 4B 验证场景建议

共同硬约束：

- 保持 Patch 2.6 route fix；
- 保持 Patch 3 `link-timeseries.csv`；
- `overallResultConsistency=pass` 和 `overallAlgorithmInvariant=pass`；
- 不改变 TL-OCS、OCS admission、拓扑、链路速率、流量生成主逻辑；
- 所有新增 CSV 字段只追加。

建议场景：

1. Backward compatibility：
   - `epsWecmpLoadSource=control-plane`；
   - 复用 Patch 3 WECMP binding 场景；
   - selectedSpine / frozenSpine 与 Patch 2.6 / Patch 3 一致。

2. Measured snapshot data path：
   - `epsWecmpLoadSource=measured-snapshot`；
   - 有 warmup traffic 产生 EPS measured samples；
   - 只验证 measured snapshot CSV 和 wecmp.csv 追加字段，不要求 selectedSpine 改变。

3. No-sample fallback：
   - measured mode 开启，但 decision 时间早于 first sample；
   - 预期 `hasMeasuredSample=false`，fallback 到 control-plane 或明确标记 no-measurement；
   - 不崩溃。

4. SelectedSpine change proof：
   - 需要 warmup flow 先压高某个 spine 的 measured utilization；
   - later residual / fallback flow 在 warmup 之后到达；
   - measured WECMP decision 选择另一个 spine；
   - route binding 只影响 later flow；
   - OCS link 仍无 fallback leakage。

场景 4 如果需要新增二阶段 flow 启动机制，建议留到 Patch 4C，避免 Patch 4B 同时改数据通路、决策时机和流量生成。

## 8. Patch 4B / 4C 拆分建议

推荐拆分：

- Patch 4B：实现 measured snapshot 数据通路、参数、CSV 字段、no-sample fallback 语义和 backward compatibility。可证明 WECMP decision 能读取 measured snapshot metadata，但不强制证明 selectedSpine 改变。
- Patch 4C：新增最小二阶段 later flow 场景，让 measured snapshot 真正改变后续新流 selectedSpine，并验证 route fix 不回退。

理由：

1. 当前 WECMP decision 是 pre-run，直接让 measured load 改变 selectedSpine 需要引入 online decision timing；
2. 引入 later flow 可能触及 flow scheduling 和 host route 粒度，风险高于单纯数据通路；
3. 分拆可以先锁定语义：measured source、fallback、CSV、snapshot mapping；
4. Patch 4C 再专门处理“只影响后续新流”的路径安装验证。

如果坚持一个 Patch 4B 同时证明 selectedSpine 变化，必须把范围限制为一个小型二阶段场景，且不得改变已有 flow 路径，不得覆盖同一 host pair 的既有 route。

## 9. Patch 4B 必须遵守的设计原则

1. Measured utilization 只能影响后续新流，不能改变已建立流。
2. 默认行为必须保持旧 WECMP control-plane 模式。
3. Measured mode 必须由显式参数开启。
4. 如果没有足够 measured samples，应 fallback 到旧 control-plane load 或明确标记 no-measurement。
5. 不能使用仿真结束后的 CSV 反向影响同一轮路径。
6. 所有新增 CSV 字段只能追加，不能破坏旧 schema。
7. Patch 2.6 route fix 必须作为回归硬约束。
8. Patch 3 link-timeseries 必须继续可用。
9. 不做 packet-level rerouting。
10. 不改变已建立 flow。
11. 不做大规模论文实验。
12. 不改变 TL-OCS 或 OCS admission。

## 10. 结论

当前 WECMP 不能直接使用 measured utilization。原因是 WECMP decision 和 route binding 发生在 `Simulator::Run()` 前，而 Patch 3 measured samples 发生在 `Simulator::Run()` 期间并在仿真结束后导出。直接把 post-run CSV 或 summary 接入 WECMP 会违反因果性。

可以进入 Patch 4B，但 Patch 4B 应优先实现 measured snapshot 数据通路和 CSV 证明：

- 新增显式 load source 参数，默认保持 `control-plane`；
- 将 EPS directed `LinkCounter` sample 映射到 runtime measured snapshot；
- 在 WECMP decision 记录中追加 load source、measured sample availability、measured path utilization 等字段；
- 在 no-sample 时 fallback 或明确标记；
- 保持 Patch 2.6 route fix 和 Patch 3 link-timeseries 回归。

Patch 4B 不应该一次性实现：

- packet-level rerouting；
- 已建立 flow 重路由；
- 多周期动态重路由；
- 大规模论文实验；
- TL-OCS 或 OCS admission 改动；
- 使用仿真结束后的 CSV 决定当前路径。

推荐采用 Patch 4B / Patch 4C 拆分。Patch 4B 做 measured data path 和语义证明；Patch 4C 再用最小二阶段 later flow 证明 measured utilization 可以改变后续新流的 selectedSpine。

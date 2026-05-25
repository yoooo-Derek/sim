# TL-OCS Patch 1 指标字段补丁验证

## 1. 补丁目的

本补丁只追加 FCT 和 summary 派生指标，不改变 TL-OCS 控制算法、流量矩阵、Louvain、OCS 选边、OCS admission、EPS-WECMP、拓扑、路由或 BulkSend 流量生成逻辑。

新增字段用于让当前 structured export 更接近论文基础指标需求：

- flows CSV 增加 `pathType`、`fctSeconds`、`rxDurationSeconds`；
- summary CSV 增加 completed flow、总收包字节、aggregate goodput、平均 FCT、p99 FCT、OCS 字节占比、OCS hit ratio、EPS fallback ratio、residual flow ratio。

## 2. 修改范围

本轮修改文件：

| 文件 | 修改区域 | 修改内容 |
|---|---|---|
| `src/main/hybrid-dcn-main.cc` | `5705-5745` | 增加 matrix flow 派生指标 helper：`computeMatrixFlowRxDurationSeconds`、`computeMatrixFlowFctSeconds`、`classifyMatrixFlowPathType` |
| `src/main/hybrid-dcn-main.cc` | `5931-6020` | 增加 completed flow FCT 收集、平均 FCT、nearest-rank p99、byte share 和 ratio 派生 |
| `src/main/hybrid-dcn-main.cc` | `6145-6201` | 在结果日志中输出新增派生指标和 per-flow path/FCT 字段 |
| `src/main/hybrid-dcn-main.cc` | `6550-6715` | 在 summary/flows CSV 尾部追加新字段，保留旧字段顺序 |
| `docs/tl_ocs_patch1_metrics_validation.md` | 全文 | 本验证文档 |

本轮未修改：

- TL-OCS 选边逻辑；
- OCS admission 逻辑；
- EPS-WECMP 决策逻辑；
- 拓扑、链路速率、路由、BulkSend 安装逻辑；
- `links.csv`、`link_timeseries.csv`、`epochs.csv`；
- per-link counter 或 WECMP measured utilization；
- 仓库 `results/raw` 历史文件。

## 3. 新增 CSV 字段

summary CSV 新增字段均追加在旧字段之后：

| 字段 | 单位 | 定义 |
|---|---|---|
| `completedFlowCount` | flows | 与旧字段 `completedFlows` 同值 |
| `totalRxBytes` | bytes | 所有 matrix flows 的 `rxBytes` 总和 |
| `aggregateGoodputMbps` | Mbps | 与旧字段 `avgGoodputMbps` 同义，使用当前已有 aggregate goodput 变量 |
| `avgFctSeconds` | s | completed flows 的 `fctSeconds` 平均值；无 completed flow 时为 0 |
| `p99FctSeconds` | s | completed flows 的 nearest-rank p99；无 completed flow 时为 0 |
| `ocsByteShare` | ratio | `ocsTxBytes / (ocsTxBytes + epsTxBytes)`；分母为 0 时为 0 |
| `ocsHitRatio` | ratio | `ocsCoveredFlowCount / matrixFlowCount`；分母为 0 时为 0 |
| `epsFallbackRatio` | ratio | `fallbackFlowCount / matrixFlowCount`；分母为 0 时为 0 |
| `residualFlowRatio` | ratio | `epsResidualFlowCount / matrixFlowCount`；分母为 0 时为 0 |

flows CSV 新增字段均追加在旧字段之后：

| 字段 | 单位 | 定义 |
|---|---|---|
| `pathType` | string | `ocs` / `eps-fallback` / `eps-residual` / `unknown` |
| `fctSeconds` | s | completed flow 的 `lastRx - startTime`；未完成或无 Rx 时为 0 |
| `rxDurationSeconds` | s | `lastRx - firstRx`；无 Rx 时为 0 |

## 4. FCT 口径

本补丁按以下口径实现：

- `fctSeconds = lastRx - startTime`。
- `fctSeconds` 只对 `completed=true` 且 `seenFirstRx=true` 的 flow 计算；未完成或无 Rx 时 CSV 输出 `0`，由 `completed=false` 区分。
- `rxDurationSeconds = lastRx - firstRx`。
- `goodputMbps` 保持当前源码原语义，仍以 `rxDurationSeconds` 作为分母，即 `rxBytes * 8 / rxDurationSeconds / 1e6`。
- `p99FctSeconds` 使用 nearest-rank：对 completed flow 的 `fctSeconds` 升序排序，`index = ceil(0.99*n) - 1`，并 clamp 到 `[0, n-1]`。

## 5. 验收命令

准备目录：

```bash
mkdir -p /tmp/tl-ocs-patch1-metrics/ns3-cwd /tmp/tl-ocs-patch1-metrics/sim/results/raw
```

构建：

```bash
cd /home/dyn/ns-3.47
./ns3 build
```

场景 1：OCS hit：

```bash
cd /tmp/tl-ocs-patch1-metrics/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch1-ocs-hit --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=false --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch1-metrics" 2>&1 | tee /tmp/tl-ocs-patch1-metrics/patch1-ocs-hit.log; exit ${PIPESTATUS[0]}
```

场景 2：admission fallback：

```bash
cd /tmp/tl-ocs-patch1-metrics/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch1-admission-fallback --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch1-metrics" 2>&1 | tee /tmp/tl-ocs-patch1-metrics/patch1-admission-fallback.log; exit ${PIPESTATUS[0]}
```

场景 3：WECMP binding：

```bash
cd /tmp/tl-ocs-patch1-metrics/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch1-wecmp-binding --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=true --enableEpsWecmpRouting=true --epsWecmpDiagnosticLoadMode=hot-spine --epsWecmpDiagnosticLoad=50 --epsWecmpDiagnosticHotSpine=0 --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch1-metrics" 2>&1 | tee /tmp/tl-ocs-patch1-metrics/patch1-wecmp-binding.log; exit ${PIPESTATUS[0]}
```

构建和三个场景均返回 0。

## 6. CSV 验证结果

summary CSV 关键字段：

| 场景 | selectedOcsEdges | completedFlowCount | totalRxBytes | aggregateGoodputMbps | avgFctSeconds | p99FctSeconds | ocsByteShare | ocsHitRatio | epsFallbackRatio | residualFlowRatio | result | invariant |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| patch1-ocs-hit | 0-3 | 3 | 1572864 | 10.9417 | 0.000413179 | 0.000449692 | 0.2 | 0.333333 | 0 | 0.666667 | pass | pass |
| patch1-admission-fallback | 0-3 | 3 | 1572864 | 10.9417 | 0.000413179 | 0.000449692 | 0.2 | 0 | 0.333333 | 1 | pass | pass |
| patch1-wecmp-binding | 0-3 | 3 | 1572864 | 10.9417 | 0.000413179 | 0.000449692 | 0.2 | 0 | 0.333333 | 1 | pass | pass |

flows CSV 关键字段：

| 场景 | flowIndex | pair | pathType | completed | fctSeconds | rxDurationSeconds | frozenSpine |
|---|---:|---|---|---|---:|---:|---:|
| patch1-ocs-hit | 0 | 0-3 | ocs | true | 0.000449692 | 0.000428164 | -1 |
| patch1-ocs-hit | 1 | 0-1 | eps-residual | true | 0.000394922 | 0.000376167 | -1 |
| patch1-ocs-hit | 2 | 0-2 | eps-residual | true | 0.000394922 | 0.000376167 | -1 |
| patch1-admission-fallback | 0 | 0-3 | eps-fallback | true | 0.000449692 | 0.000428164 | -1 |
| patch1-admission-fallback | 1 | 0-1 | eps-residual | true | 0.000394922 | 0.000376167 | -1 |
| patch1-admission-fallback | 2 | 0-2 | eps-residual | true | 0.000394922 | 0.000376167 | -1 |
| patch1-wecmp-binding | 0 | 0-3 | eps-fallback | true | 0.000449692 | 0.000428164 | 1 |
| patch1-wecmp-binding | 1 | 0-1 | eps-residual | true | 0.000394922 | 0.000376167 | 1 |
| patch1-wecmp-binding | 2 | 0-2 | eps-residual | true | 0.000394922 | 0.000376167 | 1 |

复算结果：

| 场景 | flows 复算 avg FCT | flows 复算 p99 FCT | `fctSeconds >= rxDurationSeconds` |
|---|---:|---:|---|
| patch1-ocs-hit | 0.000413179 | 0.000449692 | 通过 |
| patch1-admission-fallback | 0.000413179 | 0.000449692 | 通过 |
| patch1-wecmp-binding | 0.000413179 | 0.000449692 | 通过 |

字段兼容性检查：

- 原 summary 字段仍存在，包括 `completedFlows`、`completionRatio`、`avgGoodputMbps`、`overallResultConsistency`、`overallAlgorithmInvariant`。
- 原 flows 字段仍存在，包括 `rxBytes`、`completed`、`completionRatio`、`firstRx`、`lastRx`、`goodputMbps`。
- 新字段均追加在行末，没有删除或重命名旧字段。

## 7. 回归检查

| 检查项 | 结果 | 依据 |
|---|---|---|
| selected OCS edge 是否仍为 0-3 | 是 | 三个 summary CSV 均 `selectedOcsEdges=0-3`；日志 `finalEdge[0] = 0-3 ... installed=true` |
| OCS hit 是否仍成立 | 是 | `patch1-ocs-hit` flow 0 为 `pathType=ocs`、`ocsCovered=true` |
| admission fallback 是否仍成立 | 是 | `patch1-admission-fallback` flow 0 为 `pathType=eps-fallback`，日志 `fallbackFlows = 1`、`ocsPairHostRoutesSkippedForEpsResidual = 4` |
| WECMP binding 是否仍成立 | 是 | `patch1-wecmp-binding` 日志 `bindings = 3`，三条 flow 均 `frozenSpine=1` |
| overallResultConsistency | pass | 三个 summary CSV 均为 `pass` |
| overallAlgorithmInvariant | pass | 三个 summary CSV 均为 `pass` |

## 8. 是否可以进入 Patch 2

建议可以进入 Patch 2：per-link aggregate counters。

Patch 1 已经补齐论文基础 flow/summary 派生指标，但仍然只有聚合 `ocsTxBytes` 和 `epsTxBytes`，不能支撑 per-link utilization 或严格 per-flow path attribution。下一步应按第四阶段 A 的计划增加 per-link aggregate counters，并继续保持“不接入 WECMP measured utilization”的边界。

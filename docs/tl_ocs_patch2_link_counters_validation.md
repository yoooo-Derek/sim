# TL-OCS Patch 2 Per-link Counters 验证

## 1. 补丁目的

本补丁只新增 per-link aggregate counters 和 `links.csv` 结构化输出，用于观察已安装 OCS link 与 leaf-spine EPS link 的方向级 `MacTx` packets / bytes。

本补丁不改变 TL-OCS 控制算法、流量矩阵、Louvain、OCS 选边、OCS admission、EPS-WECMP decision、拓扑规模、链路速率、路由或 BulkSend 流量生成逻辑。本补丁也不实现 `link_timeseries.csv`，不实现 WECMP measured utilization，不把 per-link counter 接入 WECMP。

## 2. 修改范围

本轮修改文件：

| 文件 | 修改区域 | 说明 |
|---|---|---|
| `src/main/hybrid-dcn-main.cc` | `LinkCounter` / `LinkTxTrace`，约第 102-153 行 | 新增方向级链路计数结构和 `MacTx` callback。 |
| `src/main/hybrid-dcn-main.cc` | EPS / OCS 链路安装区域，约第 3215-3377 行 | 为每条 leaf-spine EPS P2P link 和每条 selected OCS P2P link 的两个方向注册 counter。 |
| `src/main/hybrid-dcn-main.cc` | result summary 统计区域，约第 6149-6198、6352-6365 行 | 计算方向级 counter 数量和 coarse aggregate utilization 摘要。 |
| `src/main/hybrid-dcn-main.cc` | structured export 区域，约第 6691-6711、6775-6830、7035-7079 行 | 追加 summary 字段并新增 `<experimentName>-links.csv`。 |
| `docs/tl_ocs_patch2_link_counters_validation.md` | 全文 | 记录 Patch 2 验证过程和结果。 |

未修改内容：

| 类别 | 状态 |
|---|---|
| TL-OCS 控制面公式与选边 | 未修改 |
| OCS admission | 未修改 |
| EPS-WECMP decision / probability | 未修改 |
| 路由和 BulkSend 安装逻辑 | 未修改 |
| `links.csv` 之外的新 CSV | 未新增 |
| 仓库 `results/raw` 历史结果 | 未修改 |

## 3. links CSV Schema

新增文件：`<experimentName>-links.csv`。

| 字段名 | 类型 | 单位 | 含义 |
|---|---:|---|---|
| `linkIndex` | uint32 | - | 导出顺序编号。 |
| `linkId` | string | - | 稳定链路 ID。 |
| `linkType` | string | - | `ocs` 或 `eps-leaf-spine`。 |
| `direction` | string | - | `a-to-b` 或 `b-to-a`。 |
| `endpointAType` | string | - | `leaf` 或 `spine`。 |
| `endpointA` | uint32 | - | endpoint A 编号。 |
| `endpointBType` | string | - | `leaf` 或 `spine`。 |
| `endpointB` | uint32 | - | endpoint B 编号。 |
| `capacityGbps` | double | Gbps | 链路容量。EPS leaf-spine 为 40，OCS 使用 `ocsDataRate` 转换。 |
| `delay` | string | - | 链路 delay 字符串。EPS leaf-spine 为 `2us`，OCS 使用 `ocsDelay`。 |
| `txPackets` | uint64 | packets | 该方向 `MacTx` packet 数。 |
| `txBytes` | uint64 | bytes | 该方向 `MacTx` bytes。 |
| `utilizationApprox` | double | ratio | 全仿真时长上的粗粒度平均利用率。 |
| `note` | string | - | `selected-ocs` 或 `eps-fabric`。 |

`linkId` 命名：

| linkType | 示例 |
|---|---|
| EPS leaf-to-spine | `eps-leaf0-spine1-leaf-to-spine` |
| EPS spine-to-leaf | `eps-leaf0-spine1-spine-to-leaf` |
| OCS a-to-b | `ocs-leaf0-leaf3-a-to-b` |
| OCS b-to-a | `ocs-leaf0-leaf3-b-to-a` |

本轮只导出 selected OCS link 和 leaf-spine EPS link，不导出 server-leaf link。

## 4. summary 新增字段

在 Patch 1 已有 summary 字段尾部追加：

| 字段名 | 单位 | 定义 |
|---|---|---|
| `linkCounterCount` | counters | `links.csv` 中方向级 counter 总数。 |
| `ocsLinkCounterCount` | counters | `linkType=ocs` 的方向级 counter 数。 |
| `epsLinkCounterCount` | counters | `linkType=eps-leaf-spine` 的方向级 counter 数。 |
| `maxOcsLinkUtilizationApprox` | ratio | OCS 方向级 counter 的最大 `utilizationApprox`；无 OCS 时为 0。 |
| `maxEpsLinkUtilizationApprox` | ratio | EPS leaf-spine 方向级 counter 的最大 `utilizationApprox`；无 EPS 时为 0。 |
| `avgEpsLinkUtilizationApprox` | ratio | EPS leaf-spine 方向级 counter 的平均 `utilizationApprox`；无 EPS 时为 0。 |
| `epsLinkUtilizationStddevApprox` | ratio | EPS leaf-spine 方向级 counter 的总体标准差；无 EPS 时为 0。 |

保留原有 `ocsTxBytes`、`epsTxBytes`、Patch 1 的 `avgFctSeconds`、`p99FctSeconds`、`pathType`、`fctSeconds`、`rxDurationSeconds` 等字段。

## 5. utilizationApprox 口径

`utilizationApprox` 定义：

```text
utilizationApprox = txBytes * 8 / (simTime * capacityGbps * 1e9)
```

该值是整个仿真时长上的粗粒度平均利用率。它不是时间序列利用率，不反映短时间窗口峰值，也不能直接作为 WECMP measured utilization。

当前 EPS-WECMP 的 `observedTraffic` / `utilization` 仍然来自 control-plane estimated residual load 和诊断注入负载，不是 NS-3 per-link measured utilization。

## 6. 验收命令

准备目录：

```bash
mkdir -p /tmp/tl-ocs-patch2-links/ns3-cwd /tmp/tl-ocs-patch2-links/sim/results/raw
```

构建：

```bash
cd /home/dyn/ns-3.47
./ns3 build
```

场景 1：OCS hit：

```bash
cd /tmp/tl-ocs-patch2-links/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2-ocs-hit --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=false --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-links" 2>&1 | tee /tmp/tl-ocs-patch2-links/patch2-ocs-hit.log; exit ${PIPESTATUS[0]}
```

场景 2：admission fallback：

```bash
cd /tmp/tl-ocs-patch2-links/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2-admission-fallback --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-links" 2>&1 | tee /tmp/tl-ocs-patch2-links/patch2-admission-fallback.log; exit ${PIPESTATUS[0]}
```

场景 3：WECMP binding：

```bash
cd /tmp/tl-ocs-patch2-links/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2-wecmp-binding --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=true --enableEpsWecmpRouting=true --epsWecmpDiagnosticLoadMode=hot-spine --epsWecmpDiagnosticLoad=50 --epsWecmpDiagnosticHotSpine=0 --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-links" 2>&1 | tee /tmp/tl-ocs-patch2-links/patch2-wecmp-binding.log; exit ${PIPESTATUS[0]}
```

构建和三个场景均返回 0。

## 7. CSV 验证结果

三个场景均生成：

| 场景 | summary | flows | wecmp | ocs-candidates | links |
|---|---|---|---|---|---|
| `patch2-ocs-hit` | `/tmp/tl-ocs-patch2-links/patch2-ocs-hit-summary.csv` | `/tmp/tl-ocs-patch2-links/patch2-ocs-hit-flows.csv` | `/tmp/tl-ocs-patch2-links/patch2-ocs-hit-wecmp.csv` | `/tmp/tl-ocs-patch2-links/patch2-ocs-hit-ocs-candidates.csv` | `/tmp/tl-ocs-patch2-links/patch2-ocs-hit-links.csv` |
| `patch2-admission-fallback` | `/tmp/tl-ocs-patch2-links/patch2-admission-fallback-summary.csv` | `/tmp/tl-ocs-patch2-links/patch2-admission-fallback-flows.csv` | `/tmp/tl-ocs-patch2-links/patch2-admission-fallback-wecmp.csv` | `/tmp/tl-ocs-patch2-links/patch2-admission-fallback-ocs-candidates.csv` | `/tmp/tl-ocs-patch2-links/patch2-admission-fallback-links.csv` |
| `patch2-wecmp-binding` | `/tmp/tl-ocs-patch2-links/patch2-wecmp-binding-summary.csv` | `/tmp/tl-ocs-patch2-links/patch2-wecmp-binding-flows.csv` | `/tmp/tl-ocs-patch2-links/patch2-wecmp-binding-wecmp.csv` | `/tmp/tl-ocs-patch2-links/patch2-wecmp-binding-ocs-candidates.csv` | `/tmp/tl-ocs-patch2-links/patch2-wecmp-binding-links.csv` |

summary 新增字段摘要：

| 场景 | selectedOcsEdges | linkCounterCount | ocsLinkCounterCount | epsLinkCounterCount | maxOcsLinkUtilizationApprox | maxEpsLinkUtilizationApprox | avgEpsLinkUtilizationApprox | epsLinkUtilizationStddevApprox |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `patch2-ocs-hit` | `0-3` | 18 | 2 | 16 | `3.07904e-05` | `0.000153952` | `2.01297e-05` | `4.26789e-05` |
| `patch2-admission-fallback` | `0-3` | 18 | 2 | 16 | `3.07904e-05` | `0.000153952` | `2.01297e-05` | `4.26789e-05` |
| `patch2-wecmp-binding` | `0-3` | 18 | 2 | 16 | `3.07904e-05` | `0.000153952` | `2.01297e-05` | `4.26789e-05` |

方向级 counter 数量：

| 场景 | 总 counters | OCS counters | EPS leaf-spine counters | OCS 非零方向数 | EPS 非零方向数 |
|---|---:|---:|---:|---:|---:|
| `patch2-ocs-hit` | 18 | 2 | 16 | 2 | 6 |
| `patch2-admission-fallback` | 18 | 2 | 16 | 2 | 6 |
| `patch2-wecmp-binding` | 18 | 2 | 16 | 2 | 6 |

该 18 个方向级 counters 符合 4 leaves、2 spines、1 selected OCS link 的预期：

```text
EPS leaf-spine: 4 * 2 * 2 directions = 16
OCS selected link: 1 * 2 directions = 2
total = 18
```

OCS counters 摘要：

| 场景 | linkId | txPackets | txBytes | utilizationApprox |
|---|---|---:|---:|---:|
| `patch2-ocs-hit` | `ocs-leaf0-leaf3-a-to-b` | 982 | 577320 | `3.07904e-05` |
| `patch2-ocs-hit` | `ocs-leaf0-leaf3-b-to-a` | 492 | 26572 | `1.41717e-06` |
| `patch2-admission-fallback` | `ocs-leaf0-leaf3-a-to-b` | 982 | 577320 | `3.07904e-05` |
| `patch2-admission-fallback` | `ocs-leaf0-leaf3-b-to-a` | 492 | 26572 | `1.41717e-06` |
| `patch2-wecmp-binding` | `ocs-leaf0-leaf3-a-to-b` | 982 | 577320 | `3.07904e-05` |
| `patch2-wecmp-binding` | `ocs-leaf0-leaf3-b-to-a` | 492 | 26572 | `1.41717e-06` |

EPS counters 摘要：

| 场景 | 非零 EPS linkId 示例 | txPackets | txBytes | utilizationApprox |
|---|---|---:|---:|---:|
| `patch2-ocs-hit` | `eps-leaf0-spine0-leaf-to-spine` | 1964 | 1154640 | `0.000153952` |
| `patch2-ocs-hit` | `eps-leaf1-spine0-spine-to-leaf` | 982 | 577320 | `7.6976e-05` |
| `patch2-admission-fallback` | `eps-leaf0-spine0-leaf-to-spine` | 1964 | 1154640 | `0.000153952` |
| `patch2-admission-fallback` | `eps-leaf2-spine0-spine-to-leaf` | 982 | 577320 | `7.6976e-05` |
| `patch2-wecmp-binding` | `eps-leaf0-spine1-leaf-to-spine` | 1964 | 1154640 | `0.000153952` |
| `patch2-wecmp-binding` | `eps-leaf2-spine1-spine-to-leaf` | 982 | 577320 | `7.6976e-05` |

WECMP binding 场景中非零 EPS traffic 出现在 `spine1` 对应 link 上，与 `wecmp.csv` 中 `selectedSpine=1`、`flows.csv` 中 `frozenSpine=1` 一致。

Patch 1 字段仍存在：

| 场景 | flow 0 pathType | flow 0 fctSeconds | flow 0 rxDurationSeconds |
|---|---|---:|---:|
| `patch2-ocs-hit` | `ocs` | `0.000449692` | `0.000428164` |
| `patch2-admission-fallback` | `eps-fallback` | `0.000449692` | `0.000428164` |
| `patch2-wecmp-binding` | `eps-fallback` | `0.000449692` | `0.000428164` |

未生成 `link_timeseries` 相关 CSV。

## 8. 回归检查

| 检查项 | 结果 | 依据 |
|---|---|---|
| selected OCS edge 是否仍为 0-3 | 通过 | 三个 summary 的 `selectedOcsEdges=0-3`；日志中 `finalEdge[0] = 0-3 ... installed=true`。 |
| OCS hit 是否仍成立 | 通过 | `patch2-ocs-hit-flows.csv` 中 flow 0 `pathType=ocs`，OCS counters 有非零 `txBytes`。 |
| admission fallback 是否仍成立 | 通过 | `patch2-admission-fallback.log` 中 `fallbackFlows = 1`；flow 0 `pathType=eps-fallback`。 |
| WECMP binding 是否仍成立 | 通过 | `patch2-wecmp-binding.log` 中 `[WECMP-ROUTE] bindings = 3`；flow 0/1/2 `frozenSpine=1`。 |
| Patch 1 字段是否仍存在 | 通过 | flows CSV 仍包含 `pathType`、`fctSeconds`、`rxDurationSeconds`；summary CSV 仍包含 `avgFctSeconds`、`p99FctSeconds`。 |
| `overallResultConsistency` | 通过 | 三个 summary 均为 `pass`。 |
| `overallAlgorithmInvariant` | 通过 | 三个 summary 均为 `pass`。 |
| 是否改变 WECMP decision 语义 | 未改变 | `wecmp.csv` 仍导出 control-plane decision 字段；本轮新增 counter 未接入 WECMP。 |

## 9. 是否可以进入 Patch 3

建议可以进入 Patch 3：`link_timeseries.csv`。

Patch 2 已提供方向级 aggregate counter，可以确认 selected OCS link 和 EPS leaf-spine link 的 `MacTx` bytes 分布。但当前 `utilizationApprox` 只是全仿真时长上的 coarse aggregate，不能支撑论文级链路利用率时间序列、峰值利用率、拥塞持续时间或 measured-utilization WECMP。

Patch 3 应只做固定 interval 采样和 `link_timeseries.csv` 导出，仍不要把 sampled utilization 接入 WECMP。WECMP measured utilization integration 应留到更后续的独立 Patch。

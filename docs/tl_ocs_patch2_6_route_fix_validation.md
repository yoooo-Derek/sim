# TL-OCS Patch 2.6 Route Fix 验证

## 1. 补丁目的

本补丁只修复 fallback / residual matrix flow 的真实 EPS 路由，使 `pathType=eps-fallback` 或 `pathType=eps-residual` 的流不再被 NS-3 IP 数据面转发到 selected OCS link。

本补丁不改变 TL-OCS 控制算法、流量矩阵、Louvain、OCS 选边公式、OCS admission 判定、EPS-WECMP probability / decision 算法、拓扑规模、链路速率或 BulkSend 流量生成逻辑。

## 2. 修改范围

本轮修改文件：

| 文件 | 修改区域 | 说明 |
|---|---|---|
| `src/main/hybrid-dcn-main.cc` | `RouteFixRecord` 与 route-fix 计数器，约 `3430` 附近 | 增加诊断计数和日志记录。 |
| `src/main/hybrid-dcn-main.cc` | `shouldSkipOcsHostRouteForEpsResidual`，约 `3708` 附近 | 对 residual / fallback flow 涉及的 host pair 更严格跳过冲突 OCS host route。 |
| `src/main/hybrid-dcn-main.cc` | matrix flow residual / fallback EPS route 安装，约 `3781` 附近 | 为 residual / fallback flow 显式安装 EPS host route。 |
| `src/main/hybrid-dcn-main.cc` | `[HYBRID-DCN][ROUTE-FIX]` 日志，约 `4566` 附近 | 输出跳过的 OCS route、安装的 EPS route 和 fallback route 记录。 |
| `docs/tl_ocs_patch2_6_route_fix_validation.md` | 新增文档 | 固化本轮修复与验证结果。 |

未修改内容：

- 未修改 TL-OCS 公式链路和选边结果；
- 未修改 OCS admission 判定逻辑；
- 未修改 EPS-WECMP decision 逻辑；
- 未新增 `link_timeseries.csv`；
- 未实现 WECMP measured utilization；
- 未修改 `results/raw` 历史结果。

## 3. 修复思路

Patch 2.5 中发生 OCS leakage 的原因是：selected OCS link 在 `Ipv4GlobalRoutingHelper::PopulateRoutingTables()` 前已安装并分配 IP，进入了 global routing；同时 `routeMode=ocs-forced` 会为 selected OCS pair 安装 host route。旧逻辑只跳过部分精确 server pair 的 OCS host route，非 WECMP fallback 没有显式 EPS fallback host route，WECMP binding 也可能被已有 OCS static / global route 抢占。

本补丁采用最小侵入方案：

1. 对 `requiresEpsResidualPath(spec)==true` 的 matrix flow，跳过与该 flow 的源 server 或目的 server 冲突的 OCS host route，避免 residual / fallback 目标 host 留下 OCS 优先路径。
2. 对每条 residual / fallback matrix flow 显式安装 EPS host route，覆盖 global routing 可能选到 OCS link 的行为。
3. 非 WECMP fallback / residual 使用确定性 `spine=0`，并记录为 `mode=deterministic-spine0`。
4. WECMP fallback / residual 使用已有 WECMP decision 的 `selectedSpine`，并记录为 `mode=wecmp-frozen`；本补丁只让 route binding 真实生效，不改变 WECMP decision。
5. OCS admitted flow 保持原有 OCS host route，OCS hit 场景仍应真实走 OCS。

当前仍然不支持同一 `srcHost/dstHost` 在 IP 层按 TCP port 同时分流到 OCS 和 EPS；本补丁的粒度仍是 host route 粒度。小矩阵场景中每条 matrix flow 使用固定 server pair，因此可以让 pathType 与真实 IP 路径一致。

## 4. 验收命令

准备目录：

```bash
mkdir -p /tmp/tl-ocs-patch2-6-route-fix/ns3-cwd /tmp/tl-ocs-patch2-6-route-fix/sim/results/raw
```

构建：

```bash
cd /home/dyn/ns-3.47
./ns3 build
```

场景 1：OCS hit 回归：

```bash
cd /tmp/tl-ocs-patch2-6-route-fix/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2.6-ocs-hit --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=false --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-6-route-fix" 2>&1 | tee /tmp/tl-ocs-patch2-6-route-fix/patch2.6-ocs-hit.log; exit ${PIPESTATUS[0]}
```

场景 2：Admission fallback 修复验证：

```bash
cd /tmp/tl-ocs-patch2-6-route-fix/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2.6-admission-fallback --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-6-route-fix" 2>&1 | tee /tmp/tl-ocs-patch2-6-route-fix/patch2.6-admission-fallback.log; exit ${PIPESTATUS[0]}
```

场景 3：WECMP binding 修复验证：

```bash
cd /tmp/tl-ocs-patch2-6-route-fix/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2.6-wecmp-binding --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=true --enableEpsWecmpRouting=true --epsWecmpDiagnosticLoadMode=hot-spine --epsWecmpDiagnosticLoad=50 --epsWecmpDiagnosticHotSpine=0 --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-6-route-fix" 2>&1 | tee /tmp/tl-ocs-patch2-6-route-fix/patch2.6-wecmp-binding.log; exit ${PIPESTATUS[0]}
```

构建和三个场景均返回 0。

## 5. 场景对比结果

| 场景 | flow0 pathType | flow0 frozenSpine | flow0 rxBytes | OCS 0-3 a-to-b txBytes | EPS leaf3 接收方向 txBytes | 是否修复 leakage |
| -- | -------------- | ----------------: | ------------: | ---------------------: | ---------------------: | ------------ |
| `patch2.6-ocs-hit` | `ocs` | -1 | 524288 | 577320 | 0 | OCS hit 基线，仍真实走 OCS |
| `patch2.6-admission-fallback` | `eps-fallback` | -1 | 524288 | 0 | 577320 (`spine0->leaf3`) | 是 |
| `patch2.6-wecmp-binding` | `eps-fallback` | 1 | 524288 | 0 | 577320 (`spine1->leaf3`) | 是 |

关键 summary 字段：

| 场景 | selectedOcsEdges | ocsCoveredFlowCount | fallbackFlowCount | epsResidualFlowCount | ocsTxBytes | epsTxBytes | ocsByteShare | overallResultConsistency | overallAlgorithmInvariant |
|---|---|---:|---:|---:|---:|---:|---:|---|---|
| `patch2.6-ocs-hit` | `0-3` | 1 | 0 | 2 | 603892 | 2415568 | 0.2 | `pass` | `pass` |
| `patch2.6-admission-fallback` | `0-3` | 0 | 1 | 3 | 0 | 3623352 | 0 | `pass` | `pass` |
| `patch2.6-wecmp-binding` | `0-3` | 0 | 1 | 3 | 0 | 3623352 | 0 | `pass` | `pass` |

关键 route-fix 日志：

| 场景 | skippedOcsRoutesForFallback | epsFallbackHostRoutes | deterministicEpsRoutes | wecmpFrozenRoutes | fallbackRoutes |
|---|---:|---:|---:|---:|---:|
| `patch2.6-ocs-hit` | 0 | 12 | 2 | 0 | 2 |
| `patch2.6-admission-fallback` | 12 | 18 | 3 | 0 | 3 |
| `patch2.6-wecmp-binding` | 12 | 18 | 0 | 3 | 3 |

## 6. links CSV 验证

三个场景的 `links.csv` 均为 19 行，含 1 行表头和 18 个方向级 counters：

- EPS leaf-spine：`4 leaves * 2 spines * 2 directions = 16`；
- OCS：`1 selected OCS link * 2 directions = 2`；
- 合计：18。

OCS counters：

| 场景 | `ocs-leaf0-leaf3-a-to-b` txBytes | `ocs-leaf0-leaf3-b-to-a` txBytes | 说明 |
|---|---:|---:|---|
| `patch2.6-ocs-hit` | 577320 | 26572 | flow0 真实走 OCS，反向主要为 ACK。 |
| `patch2.6-admission-fallback` | 0 | 0 | fallback flow0 未再走 OCS。 |
| `patch2.6-wecmp-binding` | 0 | 0 | WECMP fallback flow0 未再走 OCS。 |

EPS 非零 counters 摘要：

| 场景 | 关键 EPS counter | txBytes | 说明 |
|---|---|---:|---|
| `patch2.6-ocs-hit` | `eps-leaf0-spine0-leaf-to-spine` | 1154640 | 普通 residual flows 0-1、0-2 走 EPS spine0。 |
| `patch2.6-ocs-hit` | `eps-leaf1-spine0-spine-to-leaf` | 577320 | 0-1 residual 到 leaf1。 |
| `patch2.6-ocs-hit` | `eps-leaf2-spine0-spine-to-leaf` | 577320 | 0-2 residual 到 leaf2。 |
| `patch2.6-admission-fallback` | `eps-leaf0-spine0-leaf-to-spine` | 1731960 | 三条 residual/fallback 都走 spine0。 |
| `patch2.6-admission-fallback` | `eps-leaf3-spine0-spine-to-leaf` | 577320 | flow0 0-3 fallback 到 leaf3。 |
| `patch2.6-wecmp-binding` | `eps-leaf0-spine1-leaf-to-spine` | 1731960 | 三条 residual/fallback 都按 WECMP binding 走 spine1。 |
| `patch2.6-wecmp-binding` | `eps-leaf3-spine1-spine-to-leaf` | 577320 | flow0 0-3 fallback 经 frozenSpine=1 到 leaf3。 |

Patch 2 的 `utilizationApprox` 字段仍为全仿真时长粗粒度平均利用率，不是 time-series utilization，不能作为 WECMP measured utilization。

## 7. 回归检查

| 检查项 | 结果 | 依据 |
|---|---|---|
| TL-OCS selected edge 是否仍为 0-3 | 通过 | 三个 summary 均为 `selectedOcsEdges=0-3`。 |
| OCS hit 是否仍走 OCS | 通过 | `flow0 pathType=ocs`，OCS 0-3 a-to-b `txBytes=577320`。 |
| Admission fallback 是否真实走 EPS | 通过 | `flow0 pathType=eps-fallback`，OCS 0-3 a-to-b `txBytes=0`，`spine0->leaf3 txBytes=577320`。 |
| WECMP binding 是否真实走 frozenSpine | 通过 | `flow0 frozenSpine=1`，`spine1->leaf3 txBytes=577320`，OCS 0-3 a-to-b `txBytes=0`。 |
| Patch 1 字段是否仍存在 | 通过 | flows CSV 保留 `pathType`,`fctSeconds`,`rxDurationSeconds`；summary CSV 保留 `avgFctSeconds`,`p99FctSeconds`。 |
| Patch 2 links CSV 是否仍存在 | 通过 | 三个场景均生成 `*-links.csv`，counter 数量为 18。 |
| `overallAlgorithmInvariant` 是否 pass | 通过 | 三个场景均为 `pass`。 |
| `overallResultConsistency` 是否 pass | 通过 | 三个场景均为 `pass`。 |
| 是否生成 `link_timeseries.csv` | 未生成 | 本补丁未实现 Patch 3。 |
| WECMP 是否基于真实 measured utilization | 否 | 本补丁只修复 route binding 生效问题，未接入 sampled link utilization。 |

## 8. 是否可以进入 Patch 3

可以进入 Patch 3：`link_timeseries.csv`。

理由：

1. Patch 2.5 暴露的 fallback / residual OCS leakage 已在三个小场景中修复；
2. `pathType=eps-fallback` 的 flow0 在 admission fallback 和 WECMP binding 场景中均不再经过 OCS 0-3 link；
3. WECMP binding 场景中，flow0 的真实路径与 `frozenSpine=1` 一致；
4. Patch 1 / Patch 2 的 CSV 字段仍保持兼容；
5. `overallResultConsistency` 和 `overallAlgorithmInvariant` 均通过。

Patch 3 仍应只实现 per-link utilization time series 导出，不应在同一轮把 sampled utilization 接入 WECMP。WECMP measured utilization integration 应作为后续独立补丁。

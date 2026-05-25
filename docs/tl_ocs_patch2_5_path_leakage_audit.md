# TL-OCS Patch 2.5 Path Leakage Audit

## 1. 诊断目的

本轮只诊断 Patch 2 暴露出的 fallback path leakage 风险，不修复源码，不修改 TL-OCS 控制算法、OCS admission、EPS-WECMP、路由逻辑、拓扑或 BulkSend 流量生成逻辑。

诊断重点是：当 flow 0 被标记为 `eps-fallback` 时，NS-3 IP 数据面是否仍然把 0-3 流量转发到 selected OCS link。

## 2. 背景现象

Patch 2 中的异常现象：

| 场景 | flow 0 控制面标记 | OCS 0-3 a-to-b txBytes |
|---|---|---:|
| `patch2-ocs-hit` | `pathType=ocs` | 577320 |
| `patch2-admission-fallback` | `pathType=eps-fallback` | 577320 |
| `patch2-wecmp-binding` | `pathType=eps-fallback`, `frozenSpine=1` | 577320 |

这说明 `pathType` / admission fallback 可能只是控制面标记，真实数据面仍经过 OCS。

## 3. 本轮文件修改范围

本轮只新增本文档：

| 文件 | 说明 |
|---|---|
| `docs/tl_ocs_patch2_5_path_leakage_audit.md` | Patch 2.5 诊断记录。 |

未修改源码。当前工作区中 `src/main/hybrid-dcn-main.cc` 仍显示为 modified，是 Patch 2 已有源码补丁，不是本轮新增修改。

## 4. 运行命令

构建：

```bash
mkdir -p /tmp/tl-ocs-patch2-5-leakage/ns3-cwd /tmp/tl-ocs-patch2-5-leakage/sim/results/raw
cd /home/dyn/ns-3.47
./ns3 build
```

OCS hit：

```bash
cd /tmp/tl-ocs-patch2-5-leakage/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2.5-ocs-hit --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=false --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-5-leakage" 2>&1 | tee /tmp/tl-ocs-patch2-5-leakage/patch2.5-ocs-hit.log; exit ${PIPESTATUS[0]}
```

Admission fallback：

```bash
cd /tmp/tl-ocs-patch2-5-leakage/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2.5-admission-fallback --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-5-leakage" 2>&1 | tee /tmp/tl-ocs-patch2-5-leakage/patch2.5-admission-fallback.log; exit ${PIPESTATUS[0]}
```

WECMP binding：

```bash
cd /tmp/tl-ocs-patch2-5-leakage/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2.5-wecmp-binding --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=true --ocsAdmissionThreshold=20 --matrixFlowDemand=40 --enableEpsWecmp=true --enableEpsWecmpRouting=true --epsWecmpDiagnosticLoadMode=hot-spine --epsWecmpDiagnosticLoad=50 --epsWecmpDiagnosticHotSpine=0 --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-5-leakage" 2>&1 | tee /tmp/tl-ocs-patch2-5-leakage/patch2.5-wecmp-binding.log; exit ${PIPESTATUS[0]}
```

No OCS link 诊断：

原计划使用 `enableMatrixSelect=true --enableStaticOcs=false`，程序返回：

```text
[HYBRID-DCN][ERROR] enableMatrixSelect=true requires enableStaticOcs=true.
```

因此按最小可运行诊断改为 `enableMatrixSelect=false --enableStaticOcs=false --routeMode=global`：

```bash
cd /tmp/tl-ocs-patch2-5-leakage/ns3-cwd
/home/dyn/ns-3.47/ns3 run "hybrid-dcn-main --simTime=1.5 --experimentName=patch2.5-no-ocs-link --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=true --enableMatrixSelect=false --enableStaticOcs=false --routeMode=global --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableOcsAdmissionControl=false --enableEpsWecmp=false --enableEpsWecmpRouting=false --enableDetailedAlgorithmTrace=true --enableDetailedFlowTrace=true --detailedFlowLogLimit=50 --enableStructuredResultExport=true --structuredResultDir=/tmp/tl-ocs-patch2-5-leakage" 2>&1 | tee /tmp/tl-ocs-patch2-5-leakage/patch2.5-no-ocs-link.log; exit ${PIPESTATUS[0]}
```

前三个主诊断场景返回 0。No OCS link 诊断的第二次运行返回 0，但 `overallResultConsistency=fail`，原因是该场景故意不安装 selected OCS link，违反当前结果一致性检查中的手工场景期望；`overallAlgorithmInvariant=pass`。

## 5. 场景对比结果

summary 摘要：

| 场景 | selectedOcsEdges | ocsCoveredFlowCount | fallbackFlowCount | epsResidualFlowCount | ocsTxBytes | epsTxBytes | ocsByteShare | linkCounterCount | overallResultConsistency | overallAlgorithmInvariant |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| `patch2.5-ocs-hit` | `0-3` | 1 | 0 | 2 | 603892 | 2415568 | 0.2 | 18 | pass | pass |
| `patch2.5-admission-fallback` | `0-3` | 0 | 1 | 3 | 603892 | 2415568 | 0.2 | 18 | pass | pass |
| `patch2.5-wecmp-binding` | `0-3` | 0 | 1 | 3 | 603892 | 2415568 | 0.2 | 18 | pass | pass |
| `patch2.5-no-ocs-link` | `0-3` | 0 | 0 | 3 | 0 | 3623352 | 0 | 16 | fail | pass |

flow / link 对比：

| 场景 | flow0 pathType | flow0 rxBytes | OCS 0-3 a-to-b txBytes | OCS 0-3 b-to-a txBytes | EPS 非零 txBytes 摘要 | 是否疑似 OCS leakage |
|---|---|---:|---:|---:|---|---|
| `patch2.5-ocs-hit` | `ocs` | 524288 | 577320 | 26572 | EPS leaf0-spine0 a-to-b 1154640；spine0-leaf1 577320；spine0-leaf2 577320 | 否，这是预期 OCS 命中。 |
| `patch2.5-admission-fallback` | `eps-fallback` | 524288 | 577320 | 26572 | 与 OCS hit 相同；没有 spine0-leaf3 的 577320 | 是。fallback 标记为 EPS，但 0-3 数据仍走 OCS。 |
| `patch2.5-wecmp-binding` | `eps-fallback` | 524288 | 577320 | 26572 | EPS traffic 切到 spine1，但只有 leaf1/leaf2 receive；没有 spine1-leaf3 的 577320 | 是。WECMP 绑定 0-3 到 spine1，但 0-3 数据仍走 OCS。 |
| `patch2.5-no-ocs-link` | flow2 为 `eps-residual` | 524288 | 0 | 0 | EPS leaf0-spine0 a-to-b 1731960；spine0-leaf1/2/3 各 577320 | 否。无 OCS link 时 0-3 走 EPS。 |

关键判断：

1. `577320` 明显大于少量 ARP/TCP ACK/控制包，且与一个 524288-byte BulkSend flow 的方向级数据包 Tx 规模一致。
2. fallback 和 WECMP 场景中 OCS `a-to-b=577320`、`b-to-a=26572` 与 OCS hit 场景完全一致。
3. no-OCS 场景中 0-3 flow 出现在 EPS `spine0-leaf3=577320`；而 fallback / WECMP 场景中没有 leaf3 的 EPS receive 方向大流量。
4. 因此 flow 0 在 fallback 和 WECMP 场景中真实经过 OCS link。

## 6. 路由与源码分析

OCS link 安装：

| 位置 | 代码行为 | 影响 |
|---|---|---|
| `src/main/hybrid-dcn-main.cc:360-379` | `AddOcsLink` 创建 leaf-to-leaf PointToPoint link，并立即 `ipv4.Assign(ocsDevices)`。 | OCS link 是真实 NS-3 IP link。 |
| `src/main/hybrid-dcn-main.cc:3334-3387` | selected OCS edge 被安装，OCS NetDevice 注册 `OcsTxTrace` 和 per-link `LinkTxTrace`。 | Patch 2 counter 绑定在真实 OCS NetDevice 上。 |
| `src/main/hybrid-dcn-main.cc:3417` | `Ipv4GlobalRoutingHelper::PopulateRoutingTables()` 在 OCS link 安装之后调用。 | OCS link 进入 global routing 候选路径。 |

OCS host route：

| 位置 | 代码行为 | 影响 |
|---|---|---|
| `src/main/hybrid-dcn-main.cc:3688-3764` | `routeMode=ocs-forced` 时安装 OCS pair host routes。 | 通过 static host route 强制 selected pair 使用 OCS。 |
| `src/main/hybrid-dcn-main.cc:3692-3715` | `shouldSkipOcsHostRouteForEpsResidual` 试图跳过 residual/fallback flow 对应 server pair。 | 只判断 `srcLeaf/dstLeaf/srcServer/dstServer`，但后续 route 安装不是 flow/port 粒度。 |
| `src/main/hybrid-dcn-main.cc:3723-3753` | 对每个 serverOffsetA/serverOffsetB 安装 host routes；leaf 侧 `AddHostRouteTo(dstB, link.leafBAddress, link.leafAIfIndex)` 只按目的 host，不按源 host 或 TCP port。 | 即使跳过 server0->server0，其他 serverOffset 迭代仍会给同一个 `dstB=server-l3-s0` 安装 leaf0 到 OCS 的 host route。 |

fallback / residual route：

| 位置 | 代码行为 | 影响 |
|---|---|---|
| `src/main/hybrid-dcn-main.cc:3556-3582` | admission fallback flow 设为 `ocsCovered=false`、`epsFallback=true`、`requiresEpsResidualPath=true`、`residualPathReason=admission-fallback`。 | 这是控制面/导出标记。 |
| `src/main/hybrid-dcn-main.cc:3766-3832` | 只有 `enableEpsWecmpRouting && enableMatrixFlows && enableEpsWecmp` 时，才为 residual flow 安装 EPS-WECMP host route。 | 非 WECMP fallback 场景没有显式 EPS fallback host route。 |
| `src/main/hybrid-dcn-main.cc:3799-3811` | WECMP route binding 为 src leaf、dst leaf、selected spine 安装 host route。 | 对 0-1/0-2 生效；对 0-3 被既有 OCS host route 或 global route 抢占，实际未把 0-3 数据流导向 spine1。 |
| `src/main/hybrid-dcn-main.cc:3936-3963` | matrix flow 使用 `BulkSendHelper` 连接目的 server IP 和 port。 | IP 路由按目的 IP 转发，不能按 TCP port 区分同一 src/dst leaf pair 的 OCS 与 EPS flow。 |

Patch 2 LinkCounter 绑定：

| 位置 | 代码行为 | 判断 |
|---|---|---|
| `src/main/hybrid-dcn-main.cc:3272-3309` | EPS leaf-spine 两个 NetDevice 分别注册 per-link counter。 | WECMP 场景中 EPS 非零 counters 从 spine0 切到 spine1，说明 EPS counter 绑定有效。 |
| `src/main/hybrid-dcn-main.cc:3344-3377` | OCS 两个 NetDevice 分别注册 per-link counter。 | no-OCS 场景没有 OCS counter；有 OCS 场景 OCS counter 与全局 `ocsTxBytes` 一致，误绑定可能性低。 |

原因分析表：

| 可能原因 | 源码依据 | 是否成立 | 说明 |
|---|---|---|---|
| Global routing 仍使用 OCS link | `AddOcsLink` 分配 IP 后，`PopulateRoutingTables()` 在第 3417 行调用 | 部分成立 | OCS link 进入 global routing。即使不安装 OCS host route，global routing 也可能选择 OCS。 |
| 只跳过 OCS host route，但未安装 EPS fallback host route | 非 WECMP fallback 没有对应 EPS route 安装；WECMP route 只在第 3766 行条件成立时安装 | 成立 | `patch2.5-admission-fallback` 中 `WECMP-ROUTE bindings=0`，flow0 只能依赖已有 static/global route。 |
| static route 优先级或重复 host route 问题 | OCS host route 安装早于 WECMP route；同一目的 host 可被多个 serverOffset 迭代重复安装 | 成立 | fallback 场景日志显示 `ocsPairHostRoutes=12`、`ocsPairHostRoutesSkippedForEpsResidual=4`，说明仍有大量 OCS host route 存在。 |
| flow 粒度和 IP host route 粒度不一致 | matrix flow 按目的 IP 建立 TCP 连接；route 不能按 TCP port 区分 | 成立 | 同一 src/dst host pair 的不同 flow 无法在 IP host route 上同时一个走 OCS、一个走 EPS。 |
| OCS counter 只是 ACK/控制包 | OCS a-to-b 为 577320，b-to-a 为 26572 | 不成立 | `577320` 接近 524288-byte BulkSend 数据流加协议开销；ACK 方向才是 26572。 |
| LinkCounter 误绑定到错误 NetDevice | OCS/EPS callback 分别绑定在安装出的对应 NetDevice；no-OCS 场景无 OCS counter | 不成立 | WECMP 场景 EPS counter 切到 spine1，说明 direction counter 能反映真实 NetDevice Tx。 |

## 7. 是否存在 OCS leakage

结论：存在。

具体回答：

1. 在 admission fallback 场景中，flow 0 真实经过 OCS link。证据是 flow0 `pathType=eps-fallback`、`rxBytes=524288`，同时 OCS 0-3 `a-to-b txBytes=577320`，EPS 没有 leaf3 接收方向的对应大流量。
2. OCS `a-to-b txBytes=577320` 主要对应 flow 0 的数据包，不是少量 ARP/TCP ACK/控制包。ACK/反向流量规模对应 `b-to-a txBytes=26572`。
3. `ocsPairHostRoutesSkippedForEpsResidual=4` 不足以阻止数据面使用 OCS。日志中 fallback 场景仍有 `ocsPairHostRoutes=12`，且这些 leaf 侧 host route 是目的 host 粒度，不是 flow 粒度。
4. `routeMode=ocs-forced` 下 fallback flow 仍可能被 static/global routing 选到 OCS link。当前复现实验已经发生。
5. 当前代码对非 WECMP fallback 只跳过部分 OCS host route，没有显式安装 EPS fallback host route；WECMP 场景虽然安装 binding，但对 0-3 fallback flow 仍被已有 OCS route 抢占。
6. 最小修复方向应先保证 fallback/residual pair 的 IP 数据面不会使用 OCS link，再做 link_timeseries 或论文级利用率。

## 8. 对前序结论的影响

| 前序结论 | 是否受影响 | 说明 |
|---|---|---|
| TL-OCS 控制面公式验证 | 不影响 | W/A/d/M/P/B/G/selected edge 属于控制面公式，Patch 2.5 问题发生在 IP 数据面路由。 |
| OCS link 安装验证 | 不影响 | selected OCS edge 确实安装成 NS-3 PointToPoint link。 |
| admission fallback “真实走 EPS”的结论 | 严重影响 | 该结论应撤回或修正；当前 evidence 显示 `eps-fallback` flow 0 实际走 OCS。 |
| Patch 1 FCT 指标 | 影响 | `pathType=eps-fallback` 的 flow 可能测到的是 OCS path FCT，因此不能按 EPS fallback 性能解释。 |
| Patch 2 per-link counter 正确性 | 不影响 counter 本身，反而暴露问题 | counter 正确记录了真实 NetDevice Tx，才发现 path leakage。 |
| 后续论文实验 | 严重影响 | 在修复前不能使用当前数据面输出支撑 OCS hit ratio、EPS fallback ratio、per-link utilization、WECMP 效果或论文级 FCT 对比。 |

## 9. 最小修复建议

| 方案 | 修改范围 | 优点 | 风险 | 是否推荐 |
|---|---|---|---|---|
| 对 fallback / residual host 显式安装 EPS host route，并确保删除或覆盖冲突 OCS host route | `routeMode=ocs-forced` host route 安装区域、WECMP route binding 区域 | 最小化保持当前 IP 架构；直接修复 fallback 数据面路径 | 需要处理 ns-3 static route 优先级和重复 host route；同一目的 host 多 flow 仍无法按 port 分流 | 推荐作为 Patch 2.6 的第一步 |
| 将 OCS link 从 global routing 中隔离，只通过显式 OCS host route 使用 | OCS link 安装或 global routing 配置区域 | 避免 global routing 自动把 fallback 流量吸到 OCS | 需要确认 ns-3 中如何干净地隐藏 OCS NetDevice 或调整 metric；可能影响已有 OCS hit 路由 | 推荐作为 Patch 2.6 或 2.7 方案 |
| 明确限制当前模型为 host-pair 粒度，不支持同一 src/dst host pair 同时 OCS/EPS flow 级分流 | 文档 + flow 生成/route 分配策略 | 工程语义清晰，避免虚假 flow-level pathType | 会降低实验灵活性；需要调整 matrix flow 的 src/dst server 分配以避免路由冲突 | 作为必要约束记录，修复后仍建议保留 |

Patch 2.6 的最小验收标准应包括：

1. admission fallback 场景中 flow0 `pathType=eps-fallback` 时，OCS 0-3 `a-to-b txBytes` 不再承载 524288-byte flow 规模的数据。
2. EPS links 中出现 leaf3 接收方向大流量，例如 `spineX-leaf3 txBytes` 约 577320。
3. WECMP binding 场景中 flow0 `frozenSpine=1` 时，0-3 数据流出现在 `eps-leaf0-spine1` 和 `spine1-leaf3`。
4. OCS hit 场景仍保持 flow0 走 OCS。

## 10. 是否可以进入 Patch 3

不建议进入 Patch 3。

当前已经确认存在 fallback path leakage。继续做 `link_timeseries.csv` 会把错误路径的时间序列固化下来，无法支撑论文级链路利用率或 WECMP 效果分析。

建议下一步先做 Patch 2.6：最小修复 fallback/residual 的真实 EPS 路由，并增加能够证明 0-3 fallback 数据不再经过 OCS 的验收日志或 CSV 检查。修复通过后再进入 Patch 3。

# Patch 4C later-flow measured WECMP selectedSpine validation

## 1. 目标和边界

Patch 4C 的目标是做一个最小二阶段 proof：early fallback / residual traffic 先产生 runtime measured EPS utilization snapshot，随后只对一个 later new flow 执行 measured-snapshot WECMP decision，并证明该 later flow 的 `selectedSpine` 可以不同于同一时刻的 control-plane baseline。

本补丁不是论文实验，不做大规模流量评估，不做 packet-level rerouting，不改变已建立 flow。Measured utilization 只影响显式开启的 later proof flow。

## 2. 修改范围

修改文件：

- `src/main/hybrid-dcn-main.cc`
- `docs/tl_ocs_patch4c_later_flow_selected_spine_validation.md`

`src/eps/eps-wecmp-state.h` 沿用 Patch 4B 已有字段，本补丁未新增结构体字段。

## 3. 未修改内容

- 未修改 TL-OCS 控制算法。
- 未修改 Louvain、`B_ij`、`G_ij`、OCS candidate selection。
- 未修改 OCS 光路选择。
- 未修改 OCS admission 判定。
- 未修改 EPS fallback / residual 判定主逻辑。
- 未改变 Patch 2.6 route fix 的核心语义。
- 未修改 Patch 3 `link-timeseries.csv` schema。
- 未修改 Patch 4B `measured-wecmp.csv` schema。
- 未删除或重命名任何已有 CSV 字段。
- 未修改拓扑规模、链路速率、链路时延。
- 未做 packet-level rerouting。
- 未对已建立 flow 做重路由。
- 未做多周期动态重路由。
- 未做大规模论文实验。

## 4. 新增参数

新增参数默认都保持旧行为不变：

| 参数 | 默认值 | 语义 |
|---|---:|---|
| `enableMeasuredWecmpLaterFlowProof` | `false` | 显式开启 Patch 4C later flow proof。 |
| `measuredWecmpLaterDecisionTime` | `0.8` | later measured WECMP decision 的仿真时间。 |
| `measuredWecmpLaterFlowStart` | `0.9` | later proof BulkSend 启动时间，必须晚于 decision time。 |
| `measuredWecmpLaterFlowMaxBytes` | `524288` | later proof flow 的 MaxBytes。 |
| `measuredWecmpLaterFlowPort` | `13000` | later proof flow TCP port。 |
| `measuredWecmpLaterSrcLeaf` | `0` | later proof source leaf。 |
| `measuredWecmpLaterDstLeaf` | `3` | later proof destination leaf。 |
| `measuredWecmpLaterSrcServer` | `1` | later proof source server offset。 |
| `measuredWecmpLaterDstServer` | `1` | later proof destination server offset。 |

启用 proof 时会校验：

- 必须同时开启 `enableMatrixFlows=true`、`enableEpsWecmp=true`、`enableEpsWecmpRouting=true`；
- 必须使用 `epsWecmpLoadSource=measured-snapshot`；
- `measuredWecmpLaterDecisionTime < measuredWecmpLaterFlowStart < simTime`；
- later flow host index 不越界；
- later source / destination host 不能相同。

## 5. later flow proof 机制

启用 `enableMeasuredWecmpLaterFlowProof=true` 时，仿真追加一个 `isMeasuredLaterProofFlow=true` 的 matrix flow。默认 host pair 是 leaf 0 server 1 到 leaf 3 server 1，用于避免覆盖已有 warmup flow 的 host route。

该 proof flow：

- 被标记为 EPS residual path；
- 在 OCS host route 安装阶段会触发 Patch 2.6 的 OCS route skip，防止 OCS leakage；
- 不参加 pre-run WECMP residual accumulation；
- 不参加 pre-run WECMP decision；
- 不在 pre-run route fix 阶段安装 EPS route；
- 只在 `measuredWecmpLaterDecisionTime` 的 scheduled callback 中计算 decision 并安装 explicit EPS host route；
- BulkSend 在 `measuredWecmpLaterFlowStart` 启动，因此只影响 later new flow。

## 6. control-plane baseline 修正

Patch 4C 修正了 Patch 4B 中 `controlPlaneSelectedSpine` 的对照语义风险。

对于 later proof decision，代码会在同一 callback 中先运行一个不读取 measured snapshot、且不提交 pair state 的 control-plane baseline decision，然后再运行 measured-snapshot decision。`wecmp.csv` 中的 `controlPlaneSelectedSpine` 记录 baseline 的 selected spine，而不是 measured decision 之后的 selected spine 回填值。

主验证场景结果：

- control-plane baseline `controlPlaneSelectedSpine=1`；
- measured decision `selectedSpine=0`；
- `selectedSpine != controlPlaneSelectedSpine`；
- `measuredWecmpChangedSelectedSpineCount=1`。

## 7. 时序和 route 安装

主 proof 场景使用：

- warmup / early matrix flow start：`matrixFlowStart=0.35`；
- sampling interval：`linkUtilizationSampleInterval=0.1`；
- later measured decision：`measuredWecmpLaterDecisionTime=0.45`；
- later flow start：`measuredWecmpLaterFlowStart=0.55`。

因此 later decision 发生在 0.4s sample 之后、0.5s sample 之前，可以读取 runtime measured snapshot。route 安装发生在 later flow 启动之前。已启动的 early flow 不被重路由。

## 8. CSV 追加字段

### flows.csv

追加字段：

- `isMeasuredLaterProofFlow`
- `measuredLaterDecisionTime`
- `measuredLaterDecisionIndex`
- `measuredLaterSelectedSpine`
- `measuredLaterControlPlaneSelectedSpine`
- `measuredLaterSelectedSpineChanged`
- `measuredLaterHasMeasuredSample`
- `measuredLaterAppliesToLaterFlow`

### wecmp.csv

在 Patch 4B 字段之后追加：

- `controlPlaneSelectedSpine`

later decision 行应满足：

- `appliesToLaterFlow=true`
- `loadSource=measured-snapshot`
- `hasMeasuredSample=true`
- `measuredDecisionRequested=true`
- `measuredDecisionUsed=true`
- `measuredDecisionFallback=false`
- `measuredNoSample=false`
- `selectedSpine != controlPlaneSelectedSpine`

### summary.csv

追加字段：

- `measuredWecmpLaterFlowProofEnabled`
- `measuredWecmpLaterDecisionTime`
- `measuredWecmpLaterFlowStart`
- `measuredWecmpLaterFlowCount`
- `measuredWecmpLaterFlowCompletedCount`
- `measuredWecmpLaterDecisionCount`
- `measuredWecmpLaterChangedSelectedSpineCount`
- `measuredWecmpLaterRouteInstalled`
- `measuredWecmpLaterOcsLeakageDetected`
- `measuredWecmpLaterProofPassed`

`measured-wecmp.csv` 保持 Patch 4B schema，不新增字段。`link-timeseries.csv` 保持 Patch 3 schema，不新增字段。

## 9. 运行环境和输出目录

构建命令：

`/home/dyn/ns-3.47/ns3 build`

小场景输出目录：

`/tmp/tl-ocs-patch4c-later-flow-proof`

为避免相对路径写入历史结果目录，仿真二进制从 `/tmp/tl-ocs-patch4c-later-flow-proof/ns3-cwd` 启动，structured CSV 全部写入上述 `/tmp` 目录。

## 10. 验证场景

所有场景均为 4 leaves、2 spines、1 selected OCS link 的小规模工程验证。

| 场景 | 参数摘要 | 返回码 |
|---|---|---:|
| `patch4c-backward-control-plane` | WECMP binding，`enableMeasuredWecmpLaterFlowProof=false`，`epsWecmpLoadSource=control-plane` | 0 |
| `patch4c-measured-nosample-fallback` | WECMP binding，proof disabled，`epsWecmpLoadSource=measured-snapshot`，fallback control-plane | 0 |
| `patch4c-later-flow-selected-spine-change` | proof enabled，decision 0.45s，later flow 0.55s，measured snapshot mode | 0 |
| `patch4c-later-flow-no-sample-guard` | proof enabled，decision 0.05s，早于首个 sample，fallback control-plane | 0 |
| `patch4c-admission-fallback-route-fix` | admission fallback，WECMP disabled，deterministic spine0 | 0 |

## 11. summary 检查

| 场景 | laterEnabled | laterFlows | laterCompleted | laterDecisions | changed | routeInstalled | ocsLeakage | proofPassed | overall |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| `patch4c-backward-control-plane` | false | 0 | 0 | 0 | 0 | false | false | false | pass / pass |
| `patch4c-measured-nosample-fallback` | false | 0 | 0 | 0 | 0 | false | false | false | pass / pass |
| `patch4c-later-flow-selected-spine-change` | true | 1 | 1 | 1 | 1 | true | false | true | pass / pass |
| `patch4c-later-flow-no-sample-guard` | true | 1 | 1 | 1 | 0 | true | false | false | pass / pass |
| `patch4c-admission-fallback-route-fix` | false | 0 | 0 | 0 | 0 | false | false | false | pass / pass |

Patch 3 `link-timeseries.csv` 回归：每个场景均生成 252 行数据。Patch 4B `measured-wecmp.csv` 回归：每个场景均生成 16 行 EPS directed snapshot 投影。

## 12. selectedSpine change proof

`patch4c-later-flow-selected-spine-change` 的 later proof flow：

- `pathType=eps-fallback`
- `frozenSpine=0`
- `completed=true`
- `rxBytes=524288`
- `measuredLaterSelectedSpine=0`
- `measuredLaterControlPlaneSelectedSpine=1`
- `measuredLaterSelectedSpineChanged=true`
- `measuredLaterHasMeasuredSample=true`
- `measuredLaterAppliesToLaterFlow=true`

对应 `wecmp.csv` later decision 行：

| row spine | selectedSpine | controlPlaneSelectedSpine | hasMeasuredSample | measuredDecisionUsed | fallback | noSample | measuredPathUtilization | effectivePathLoadMetric |
|---:|---:|---:|---|---|---|---|---:|---:|
| 0 | 0 | 1 | true | true | false | false | 0 | 0 |
| 1 | 0 | 1 | true | true | false | false | 0.00346392 | 0.00346392 |

这证明 measured snapshot 使 later new flow 避开 warmup traffic 压高的 spine1，选择 spine0。early flow 保持 `frozenSpine=1`，没有被重路由。

## 13. no-sample fallback 回归

`patch4c-later-flow-no-sample-guard` 将 later decision time 设为 0.05s，早于首个 0.1s sample。

结果：

- later flow 完成；
- `measuredDecisionFallback=true`；
- `measuredNoSample=true`；
- `measuredDecisionUsed=false`；
- `selectedSpine=1`；
- `controlPlaneSelectedSpine=1`；
- `measuredLaterSelectedSpineChanged=false`；
- `measuredWecmpLaterProofPassed=false`。

该场景验证 no-sample 时不会伪造 measured load，也不会错误声称 proof 成功。

## 14. OCS leakage 和 route fix 回归

`links.csv` 每个场景方向级 counter 数量均为 18：

- EPS leaf-spine：16；
- OCS：2。

关键链路字节：

| 场景 | OCS 0-3 a-to-b txBytes | spine1->leaf3 txBytes | leaf0->spine0 txBytes | spine0->leaf3 txBytes |
|---|---:|---:|---:|---:|
| `patch4c-backward-control-plane` | 0 | 577320 | 0 | 0 |
| `patch4c-measured-nosample-fallback` | 0 | 577320 | 0 | 0 |
| `patch4c-later-flow-selected-spine-change` | 0 | 577320 | 577320 | 577320 |
| `patch4c-later-flow-no-sample-guard` | 0 | 1154640 | 0 | 0 |
| `patch4c-admission-fallback-route-fix` | 0 | 0 | 1731960 | 577320 |

结论：

- WECMP binding 场景中 early flow 仍真实走 frozenSpine=1；
- selectedSpine change proof 中 later flow 走 spine0；
- no-sample guard 中 later flow fallback 到 control-plane spine1；
- admission fallback 仍走 deterministic spine0；
- selected OCS 0-3 a-to-b 对 fallback / later EPS flow 均为 0，没有 OCS leakage 回退。

## 15. backward compatibility

`patch4c-backward-control-plane` 保持 Patch 4B 默认兼容语义：

- `epsWecmpLoadSource=control-plane`
- `measuredWecmpEnabled=false`
- no later proof flow；
- flow0 `pathType=eps-fallback`
- flow0 `frozenSpine=1`
- `overallResultConsistency=pass`
- `overallAlgorithmInvariant=pass`

`patch4c-measured-nosample-fallback` 保持 Patch 4B pre-run measured no-sample fallback 语义：

- measured pre-run decisions 没有 runtime sample；
- fallback/no-sample decision count 为 3；
- selected/frozen spine 与 control-plane 场景一致；
- Patch 3 link-timeseries 和 Patch 4B measured-wecmp 仍生成。

## 16. 结论

Patch 4C 通过。

本补丁已经证明 runtime measured EPS utilization snapshot 可以改变后续新流的 WECMP `selectedSpine`：later proof flow 的 measured selected spine 为 0，control-plane baseline 为 1，且 only later flow 受影响。已建立 early flow 保持 frozenSpine=1，没有被重路由。

Patch 2.6 route fix 保持，fallback / later EPS flow 未泄漏到 selected OCS link。Patch 3 `link-timeseries.csv` 保持可用，Patch 4B `measured-wecmp.csv` 保持可用。

下一阶段可以进入实验脚本 / baseline 设计，但仍不建议直接进入大规模论文实验；应先设计可复现的小规模 baseline 脚本和验收口径。

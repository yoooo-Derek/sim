# TL-OCS 4-ToR 小矩阵算法一致性验证

## 1. 验证目的

本验证只检查 TL-OCS 控制面公式链路是否与论文 V2 主线一致：

`synthetic W(t) -> A(t) -> thresholded A(t) -> A_bar(t) -> d_i -> M -> P_ij -> B_ij -> [B_ij]^+ -> Louvain community -> h(c_i,c_j) -> communityUtility/selectionScore -> greedy selected OCS edge`

本验证不评估论文级平均 FCT、99% FCT、吞吐量、真实链路利用率或真实动态重构。运行中关闭了业务流 `enableMatrixFlows=false`，因此结果校验里的数据面相关项会显示 fail/warn；本轮只采用 algorithm invariant、详细 trace 和 OCS candidates CSV 判断控制面公式。

## 2. 本轮文件修改范围

本轮新增文件：

- `docs/tl_ocs_small_matrix_validation.md`

本轮修改的已有文档：

- `docs/code_map.md`：将 `CommandLine::AddValue` 注册范围校准为 `src/main/hybrid-dcn-main.cc:599-754`。
- `docs/tl_ocs_review_audit.md`：已检查，未发现 CommandLine 注册范围截断到 `matrixFlowDemand` 附近的错误，本轮未修改该文件。

本轮未修改任何源码文件，包括：

- `src/main/hybrid-dcn-main.cc`
- `src/traffic/traffic-matrix.h`
- `src/model/louvain.h`
- `src/ocs/ocs-state.h`
- `src/eps/eps-wecmp-state.h`

本轮未修改 README、PROJECT_CONTEXT、V2.md、脚本、配置、CMake/wscript 或仓库内结果文件。运行日志和 CSV 写入 `/tmp`，避免新增或覆盖 `results/` 下文件。

## 3. 运行环境与命令

README 指出 ns-3 根目录为 `/home/dyn/ns-3.47`，scratch 入口 `/home/dyn/ns-3.47/scratch/hybrid-dcn-main.cc` 是指向 `/home/dyn/sim/src/main/hybrid-dcn-main.cc` 的符号链接。

初始检查发现 `/home/dyn/sim` 下没有 `./ns3`，因此实际构建和运行均在 `/home/dyn/ns-3.47` 执行。构建命令：

```bash
cd /home/dyn/ns-3.47
./ns3 build
```

构建结果：`ninja: no work to do.`，命令返回 0。

skewed 验证命令：

```bash
cd /home/dyn/ns-3.47
./ns3 run "hybrid-dcn-main --simTime=1.0 --experimentName=tl-ocs-small-matrix-validation-skewed --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=false --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=skewed --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableDetailedAlgorithmTrace=true --detailedCandidateLogLimit=20 --enableStructuredResultExport=true --structuredResultDir=/tmp"
```

输出：

- 日志：`/tmp/tl-ocs-small-matrix-validation-skewed.log`
- summary CSV：`/tmp/tl-ocs-small-matrix-validation-skewed-summary.csv`
- OCS candidates CSV：`/tmp/tl-ocs-small-matrix-validation-skewed-ocs-candidates.csv`

clustered 验证命令：

```bash
cd /home/dyn/ns-3.47
./ns3 run "hybrid-dcn-main --simTime=1.0 --experimentName=tl-ocs-small-matrix-validation-clustered --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=false --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=clustered --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableDetailedAlgorithmTrace=true --detailedCandidateLogLimit=20 --enableStructuredResultExport=true --structuredResultDir=/tmp"
```

输出：

- 日志：`/tmp/tl-ocs-small-matrix-validation-clustered.log`
- summary CSV：`/tmp/tl-ocs-small-matrix-validation-clustered-summary.csv`
- OCS candidates CSV：`/tmp/tl-ocs-small-matrix-validation-clustered-ocs-candidates.csv`

uniform 验证命令：

```bash
cd /home/dyn/ns-3.47
./ns3 run "hybrid-dcn-main --simTime=1.0 --experimentName=tl-ocs-small-matrix-validation-uniform --numLeaves=4 --numSpines=2 --serversPerLeaf=2 --enableEcho=false --enableBulk=false --enableSecondBulk=false --enableResidualBulk=false --enableMatrixFlows=false --enableMatrixSelect=true --enableStaticOcs=true --routeMode=ocs-forced --trafficMatrixMode=uniform --communityMode=louvain --louvainMode=single-level --selectionMetric=community-excess --eta=1.0 --communityAlpha=0.5 --ocsPortK=1 --maxSelectedOcsLinks=1 --enableStateHolding=false --enableConfigUpdateGate=false --enableHoldTimeGate=false --enableDetailedAlgorithmTrace=true --detailedCandidateLogLimit=20 --enableStructuredResultExport=true --structuredResultDir=/tmp"
```

输出：

- 日志：`/tmp/tl-ocs-small-matrix-validation-uniform.log`
- summary CSV：`/tmp/tl-ocs-small-matrix-validation-uniform-summary.csv`
- OCS candidates CSV：`/tmp/tl-ocs-small-matrix-validation-uniform-ocs-candidates.csv`

三条场景命令均返回 0。三条 summary CSV 的 `overallAlgorithmInvariant` 均为 `pass`。由于 `enableMatrixFlows=false`，summary CSV 的 `overallResultConsistency` 为 `fail`，这是数据面结果校验不满足，不影响本轮控制面公式验证。

## 4. 源码字段与论文公式对应关系

| 论文公式 | 源码字段/日志字段 | CSV 字段 | 备注 |
|---|---|---|---|
| 原始 directed `W(t)` | `buildSyntheticDirectedTrafficMatrix()`；日志 `rawMatrixSemantic = synthetic-directed-W-derived-A` | 无完整 directed W CSV | 当前 directed W 由 synthetic undirected 矩阵二等分得到，即 `W_ij=W_ji=A_ij/2` |
| `A_ij=W_ij+W_ji` | `buildUndirectedCommunicationIntensityMatrix()`；日志 `matrixSemantic = directed-W-to-undirected-A`、`traffic[...]` | OCS CSV `traffic` | `enableEwmaSmoothing=false` 且 `trafficGraphThreshold=0`，所以控制矩阵等于 A |
| `d_i=sum_j A_ij` | 日志 `[TRACE] nodeDegree[i]` | 无 | 详细 trace 输出所有 leaf 的 degree |
| `M=sum_{i<j} A_ij` | 日志 `totalTraffic`、`rawTotalTraffic`、`controlTotalTraffic` | 无 | 源码 `computeTotalTraffic()` 对全矩阵求和后乘 0.5 |
| `P_ij=d_i d_j/(2M)` | 日志 `[TRACE] expectedTraffic[i][j]` | OCS CSV `expected` | `eta=1.0` |
| `B_ij=A_ij-eta P_ij` | 日志 `[TRACE] modularityGain[i][j]` | OCS CSV `modularityGain` | 本轮 `eta=1.0` |
| `[B_ij]^+` | 日志 `[TRACE] utility[i][j]` 在本轮等于 community-excess utility；OCS CSV `utility` | OCS CSV `utility` | 对跨社区且 B 为正时，`utility` 已包含 communityFactor；本轮正 B 均为同社区，二者相同 |
| community label | 日志 `communityLabelVector`、`leaf-X community` | summary CSV `activeCommunityCount` | `communityMode=louvain`、`louvainMode=single-level` |
| `h(c_i,c_j)` | 日志 `[TRACE] communityFactor[i][j]` | OCS CSV `communityFactor` | 同社区为 1，跨社区为 `communityAlpha=0.5` |
| `communityUtility=[B]^+ h` | 日志 `selectionScore[i][j]`，本轮 state holding 关闭 | OCS CSV `selectionScore` | `enableStateHolding=false`，所以 `selectionScore=communityUtility` |
| candidate 排序 | 日志 `candidateSortRule`、`sortedCandidate[...]` | OCS CSV `candidateIndex` 顺序 | 规则为 selectionScore 降序，平分时 leafA、leafB 升序 |
| selected OCS edge | 日志 `replaySelectedEdgeSet`、`selectedEdge[...]` | summary CSV `selectedOcsEdges`，OCS CSV `selected` | 本轮 `maxSelectedOcsLinks=1` |
| OCS port constraint | 日志/invariant `ocsPortConstraintCheck = pass` | 无 | 本轮 `ocsPortK=1` |

## 5. skewed 场景复核

### W/A 语义

`skewed` synthetic undirected A 的 4-ToR pair 为：

- `A_03=100`
- `A_12=30`
- 其他 pair 为 `10`

directed W 由源码二等分得到：`W_03=W_30=50`，`W_12=W_21=15`，其他 pair 两个方向各为 `5`。日志确认 `rawTotalTraffic=170`、`controlTotalTraffic=170`、`traffic[0][3]=100`、`traffic[1][2]=30`。

节点度手算：

- `d_0=10+10+100=120`
- `d_1=10+30+10=50`
- `d_2=10+30+10=50`
- `d_3=100+10+10=120`
- `M=100+30+4*10=170`

日志输出：`nodeDegree=[120,50,50,120]`，`totalTraffic=170`。

### pair 级复核表

| pair | A_ij | d_i | d_j | M | P_ij | B_ij | B_positive | community_i | community_j | communityFactor | communityUtility | selectionScore | 是否候选 | 是否被选中 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| 0-1 | 10 | 120 | 50 | 170 | 17.6471 | -7.6471 | 0 | 0 | 1 | 0.5 | 0 | 0 | 否 | 否 |
| 0-2 | 10 | 120 | 50 | 170 | 17.6471 | -7.6471 | 0 | 0 | 1 | 0.5 | 0 | 0 | 否 | 否 |
| 0-3 | 100 | 120 | 120 | 170 | 42.3529 | 57.6471 | 57.6471 | 0 | 0 | 1 | 57.6471 | 57.6471 | 是 | 是 |
| 1-2 | 30 | 50 | 50 | 170 | 7.3529 | 22.6471 | 22.6471 | 1 | 1 | 1 | 22.6471 | 22.6471 | 是 | 否，`max-selected-links` |
| 1-3 | 10 | 50 | 120 | 170 | 17.6471 | -7.6471 | 0 | 1 | 0 | 0.5 | 0 | 0 | 否 | 否 |
| 2-3 | 10 | 50 | 120 | 170 | 17.6471 | -7.6471 | 0 | 1 | 0 | 0.5 | 0 | 0 | 否 | 否 |

### selected edge 与结论

日志/CSV 中候选排序：

1. `0-3`，`selectionScore=57.6471`，`selected=true`
2. `1-2`，`selectionScore=22.6471`，`selected=false`，`rejectReason=max-selected-links`

结论：

- `d_i`、`M`、`P_ij`、`B_ij` 与手算一致。
- Louvain label 为 `0:0,1:1,2:1,3:0`，将强边 `0-3` 和 `1-2` 分到各自社区，合理。
- `communityFactor` 同社区为 1，跨社区为 0.5，符合 `h(c_i,c_j)`。
- selected edge 为最高 `selectionScore` 的 `0-3`，且 `ocsPortK=1` invariant 为 pass。
- 未发现控制面公式不一致。

## 6. clustered 场景复核

### W/A 语义

`clustered` synthetic undirected A 的 4-ToR pair 为：

- 同 cluster：`A_01=80`、`A_23=80`
- 跨 cluster：`A_02=A_03=A_12=A_13=10`

directed W 由源码二等分得到：同 cluster 两个方向各 `40`，跨 cluster 两个方向各 `5`。日志确认 `rawTotalTraffic=200`、`controlTotalTraffic=200`、`traffic[0][1]=80`、`traffic[2][3]=80`。

节点度手算：

- `d_0=d_1=d_2=d_3=100`
- `M=80+80+4*10=200`

日志输出：`nodeDegree=[100,100,100,100]`，`totalTraffic=200`。

### pair 级复核表

| pair | A_ij | d_i | d_j | M | P_ij | B_ij | B_positive | community_i | community_j | communityFactor | communityUtility | selectionScore | 是否候选 | 是否被选中 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| 0-1 | 80 | 100 | 100 | 200 | 25 | 55 | 55 | 0 | 0 | 1 | 55 | 55 | 是 | 是 |
| 0-2 | 10 | 100 | 100 | 200 | 25 | -15 | 0 | 0 | 1 | 0.5 | 0 | 0 | 否 | 否 |
| 0-3 | 10 | 100 | 100 | 200 | 25 | -15 | 0 | 0 | 1 | 0.5 | 0 | 0 | 否 | 否 |
| 1-2 | 10 | 100 | 100 | 200 | 25 | -15 | 0 | 0 | 1 | 0.5 | 0 | 0 | 否 | 否 |
| 1-3 | 10 | 100 | 100 | 200 | 25 | -15 | 0 | 0 | 1 | 0.5 | 0 | 0 | 否 | 否 |
| 2-3 | 80 | 100 | 100 | 200 | 25 | 55 | 55 | 1 | 1 | 1 | 55 | 55 | 是 | 否，`max-selected-links` |

### selected edge 与结论

日志/CSV 中候选排序：

1. `0-1`，`selectionScore=55`，`selected=true`
2. `2-3`，`selectionScore=55`，`selected=false`，`rejectReason=max-selected-links`

结论：

- `d_i`、`M`、`P_ij`、`B_ij` 与手算一致。
- Louvain label 为 `0:0,1:0,2:1,3:1`，与 clustered synthetic 矩阵一致。
- `0-1` 与 `2-3` 同分；源码排序规则为 selectionScore 降序后按 `leafA`、`leafB` 升序，所以 `0-1` 在 `2-3` 前被选中。
- selected edge 满足 `ocsPortK=1`，algorithm invariant 为 pass。
- 未发现控制面公式不一致。

## 7. uniform 场景复核

### W/A 语义

`uniform` synthetic undirected A 的所有 4-ToR pair 均为 `20`。directed W 由源码二等分得到：每个方向为 `10`。日志确认 `rawTotalTraffic=120`、`controlTotalTraffic=120`、`traffic[0][1]=20`、`traffic[0][3]=20`、`traffic[1][2]=20`、`traffic[2][3]=20`。

节点度手算：

- `d_0=d_1=d_2=d_3=60`
- `M=6*20=120`

日志输出：`nodeDegree=[60,60,60,60]`，`totalTraffic=120`。

### pair 级复核表

| pair | A_ij | d_i | d_j | M | P_ij | B_ij | B_positive | community_i | community_j | communityFactor | communityUtility | selectionScore | 是否候选 | 是否被选中 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| 0-1 | 20 | 60 | 60 | 120 | 15 | 5 | 5 | 0 | 0 | 1 | 5 | 5 | 是 | 是 |
| 0-2 | 20 | 60 | 60 | 120 | 15 | 5 | 5 | 0 | 0 | 1 | 5 | 5 | 是 | 否，`max-selected-links` |
| 0-3 | 20 | 60 | 60 | 120 | 15 | 5 | 5 | 0 | 0 | 1 | 5 | 5 | 是 | 否，`max-selected-links` |
| 1-2 | 20 | 60 | 60 | 120 | 15 | 5 | 5 | 0 | 0 | 1 | 5 | 5 | 是 | 否，`max-selected-links` |
| 1-3 | 20 | 60 | 60 | 120 | 15 | 5 | 5 | 0 | 0 | 1 | 5 | 5 | 是 | 否，`max-selected-links` |
| 2-3 | 20 | 60 | 60 | 120 | 15 | 5 | 5 | 0 | 0 | 1 | 5 | 5 | 是 | 否，`max-selected-links` |

### selected edge 与结论

日志/CSV 中候选排序：

1. `0-1`
2. `0-2`
3. `0-3`
4. `1-2`
5. `1-3`
6. `2-3`

六条边 `selectionScore` 均为 5。源码排序规则在同分时按 `leafA`、`leafB` 升序，因此 `0-1` 被选中，其他边因 `maxSelectedOcsLinks=1` 未选。

结论：

- `d_i`、`M`、`P_ij`、`B_ij` 与手算一致。
- Louvain label 为 `0:0,1:0,2:0,3:0`，在完全均匀矩阵上合并为单社区，合理。
- 所有 pair 的 `communityFactor=1`，符合单社区下的 `h(c_i,c_j)`。
- selected edge 为确定性 tie-break 结果 `0-1`，并满足 `ocsPortK=1`。
- 未发现控制面公式不一致。

## 8. 不一致项与风险

| 编号 | 事项 | 影响 | 建议 |
|---|---|---|---|
| I1 | 没有完整 directed W 矩阵日志或 CSV；本轮 W 语义通过源码 `buildSyntheticDirectedTrafficMatrix()` 与 A/2 手算确认 | 不影响本轮 A/B/G/selected 验证，但无法从单独输出文件直接复核每个 `W_ij` | 如后续需要审计 W 输入，可最小化新增 directed W trace 或 CSV 字段 |
| I2 | OCS candidates CSV 只导出 `selectionScore>0` 的 candidate，不包含 B<=0 的 pair | 非候选 pair 需要从 detailed trace 复核，不能只看 CSV | 当前 detailed trace 已足够；如要纯 CSV 复核，可后续导出 all-pair matrix CSV |
| I3 | `enableMatrixFlows=false` 导致 `overallResultConsistency=fail` | 这是数据面结果校验失败，不影响控制面 invariant；三场景 `overallAlgorithmInvariant=pass` | 下一阶段如果验证数据面路径，应打开 matrix flows 并另设验收标准 |
| I4 | uniform 场景所有边同分，selected edge 不是唯一数学最优 | 当前源码用确定性 tie-break 选 `0-1`，这不是公式错误 | 论文或实验说明中应说明 tie-break 规则 |

## 9. 是否可以进入下一阶段

- 是否确认 `W -> A -> B -> G -> selected OCS edge` 公式链路基本正确：是。`skewed`、`clustered`、`uniform` 三个 4-ToR 场景的手算表与日志/CSV 一致。
- 是否仍需补日志字段：控制面公式验证不强制需要补字段；若希望只依赖 CSV 复核，需要补 all-pair matrix CSV 和完整 directed W 输出。
- 是否建议进入下一阶段“数据面路径真实性验证”：建议进入。当前控制面公式链路已通过小矩阵验证，下一阶段应专门验证 OCS/EPS 路由、matrix flow 是否按选边和 fallback 规则真实走对应数据面路径。

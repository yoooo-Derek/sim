# TL-OCS 仓库审计与人工 Review 材料

本文档是只读审计结果，用于后续人工 review 和小规模算法一致性验证。本文档没有修改任何源码或算法逻辑。

## 1. 审计范围

已读取的核心论文与说明文档：

- `docs/V2.md`
- `README.md`
- `PROJECT_CONTEXT.md`
- `docs/architecture.md`
- `docs/algorithm_mapping.md`
- `.gitignore`

已读取的源码：

- `src/main/hybrid-dcn-main.cc`
- `src/traffic/traffic-matrix.h`
- `src/model/louvain.h`
- `src/ocs/ocs-state.h`
- `src/eps/eps-wecmp-state.h`
- `src/controller/.gitkeep`
- `src/helper/.gitkeep`
- `src/result/.gitkeep`

已检查的目录状态：

- `scripts/`：当前为空。
- `experiments/configs/`：当前为空。
- `experiments/runs/`：当前为空。
- `build-meta/`：当前无文件。
- `src/utils/`：当前无文件。
- `results/raw/`：包含历史运行日志、结构化 CSV 和 NetAnim XML。
- `results/figures/`、`results/tables/`：仅有 `.gitkeep`。

已检查的结果输出类型：

- 构建日志：`results/raw/build*.log`
- 运行日志：`results/raw/*.log`
- 结构化 CSV：`results/raw/*-summary.csv`、`*-flows.csv`、`*-wecmp.csv`、`*-ocs-candidates.csv`
- NetAnim 输出：`results/raw/hybrid-dcn-anim.xml`

注意：`docs/V2.md` 当前在 git 状态中显示为未跟踪文件，但本审计按论文依据读取。

## 2. 工程结构结论

当前工程不是模块化 ns-3 应用，而是 scratch-compatible 单入口仿真：

- 主入口：`src/main/hybrid-dcn-main.cc:354` 的 `main()`。
- 真实 ns-3 拓扑、路由、应用、结果导出、校验和大部分控制逻辑仍在 `main()` 内。
- 头文件只承担部分纯数据结构和纯算法辅助：
  - `src/traffic/traffic-matrix.h`：流量矩阵辅助。
  - `src/model/louvain.h`：Louvain 辅助。
  - `src/ocs/ocs-state.h`：OCS 候选边和保持周期状态。
  - `src/eps/eps-wecmp-state.h`：EPS-WECMP 数据结构。

`src/controller/`、`src/helper/`、`src/result/` 是预留目录，当前没有实现逻辑。

## 3. 论文公式到代码与输出字段映射

| 论文模块 | 代码位置 | 关键参数/字段 | 输出字段 | 当前状态 |
|---|---|---|---|---|
| 原始有向流量矩阵 `W(t)` | `buildSyntheticDirectedTrafficMatrix()`，`src/traffic/traffic-matrix.h:68`；调用点 `src/main/hybrid-dcn-main.cc:1510`、`2596` | `trafficMatrixMode`、`trafficMatrixSource` | `[MATRIX] rawMatrixSemantic`、CSV `rawDemand` | 仅支持 synthetic，不支持外部 trace 或 ns-3 实测矩阵 |
| 无向通信强度 `A_ij=w_ij+w_ji` | `buildUndirectedCommunicationIntensityMatrix()`，`src/traffic/traffic-matrix.h:87`；调用点 `src/main/hybrid-dcn-main.cc:1512`、`2598` | `rawTrafficMatrix`、`currentA` | `[MATRIX] matrixSemantic`、`rawTraffic[0][3]` | 已实现；输入仍是合成流量 |
| 流量图稀疏化 `theta_f` | `applyTrafficGraphThreshold()`，`src/traffic/traffic-matrix.h:112`；调用点 `src/main/hybrid-dcn-main.cc:1514`、`2600` | `trafficGraphThreshold` | `[MATRIX] trafficGraphThreshold`、CSV summary `trafficGraphThreshold`（当前代码） | 已实现；历史 CSV 有字段版本不一致 |
| EWMA `A_bar` | `updateEwmaMatrix()`，`src/traffic/traffic-matrix.h:134`；调用点 `src/main/hybrid-dcn-main.cc:1516`、`2602` | `enableEwmaSmoothing`、`ewmaBeta` | `[MATRIX] matrixUsedForControl`、`controlTraffic[...]` | 单周期无历史时直接等于当前矩阵；多周期才有真实 EWMA 历史 |
| 节点吞吐度 `d_i` | `computeNodeDegree()`，`src/traffic/traffic-matrix.h:163`；调用点 `src/main/hybrid-dcn-main.cc:1528`、`2258` | `nodeDegree`、`epochDegree` | `[MATRIX] degree[0]`、`degree[3]`、`[TRACE] nodeDegree[i]` | 已实现 |
| 有效总流量 `M` | `computeTotalTraffic()`，`src/traffic/traffic-matrix.h:177`；调用点 `src/main/hybrid-dcn-main.cc:1529`、`2259` | `totalTraffic`、`epochTotalTraffic` | `[MATRIX] totalTraffic`、`rawTotalTraffic`、`controlTotalTraffic` | 已实现 |
| 随机图零模型 `P_ij=d_i*d_j/(2M)` | `src/main/hybrid-dcn-main.cc:1538-1558`、`2260-2278` | `expectedTraffic`、`epochExpected`、`eta` | `[MATRIX] expected[0][3]`、CSV OCS `expected` | 已实现 |
| 模块度增益 `B_ij=A_bar_ij-eta*P_ij` | `src/main/hybrid-dcn-main.cc:1555`、`2275` | `modularityGain`、`epochGain` | `[MATRIX] B[0][3]`、CSV OCS `modularityGain` | 已实现 |
| `[B_ij]^+` | `src/main/hybrid-dcn-main.cc:1556`、`2276` | `ocsUtility`、`baseUtility` | `[MATRIX] U[0][3]`、`selectedBaseUtility` | 已实现 |
| Louvain 社区划分 | `runSingleLevelLouvain()`、`runMultiLevelLouvain()`，`src/model/louvain.h:259`、`281`；调用点 `src/main/hybrid-dcn-main.cc:1569-1584`、`2291-2305` | `communityMode`、`louvainMode`、`louvainMaxPasses`、`louvainMaxLevels` | `[LOUVAIN] modularityQ`、`[COMMUNITY] leaf-* community` | 有实现；`preview` 模式是按 trafficMatrixMode 的预设标签，不是算法结果 |
| 社区折减 `h(c_i,c_j)` | `makeCandidateEdge`，`src/main/hybrid-dcn-main.cc:1652-1675`；多周期 `2319-2342` | `communityAlpha`、`intraCommunity`、`communityFactor` | `[MATRIX] selectedCommunityFactor`、CSV OCS `communityFactor` | 已实现 |
| 综合光调度增益 `G_ij=[B]^+h` | `src/main/hybrid-dcn-main.cc:1653-1657`、`2320-2323` | `baseUtility`、`communityUtility` | `communityUtility`、`utility` | 已实现；实际排序指标还受 `selectionMetric` 和 state holding 影响 |
| 上一周期保持项 `S_ij=G_ij+lambda*x(t-1)` | `src/main/hybrid-dcn-main.cc:1657-1674`、`2324-2341` | `enableStateHolding`、`stateHoldingLambda`、`selectionScore` | `stateHoldingGain`、`selectionScore` | 已实现为加分项 |
| 端口约束 `sum x_ij <= k` | `src/main/hybrid-dcn-main.cc:1724-1776`、`2377-2487` | `ocsPortK`、`maxSelectedOcsLinks` | `[INVARIANT] ocsPortConstraintCheck` | 已实现贪心近似 |
| 配置更新阈值 | `src/main/hybrid-dcn-main.cc:1842-1895`、`2489-2517` | `enableConfigUpdateGate`、`configUpdateThreshold`、`configScoreMode` | `[CONFIG] decision`、`configScoreImprovement` | 已实现；`paper-objective` 才包含重构惩罚 |
| 最小保持周期 | `src/main/hybrid-dcn-main.cc:1689-1693`、`1727-1761`、`2519-2527` | `enableHoldTimeGate`、`minHoldCycles`、`previousConfigAge` | `[HOLD] hardHoldEdges`、`[HOLD-AGE] edge age` | 已实现为硬保留，先占用端口 |
| NS-3 OCS 物理链路安装 | `AddOcsLink()`，`src/main/hybrid-dcn-main.cc:330`；安装点 `3223-3275` | `enableStaticOcs`、`selectedOcsEdges`、`ocsDataRate`、`ocsDelay` | `[OCS-LINK] installedOcsLinks`、`[RESULT] installedOcsLinks` | 有真实 PointToPoint OCS 链路安装 |
| OCS/EPS 路由选择 | OCS host route `src/main/hybrid-dcn-main.cc:3560-3636`；WECMP route `3638-3704` | `routeMode=ocs-forced`、`enableEpsWecmpRouting` | `[ROUTE] ocsPairHostRoutes`、`[WECMP-ROUTE] bindings` | 有静态路由绑定；不是动态设备级权重下发 |
| OCS 准入 | `applyOcsAdmission`，`src/main/hybrid-dcn-main.cc:3345-3391` | `enableOcsAdmissionControl`、`ocsAdmissionThreshold`、`matrixFlowDemand` | `[ADMISSION] ocsAdmitted`、`plannedResidualDemand`、`realResidualDemand` | 有抽象准入；用矩阵单位，不是按真实链路速率/字节窗口计算 |
| EPS 残余需求 | `computePlannedResidualDemand`，`src/main/hybrid-dcn-main.cc:3337-3343`；矩阵流标记 `3393-3518` | `requiresEpsResidualPath`、`wecmpResidualDemand` | CSV flows `plannedResidualDemand`、`realResidualDemand`、`wecmpResidualDemand` | 已实现控制面残余需求 |
| EPS-WECMP 链路状态 | `accumulateEpsResidualTraffic()`、`updateEpsPhysicalSmoothedUtilization()`，`src/main/hybrid-dcn-main.cc:1979-2007` | `observedTraffic`、`utilization`、`smoothedUtilization` | `[WECMP] observedTraffic`、CSV WECMP `pathLoadMetric` | 控制面估计，不是 NS-3 实测 |
| WECMP 权重更新 | `runEpsWecmpUpdateForPair()`，`src/main/hybrid-dcn-main.cc:2032-2173` | `epsWecmpRho`、`epsWecmpGamma`、`epsWecmpEpsilon`、`epsWecmpKappa`、`epsWecmpMaxDelta` | CSV WECMP `targetProbability`、`updatedProbability` | 算法近似实现；最终数据面只给每个残余流冻结一个 spine |
| 结果指标 | `src/main/hybrid-dcn-main.cc:5549-6135`、`6337-6699` | `rxBytes`、`firstRx`、`lastRx`、`goodputMbps` | `[RESULT]`、CSV summary/flows/wecmp/ocs-candidates | 有小规模工程指标，不足以直接支撑论文完整实验 |

## 4. 当前实现状态判断

已较明确实现的论文算法模块：

- `W(t) -> A(t)` 合成流量矩阵转换。
- `A(t) -> A_bar(t)` 的 EWMA 辅助，尤其在多周期控制中有历史状态。
- `d_i`、`M`、随机图期望值 `P_ij`、模块度增益 `B_ij`。
- Louvain 单层/多层局部移动与折叠图辅助。
- `[B]^+`、社区折减、上一周期保持加分。
- 端口约束下的贪心 OCS 边选择。
- 配置更新门控和最小保持周期。
- 将最终 OCS 边安装为 ns-3 PointToPoint 叶间链路。
- 对残余矩阵流可选安装静态 spine 路由，实现流级路径冻结。

占位、近似或需要谨慎表述的模块：

- 原始流量矩阵不是来自 ns-3 业务观测，而是 `trafficMatrixSource=synthetic` 的内置模式。
- `communityMode=preview` 不是 Louvain，而是按 `trafficMatrixMode` 生成预设社区标签。
- OCS 准入使用 `matrixFlowDemand` 和 `ocsAdmissionThreshold` 的抽象矩阵单位，不是按真实 `100Gbps` 链路容量和测量窗口计算。
- WECMP 的 `observedTraffic` 是控制面估计残余需求，不是 ns-3 per-link byte counter 统计。
- 多周期 WECMP 中 `totalRealResidualDemand` 当前是 planned residual placeholder，不能解释为实测残余负载。
- WECMP 权重没有以设备级加权 ECMP 形式持续下发；当前是为每个残余矩阵流选择一个 spine 并安装静态 host route。
- 历史 `results/raw/*.log` 中部分旧日志仍出现 `source = shared-eps-physical-link-telemetry`，但当前源码 `src/main/hybrid-dcn-main.cc:4172-4177` 明确输出 `control-plane-estimated-residual-load`。历史日志与当前源码语义不完全一致。

## 5. 影响论文实验可信度的主要风险

1. 流量矩阵来源风险

`trafficMatrixSource` 只允许 `synthetic`，校验在 `src/main/hybrid-dcn-main.cc:1219-1224`。因此论文若声称使用真实训练流量或 ns-3 统计得到的 `W(t)`，当前代码不能支撑。

2. WECMP 遥测语义风险

`src/eps/eps-wecmp-state.h:11-13` 和 `49-52` 已注明 `observedTraffic` 不是 ns-3 measured per-link bytes。实际累加逻辑在 `src/main/hybrid-dcn-main.cc:1979-1994`，根据 residualDemand 和当前概率写入 `observedTraffic`。这必须在论文和实验说明中标成控制面估计。

3. OCS 准入容量单位风险

准入判断在 `src/main/hybrid-dcn-main.cc:3361` 使用 `loadBefore + matrixFlowDemand <= ocsAdmissionThreshold`。这里的单位是抽象矩阵需求，不是 `ocsDataRate=100Gbps` 的真实发送速率或字节量。

4. 数据面路径验证粒度风险

OCS 使用 PointToPoint 链路并统计 `MacTx`，位置 `src/main/hybrid-dcn-main.cc:3241-3249`。EPS 也统计 leaf-spine `MacTx`，位置 `3210-3214`。但输出只有 aggregate `ocsTxBytes`、`epsTxBytes`，没有 per-flow path trace 或 per-link utilization 的真实时间序列。

5. FCT 与 tail 指标不足

当前结果有 `firstRx`、`lastRx`、`duration`、`goodputMbps`，见 `src/main/hybrid-dcn-main.cc:6077-6099` 和 CSV flows 字段。但没有直接计算平均 FCT、99% FCT、按流大小分类 FCT，也没有足够多流样本形成 tail 统计。

6. 历史结果版本混杂风险

当前源码结构化 summary CSV 头部包含 `trafficGraphThreshold`，见 `src/main/hybrid-dcn-main.cc:6437-6466`；但已有 `results/raw/*-summary.csv` 头部缺少该字段。这说明历史 CSV 不是完全由当前源码版本生成，不能直接作为当前代码审计依据。

7. 实验配置缺失风险

`experiments/configs/` 和 `experiments/runs/` 当前为空。正式实验组、重复次数、随机种子、流量规模、统计窗口、参数表尚未在仓库中固化。

8. `main.cc` 过大带来的 review 风险

`src/main/hybrid-dcn-main.cc` 共 7287 行，控制逻辑、ns-3 拓扑和结果导出混在一个函数中。当前阶段不要重构，但人工 review 时需要按模块切片，否则容易漏掉路径语义。

## 6. 人工需要重点 Review 的位置

建议按以下顺序 review：

1. 流量矩阵与 EWMA
   - `src/traffic/traffic-matrix.h:68-160`
   - `src/main/hybrid-dcn-main.cc:1510-1524`
   - `src/main/hybrid-dcn-main.cc:2593-2613`

2. `B_ij`、`G_ij` 和候选边分数
   - `src/main/hybrid-dcn-main.cc:1528-1558`
   - `src/main/hybrid-dcn-main.cc:1640-1675`
   - `src/main/hybrid-dcn-main.cc:2258-2278`
   - `src/main/hybrid-dcn-main.cc:2319-2342`

3. Louvain 实现
   - `src/model/louvain.h:121-146`
   - `src/model/louvain.h:148-241`
   - `src/model/louvain.h:243-357`

4. 端口约束、配置门控、保持周期
   - `src/main/hybrid-dcn-main.cc:1689-1897`
   - `src/main/hybrid-dcn-main.cc:2377-2580`
   - `src/ocs/ocs-state.h:78-115`

5. OCS 数据面安装和路由强制
   - `src/main/hybrid-dcn-main.cc:330-350`
   - `src/main/hybrid-dcn-main.cc:3223-3275`
   - `src/main/hybrid-dcn-main.cc:3560-3636`

6. OCS 准入和 EPS fallback
   - `src/main/hybrid-dcn-main.cc:3337-3391`
   - `src/main/hybrid-dcn-main.cc:3393-3518`
   - `src/main/hybrid-dcn-main.cc:3521-3558`

7. WECMP 控制面估计和静态路由绑定
   - `src/main/hybrid-dcn-main.cc:1939-2007`
   - `src/main/hybrid-dcn-main.cc:2032-2173`
   - `src/main/hybrid-dcn-main.cc:3638-3704`
   - `src/eps/eps-wecmp-state.h:8-65`

8. 输出字段和指标解释
   - 日志输出：`src/main/hybrid-dcn-main.cc:4101-6135`
   - CSV 导出：`src/main/hybrid-dcn-main.cc:6337-6699`
   - invariant：`src/main/hybrid-dcn-main.cc:6702-7282`

## 7. 小规模一致性验证建议

当前不建议直接做大规模实验。后续人工确认后，可先做以下小规模验证：

- 固定 `numLeaves=4`，手算 `skewed` 和 `clustered` 的 `A`、`d_i`、`M`、`P_ij`、`B_ij`，与 `[MATRIX]` 和 CSV OCS candidates 对齐。
- 分别运行 `communityMode=preview` 与 `communityMode=louvain`，确认社区标签来源差异。
- 只开启 `enableEpsWecmp=true` 且关闭/打开 `enableEpsWecmpRouting`，区分“控制面权重变化”和“数据面静态路由绑定”。
- 对 `ocsAdmissionThreshold < matrixFlowDemand` 的场景确认 fallback 流是否真的走 EPS。
- 对 `enableEwmaSmoothing=true` 的多周期场景手算第二个 epoch 的 `A_bar`。

## 8. 当前结论

当前仓库已经实现了一条小规模 TL-OCS 控制面主线，并且能把最终 OCS 边安装成 ns-3 PointToPoint 光链路。它也能为部分 EPS 残余矩阵流安装静态 spine 路由。

但当前工程还不能被描述为完整论文级实验平台：流量矩阵、OCS 准入容量、WECMP 链路状态大多仍是控制面抽象或合成估计；结果指标不足以支撑平均 FCT、99% FCT、真实吞吐、真实链路利用率和重构次数等完整论文结论。


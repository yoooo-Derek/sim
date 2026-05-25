# sim 工程代码地图

生成时间：2026-05-25  
范围：仅基于当前仓库只读扫描与源码阅读生成，不包含实验运行结论。  
约束：本文档用于人工 review 和算法一致性准备，不修改任何仿真逻辑。

## 一、仓库总览

### 1. 根目录结构

```text
sim/
  README.md
  PROJECT_CONTEXT.md
  docs/
  src/
    main/
    traffic/
    model/
    ocs/
    eps/
    controller/
    helper/
    result/
    utils/
  experiments/
    configs/
    runs/
  scripts/
  results/
    raw/
    figures/
    tables/
  build-meta/
  .agents/
  .codex/
```

### 2. 一级目录用途

| 路径 | 用途 | 备注 |
|---|---|---|
| `src/` | 当前仿真源码和头文件 | 核心逻辑集中在 `src/main/hybrid-dcn-main.cc`，其余为 header-only 辅助结构和算法函数 |
| `docs/` | 算法、架构、review 文档 | 包含论文 V2 文稿、已有映射文档和本代码地图 |
| `results/` | 历史构建、运行日志、结构化 CSV、图表/表格占位目录 | `results/raw/` 中已有大量历史输出；本次未生成新实验输出 |
| `experiments/` | 实验配置和运行目录占位 | 当前扫描到的 `configs/`、`runs/` 为空目录 |
| `scripts/` | 运行或批处理脚本目录 | 当前扫描为空目录 |
| `build-meta/` | 构建元数据目录 | 当前扫描为空目录 |
| `.agents/` | 本地 agent 相关目录 | 非论文算法代码 |
| `.codex/` | 本地 Codex 相关目录 | 非论文算法代码 |

### 3. ns-3 工程形态判断

当前工程更像“独立仿真工程 / scratch 风格工程”，不是标准 ns-3 模块结构。依据：

- 未扫描到标准 ns-3 模块常见的 `model/`、`helper/`、`test/` 与模块级构建文件组合。
- 仿真入口位于 `src/main/hybrid-dcn-main.cc:354` 的 `main` 函数。
- 工程源码主要通过一个主程序文件组织，辅助头文件放在 `src/traffic/`、`src/model/`、`src/ocs/`、`src/eps/`。

### 4. 主入口文件

主入口文件：`src/main/hybrid-dcn-main.cc`  
`main` 函数位置：`src/main/hybrid-dcn-main.cc:354`

### 5. 代码集中程度

当前代码高度集中在单个大文件中：

- `src/main/hybrid-dcn-main.cc` 承担命令行参数、预设场景、拓扑构建、矩阵控制、OCS 选择、OCS 链路安装、EPS/WECMP 路由、应用流安装、统计输出、CSV 导出和算法 invariant 检查。
- `src/traffic/traffic-matrix.h`、`src/model/louvain.h`、`src/ocs/ocs-state.h`、`src/eps/eps-wecmp-state.h` 提供算法数据结构和 header-only 辅助逻辑。
- 这会增加人工 review 难度，尤其是区分“控制面估计”和“NS-3 数据面真实测量”时需要逐段检查。

## 二、文件清单

| 文件路径 | 文件类型 | 主要用途 | 是否与 TL-OCS 算法相关 | 是否需要人工重点 review |
|---|---|---|---|---|
| `.gitignore` | 配置 | Git 忽略规则 | 否 | 否 |
| `PROJECT_CONTEXT.md` | 文档 | 项目上下文说明 | 间接 | 是 |
| `README.md` | 文档 | 工程说明和可能的运行入口说明 | 间接 | 是 |
| `docs/V2.md` | 文档 | TL-OCS V2 论文/算法文稿 | 是 | 是 |
| `docs/algorithm_mapping.md` | 文档 | 已有算法到代码映射材料 | 是 | 是 |
| `docs/architecture.md` | 文档 | 工程架构说明 | 是 | 是 |
| `docs/tl_ocs_review_audit.md` | 文档 | 已有 TL-OCS review/audit 材料 | 是 | 是 |
| `docs/code_map.md` | 文档 | 本次生成的工程代码地图 | 是 | 是 |
| `results/figures/.gitkeep` | 占位 | 图输出目录占位 | 否 | 否 |
| `results/raw/.gitkeep` | 占位 | 原始输出目录占位 | 否 | 否 |
| `results/raw/admission-fallback-after-matrix-semantics.log` | 结果日志 | OCS admission/fallback 历史运行日志 | 是 | 是 |
| `results/raw/admission-fallback-explicit.log` | 结果日志 | OCS admission/fallback 显式场景历史日志 | 是 | 是 |
| `results/raw/admission-fallback-regression.log` | 结果日志 | OCS admission/fallback 回归日志 | 是 | 是 |
| `results/raw/baseline-excess.log` | 结果日志 | baseline/excess 历史日志 | 是 | 是 |
| `results/raw/build-after-config-objective.log` | 构建日志 | config objective 后构建输出 | 间接 | 否 |
| `results/raw/build-after-ewma.log` | 构建日志 | EWMA 后构建输出 | 间接 | 否 |
| `results/raw/build-after-flow-path-trace.log` | 构建日志 | flow path trace 后构建输出 | 间接 | 否 |
| `results/raw/build-after-louvain-ocs-trace.log` | 构建日志 | Louvain/OCS trace 后构建输出 | 间接 | 否 |
| `results/raw/build-after-matrix-semantics.log` | 构建日志 | matrix semantics 后构建输出 | 间接 | 否 |
| `results/raw/build-after-p0.log` | 构建日志 | P0 后构建输出 | 间接 | 否 |
| `results/raw/build-after-structured-export.log` | 构建日志 | structured export 后构建输出 | 间接 | 否 |
| `results/raw/build-after-wecmp-diagnostics.log` | 构建日志 | WECMP diagnostics 后构建输出 | 间接 | 否 |
| `results/raw/build.log` | 构建日志 | 通用构建输出 | 间接 | 否 |
| `results/raw/community-aware.log` | 结果日志 | community-aware 场景历史日志 | 是 | 是 |
| `results/raw/config-gated.log` | 结果日志 | config update gate 场景历史日志 | 是 | 是 |
| `results/raw/fallback-flow-trace.log` | 结果日志 | fallback flow trace 历史日志 | 是 | 是 |
| `results/raw/fallback-structured-export-flows.csv` | 结果 CSV | flow 级结构化输出 | 是 | 是 |
| `results/raw/fallback-structured-export-ocs-candidates.csv` | 结果 CSV | OCS candidate 结构化输出 | 是 | 是 |
| `results/raw/fallback-structured-export-summary.csv` | 结果 CSV | summary 结构化输出 | 是 | 是 |
| `results/raw/fallback-structured-export-wecmp.csv` | 结果 CSV | WECMP 结构化输出 | 是 | 是 |
| `results/raw/fallback-structured-export.log` | 结果日志 | fallback structured export 历史日志 | 是 | 是 |
| `results/raw/full-control-after-config-objective.log` | 结果日志 | full-control config objective 历史日志 | 是 | 是 |
| `results/raw/full-control-after-ewma.log` | 结果日志 | full-control EWMA 历史日志 | 是 | 是 |
| `results/raw/full-control-after-matrix-semantics.log` | 结果日志 | full-control matrix semantics 历史日志 | 是 | 是 |
| `results/raw/full-control-after-p0.log` | 结果日志 | full-control P0 历史日志 | 是 | 是 |
| `results/raw/full-control-no-export-regression.log` | 结果日志 | full-control no-export 回归日志 | 是 | 是 |
| `results/raw/full-control-wecmp.log` | 结果日志 | full-control WECMP 历史日志 | 是 | 是 |
| `results/raw/full-control.log` | 结果日志 | full-control 历史日志 | 是 | 是 |
| `results/raw/full-stack-control-after-p0.log` | 结果日志 | full-stack-control P0 历史日志 | 是 | 是 |
| `results/raw/full-stack-ewma-after-matrix-semantics.log` | 结果日志 | full-stack EWMA matrix semantics 历史日志 | 是 | 是 |
| `results/raw/full-stack-ewma-paper-objective.log` | 结果日志 | full-stack EWMA paper objective 历史日志 | 是 | 是 |
| `results/raw/full-stack-ewma.log` | 结果日志 | full-stack EWMA 历史日志 | 是 | 是 |
| `results/raw/full-stack-flow-regression.log` | 结果日志 | full-stack flow 回归日志 | 是 | 是 |
| `results/raw/full-stack-no-ewma-after-matrix-semantics.log` | 结果日志 | full-stack no-EWMA matrix semantics 历史日志 | 是 | 是 |
| `results/raw/full-stack-no-ewma.log` | 结果日志 | full-stack no-EWMA 历史日志 | 是 | 是 |
| `results/raw/full-stack-paper-after-trace.log` | 结果日志 | full-stack paper trace 历史日志 | 是 | 是 |
| `results/raw/full-stack-paper-objective.log` | 结果日志 | full-stack paper objective 历史日志 | 是 | 是 |
| `results/raw/full-stack-paper-regression.log` | 结果日志 | full-stack paper 回归日志 | 是 | 是 |
| `results/raw/full-stack-selection-score.log` | 结果日志 | full-stack selection-score 历史日志 | 是 | 是 |
| `results/raw/hold-gated-after-config-objective.log` | 结果日志 | hold gate config objective 历史日志 | 是 | 是 |
| `results/raw/hold-gated-after-ewma.log` | 结果日志 | hold gate EWMA 历史日志 | 是 | 是 |
| `results/raw/hold-gated-after-matrix-semantics.log` | 结果日志 | hold gate matrix semantics 历史日志 | 是 | 是 |
| `results/raw/hold-gated-after-p0.log` | 结果日志 | hold gate P0 历史日志 | 是 | 是 |
| `results/raw/hold-gated.log` | 结果日志 | hold gate 历史日志 | 是 | 是 |
| `results/raw/hybrid-dcn-anim.xml` | 结果文件 | NetAnim XML 输出 | 间接 | 否 |
| `results/raw/preset-wins-override-warning.log` | 结果日志 | preset override warning 历史日志 | 间接 | 是 |
| `results/raw/preset-wins-warning-regression.log` | 结果日志 | preset warning 回归日志 | 间接 | 是 |
| `results/raw/smoke-test.log` | 结果日志 | smoke test 历史日志 | 间接 | 否 |
| `results/raw/state-aware.log` | 结果日志 | state-aware 场景历史日志 | 是 | 是 |
| `results/raw/trace-clustered.log` | 结果日志 | clustered trace 历史日志 | 是 | 是 |
| `results/raw/trace-skewed-flow-compatible.log` | 结果日志 | skewed trace flow-compatible 历史日志 | 是 | 是 |
| `results/raw/trace-skewed.log` | 结果日志 | skewed trace 历史日志 | 是 | 是 |
| `results/raw/trace-uniform.log` | 结果日志 | uniform trace 历史日志 | 是 | 是 |
| `results/raw/wecmp-default-regression.log` | 结果日志 | WECMP 默认回归日志 | 是 | 是 |
| `results/raw/wecmp-hot-spine-after-trace.log` | 结果日志 | WECMP hot-spine trace 历史日志 | 是 | 是 |
| `results/raw/wecmp-hot-spine-diagnostic.log` | 结果日志 | WECMP hot-spine diagnostic 历史日志 | 是 | 是 |
| `results/raw/wecmp-hot-spine-flow-trace.log` | 结果日志 | WECMP hot-spine flow trace 历史日志 | 是 | 是 |
| `results/raw/wecmp-hot-spine-structured-export-flows.csv` | 结果 CSV | WECMP hot-spine flow 输出 | 是 | 是 |
| `results/raw/wecmp-hot-spine-structured-export-ocs-candidates.csv` | 结果 CSV | WECMP hot-spine OCS candidate 输出 | 是 | 是 |
| `results/raw/wecmp-hot-spine-structured-export-summary.csv` | 结果 CSV | WECMP hot-spine summary 输出 | 是 | 是 |
| `results/raw/wecmp-hot-spine-structured-export-wecmp.csv` | 结果 CSV | WECMP hot-spine WECMP 输出 | 是 | 是 |
| `results/raw/wecmp-hot-spine-structured-export.log` | 结果日志 | WECMP hot-spine structured export 历史日志 | 是 | 是 |
| `results/tables/.gitkeep` | 占位 | 表格输出目录占位 | 否 | 否 |
| `src/controller/.gitkeep` | 占位 | controller 目录占位 | 否 | 否 |
| `src/eps/eps-wecmp-state.h` | C++ 头文件 | EPS-WECMP 链路/路径状态、决策、epoch summary 数据结构 | 是 | 是 |
| `src/helper/.gitkeep` | 占位 | helper 目录占位 | 否 | 否 |
| `src/main/hybrid-dcn-main.cc` | C++ 源码 | 主仿真入口；包含大部分控制逻辑、NS-3 拓扑和输出逻辑 | 是 | 是 |
| `src/model/louvain.h` | C++ 头文件 | Louvain 社区发现和模块度计算 | 是 | 是 |
| `src/ocs/ocs-state.h` | C++ 头文件 | OCS candidate edge 和 edge age 状态 | 是 | 是 |
| `src/result/.gitkeep` | 占位 | result 源码目录占位 | 否 | 否 |
| `src/traffic/traffic-matrix.h` | C++ 头文件 | traffic matrix 构造、A 矩阵、阈值、EWMA、degree/M 计算 | 是 | 是 |

补充：`experiments/configs/`、`experiments/runs/`、`scripts/`、`build-meta/`、`src/utils/` 当前扫描为目录存在但未发现文件。

## 三、主入口分析

### 1. main 函数位置

| 项目 | 位置 |
|---|---|
| 主入口文件 | `src/main/hybrid-dcn-main.cc` |
| `main` 函数 | `src/main/hybrid-dcn-main.cc:354` |
| 命令行默认值定义 | `src/main/hybrid-dcn-main.cc:356-447` |
| `CommandLine::AddValue` 注册 | `src/main/hybrid-dcn-main.cc:599-754` |

### 2. 主要结构体

| 结构体 | 位置 | 作用 |
|---|---|---|
| `OcsInstalledLink` | `src/main/hybrid-dcn-main.cc:37` | 记录已安装 OCS leaf pair、接口、IP 和 NetDevice |
| `MatrixBulkFlowSpec` | `src/main/hybrid-dcn-main.cc:50` | 矩阵流应用规格，包含源/目的 leaf、server、端口、OCS 命中和路由标签 |
| `OcsAdmissionEvent` | `src/main/hybrid-dcn-main.cc:79` | OCS admission 控制的单次判定记录 |
| `MatrixBulkFlowStats` | `src/main/hybrid-dcn-main.cc:94` | 矩阵 Bulk 流的观测字节、起止时间、FCT、goodput |
| `ControlEpochSummary` | `src/main/hybrid-dcn-main.cc:126` | 多周期控制 epoch 的矩阵、社区、candidate、selected OCS 信息 |
| `OcsControllerDecision` | `src/main/hybrid-dcn-main.cc:156` | 单周期 OCS 控制器输出 |
| `MatrixEpochSummary` | `src/main/hybrid-dcn-main.cc:185` | traffic matrix epoch 的 W/A/Abar/degree/M 信息 |
| `EpsWecmpRouteBinding` | `src/main/hybrid-dcn-main.cc:200` | WECMP route binding 到源 leaf、目的 leaf、spine 和 flow label |
| `WeightedMatrix` | `src/traffic/traffic-matrix.h:9` | 无向加权矩阵类型 |
| `DirectedTrafficMatrix` | `src/traffic/traffic-matrix.h:10` | 有向流量矩阵类型 |
| `CommunityPreview` | `src/model/louvain.h:12` | 社区预览结果 |
| `LouvainResult` | `src/model/louvain.h:29` | Louvain 输出，包含 community、modularity、level summaries |
| `OcsCandidateEdge` | `src/ocs/ocs-state.h:12` | OCS candidate edge，含 expected/modularity/utility/selected 等字段 |
| `OcsEdgeAgeMatrix` | `src/ocs/ocs-state.h:10` | OCS edge age 二维矩阵 |
| `EpsWecmpDecision` | `src/eps/eps-wecmp-state.h:27` | 单条 EPS 路径的 WECMP weight 更新结果 |

### 3. 主要全局变量

| 全局变量 | 位置 | 作用 |
|---|---|---|
| `g_bulkRxBytes`, `g_bulkSeenFirstRx`, `g_bulkFirstRxTime`, `g_bulkLastRxTime` | `src/main/hybrid-dcn-main.cc:213-216` | 主 Bulk 流接收统计 |
| `g_ocsTxPackets`, `g_ocsTxBytes` | `src/main/hybrid-dcn-main.cc:218-219` | OCS NetDevice MacTx 聚合统计 |
| `g_epsTxPackets`, `g_epsTxBytes` | `src/main/hybrid-dcn-main.cc:221-222` | EPS NetDevice MacTx 聚合统计 |
| `g_residualBulkRxBytes`, `g_residualBulkSeenFirstRx`, `g_residualBulkFirstRxTime`, `g_residualBulkLastRxTime` | `src/main/hybrid-dcn-main.cc:224-227` | residual/fallback Bulk 流接收统计 |
| `g_secondBulkRxBytes`, `g_secondBulkSeenFirstRx`, `g_secondBulkFirstRxTime`, `g_secondBulkLastRxTime` | `src/main/hybrid-dcn-main.cc:228-231` | 第二条 Bulk 流接收统计 |

### 4. 主要辅助函数

| 函数 | 位置 | 作用 |
|---|---|---|
| `FormatIpv4Endpoint` | `src/main/hybrid-dcn-main.cc:231` | 格式化 IPv4 endpoint |
| `BulkSinkRxTrace` | `src/main/hybrid-dcn-main.cc:245` | 主 Bulk sink Rx trace |
| `OcsTxTrace` | `src/main/hybrid-dcn-main.cc:260` | OCS MacTx trace |
| `EpsTxTrace` | `src/main/hybrid-dcn-main.cc:267` | EPS MacTx trace |
| `ResidualBulkSinkRxTrace` | `src/main/hybrid-dcn-main.cc:274` | residual Bulk sink Rx trace |
| `SecondBulkSinkRxTrace` | `src/main/hybrid-dcn-main.cc:289` | second Bulk sink Rx trace |
| `EchoClientTxTrace`, `EchoClientRxTrace`, `EchoServerRxTrace` | `src/main/hybrid-dcn-main.cc:304-322` | Echo 应用 trace |
| `AddOcsLink` | `src/main/hybrid-dcn-main.cc:331` | 使用 `PointToPointHelper` 安装 leaf-to-leaf OCS link |
| `buildSyntheticDirectedTrafficMatrix` | `src/traffic/traffic-matrix.h:69` | 构造 synthetic 有向 W 矩阵 |
| `buildUndirectedCommunicationIntensityMatrix` | `src/traffic/traffic-matrix.h:88` | 由 W 构造无向 A 矩阵 |
| `applyTrafficGraphThreshold` | `src/traffic/traffic-matrix.h:113` | 按 `theta_f` 稀疏化流量图 |
| `updateEwmaMatrix` | `src/traffic/traffic-matrix.h:135` | EWMA 平滑 |
| `computeNodeDegree` | `src/traffic/traffic-matrix.h:164` | 计算节点吞吐度 `d_i` |
| `computeTotalTraffic` | `src/traffic/traffic-matrix.h:178` | 计算有效总流量 `M` |
| `runSingleLevelLouvain`, `runMultiLevelLouvain` | `src/model/louvain.h:260`, `src/model/louvain.h:282` | Louvain 社区发现 |

### 5. 命令行参数

命令行参数在 `src/main/hybrid-dcn-main.cc:599-754` 注册。完整清单见第四节。

### 6. 拓扑构建逻辑

| 逻辑 | 代码位置 | 说明 |
|---|---|---|
| 创建 spine、leaf、server nodes | `src/main/hybrid-dcn-main.cc:3118-3123` | `NodeContainer spines/leaves/servers` |
| 安装 Internet stack | `src/main/hybrid-dcn-main.cc:3129-3132` | 对 spine、leaf、server 安装协议栈 |
| server-leaf P2P helper | `src/main/hybrid-dcn-main.cc:3158-3160` | 默认 `25Gbps`、`1us`，已按当前源码复核 |
| leaf-spine EPS P2P helper | `src/main/hybrid-dcn-main.cc:3162-3164` | 默认 `40Gbps`、`2us`，已按当前源码复核 |
| 安装 server-leaf 链路 | `src/main/hybrid-dcn-main.cc:3190-3203` | 给 server 和 leaf 分配子网 |
| 安装 leaf-spine EPS 链路 | `src/main/hybrid-dcn-main.cc:3206-3221` | 同时接 EPS MacTx trace |

### 7. 流量生成逻辑

| 逻辑 | 代码位置 | 说明 |
|---|---|---|
| synthetic W 矩阵生成 | `src/main/hybrid-dcn-main.cc:1510`, `src/traffic/traffic-matrix.h:69` | 基于 `trafficMatrixMode` 生成 uniform/skewed/clustered 等 synthetic 矩阵 |
| W 到 A | `src/main/hybrid-dcn-main.cc:1512`, `src/traffic/traffic-matrix.h:88` | `A_ij = W_ij + W_ji` |
| 阈值稀疏化 | `src/main/hybrid-dcn-main.cc:1515`, `src/traffic/traffic-matrix.h:113` | 小于 `trafficGraphThreshold` 的边置 0 |
| EWMA | `src/main/hybrid-dcn-main.cc:1518`, `src/traffic/traffic-matrix.h:135` | 受 `enableEwmaSmoothing`、`ewmaBeta` 控制 |
| 多周期矩阵 | `src/main/hybrid-dcn-main.cc:2593-2613` | 为 control epochs 构造 W/A/Abar |
| 应用层 Bulk 流 | `src/main/hybrid-dcn-main.cc:3733-3836` | 安装 PacketSink/BulkSend，包括 matrix flows |

### 8. OCS 链路安装逻辑

| 逻辑 | 代码位置 | 说明 |
|---|---|---|
| OCS link helper | `src/main/hybrid-dcn-main.cc:331-350` | `AddOcsLink` 使用 P2P 设备模拟 leaf-to-leaf OCS 光路 |
| 根据 selected edges 安装 | `src/main/hybrid-dcn-main.cc:3223-3275` | 最终 `edgesToInstall` 被安装为 OCS P2P 链路 |
| OCS route 安装 | `src/main/hybrid-dcn-main.cc:3560-3636` | 对命中 OCS 的 server-to-server pair 写 host route |

注意：当前 OCS 链路看起来是在仿真运行前安装的 P2P link。多周期控制逻辑会更新控制面选择结果，但需要人工重点确认是否存在 NS-3 时间线上动态删除/新增 NetDevice 或动态改路由；从当前主入口结构看，动态重构更像控制面模拟，而非真实运行时链路重构。

### 9. EPS 路由逻辑

| 逻辑 | 代码位置 | 说明 |
|---|---|---|
| 默认 GlobalRouting | `src/main/hybrid-dcn-main.cc:3289` | `Ipv4GlobalRoutingHelper::PopulateRoutingTables()` |
| OCS 命中流 host route | `src/main/hybrid-dcn-main.cc:3560-3636` | 强制 OCS 路径 |
| WECMP static host route | `src/main/hybrid-dcn-main.cc:3638-3704` | 对部分 matrix flow 绑定 spine 并写静态路由 |

### 10. WECMP 逻辑

| 逻辑 | 代码位置 | 说明 |
|---|---|---|
| WECMP 状态结构 | `src/eps/eps-wecmp-state.h:8-57` | 包括 observed/estimated load、weight、delta、epoch summary |
| residual traffic 累加 | `src/main/hybrid-dcn-main.cc:1979-2007` | 根据控制面 residual demand 累加，不是从 NetDevice utilization trace 读入 |
| WECMP 更新公式 | `src/main/hybrid-dcn-main.cc:2032-2173` | 使用 `rho/gamma/epsilon/kappa/maxDelta/capacity` 更新 weight |
| WECMP route binding | `src/main/hybrid-dcn-main.cc:3638-3704` | 将 matrix flow 映射到某个 spine 路径 |

关键判断：WECMP 当前主要基于控制面估计的 residual load，而不是 NS-3 数据面真实链路利用率。

### 11. 统计和输出逻辑

| 输出 | 代码位置 | 说明 |
|---|---|---|
| 控制面和算法日志 | `src/main/hybrid-dcn-main.cc:4101-5548` | 打印参数、矩阵、候选边、WECMP 等 |
| flow/FCT/goodput 输出 | `src/main/hybrid-dcn-main.cc:5549-6135` | 基于 PacketSink Rx 统计计算部分 flow 指标 |
| structured CSV 路径 | `src/main/hybrid-dcn-main.cc:6388-6394` | summary、flows、wecmp、ocs-candidates |
| summary CSV | `src/main/hybrid-dcn-main.cc:6437-6511` | 总体参数和部分指标 |
| flows CSV | `src/main/hybrid-dcn-main.cc:6513-6599` | 每条 matrix flow 的 route/admission/FCT/goodput |
| WECMP CSV | `src/main/hybrid-dcn-main.cc:6601-6652` | WECMP epoch/path 级决策 |
| OCS candidates CSV | `src/main/hybrid-dcn-main.cc:6654-6699` | OCS candidate edge 特征与选择状态 |
| invariant 检查 | `src/main/hybrid-dcn-main.cc:6702-7282` | 算法一致性和结构化结果检查 |

## 四、命令行参数清单

位置说明：`默认值行 / AddValue 行` 均指 `src/main/hybrid-dcn-main.cc`。

| 参数名 | 默认值 | 代码位置 | 含义 | 对应论文概念 | 是否建议用于论文实验 |
|---|---:|---|---|---|---|
| `simTime` | `1.0` | `356 / 599` | 仿真总时长 | 实验运行时间 | 可用，但需统一固定 |
| `experimentName` | `"stage-1-topology"` | `357 / 600` | 实验名称和输出标识 | 实验元数据 | 建议使用 |
| `presetScenario` | `"manual"` | `358 / 601` | 预设场景 | baseline/算法配置 | 可用，但需记录 override 规则 |
| `presetOverrideMode` | `"preset-wins"` | `359 / 602` | preset 与显式参数冲突处理 | 实验配置一致性 | 建议 review 后使用 |
| `numSpines` | `2` | `360 / 603` | spine 数量 | EPS 拓扑规模 | 建议使用 |
| `numLeaves` | `4` | `361 / 604` | leaf/ToR 数量 | ToR 集合 | 建议使用 |
| `serversPerLeaf` | `4` | `362 / 605` | 每个 leaf 下 server 数 | 主机规模 | 建议使用 |
| `enableEcho` | `true` | `363 / 606` | 是否安装 Echo 流 | 工程连通性测试 | 不建议作为论文指标 |
| `echoPacketSize` | `1024` | `364 / 607` | Echo 包大小 | 无 | 不建议 |
| `echoInterval` | `0.1` | `365 / 608` | Echo 间隔 | 无 | 不建议 |
| `echoCount` | `5` | `366 / 609` | Echo 包数 | 无 | 不建议 |
| `enableBulk` | `true` | `367 / 610` | 是否启用主 Bulk 流 | FCT/吞吐量样例 | 仅适合小规模验证 |
| `bulkMaxBytes` | `1048576` | `368 / 611` | 主 Bulk 最大字节 | flow size | 仅适合小规模验证 |
| `bulkStart` | `0.2` | `369 / 612` | 主 Bulk 启动时间 | flow arrival | 仅适合小规模验证 |
| `bulkPort` | `10000` | `370 / 613` | 主 Bulk 端口 | 无 | 不建议作为论文变量 |
| `enableSecondBulk` | `true` | `371 / 614` | 是否启用第二条 Bulk | 多流样例 | 仅适合小规模验证 |
| `secondBulkPort` | `10002` | `372 / 615` | 第二条 Bulk 端口 | 无 | 不建议 |
| `secondBulkMaxBytes` | `1048576` | `373 / 616` | 第二条 Bulk 字节 | flow size | 仅适合小规模验证 |
| `secondBulkStart` | `0.3` | `374 / 617` | 第二条 Bulk 启动时间 | flow arrival | 仅适合小规模验证 |
| `enableResidualBulk` | `true` | `375 / 618` | 是否启用 residual Bulk | EPS fallback 样例 | 仅适合小规模验证 |
| `residualBulkPort` | `10001` | `376 / 619` | residual Bulk 端口 | 无 | 不建议 |
| `residualBulkMaxBytes` | `1048576` | `377 / 620` | residual Bulk 字节 | residual flow size | 仅适合小规模验证 |
| `residualBulkStart` | `0.25` | `378 / 621` | residual Bulk 启动时间 | flow arrival | 仅适合小规模验证 |
| `enableStaticOcs` | `true` | `379 / 622` | 是否启用静态 OCS 链路安装 | OCS 光路存在性 | 可用于验证，不足以证明动态 OCS |
| `ocsLeafA` | `0` | `380 / 623` | 手工 OCS 端点 A | OCS pair | 仅 manual/debug |
| `ocsLeafB` | `3` | `381 / 624` | 手工 OCS 端点 B | OCS pair | 仅 manual/debug |
| `ocsDataRate` | `"100Gbps"` | `382 / 625` | OCS 链路速率 | OCS capacity | 建议使用 |
| `ocsDelay` | `"5us"` | `383 / 626` | OCS 链路时延 | OCS delay | 建议使用 |
| `routeMode` | `"global"` | `384 / 627` | 路由模式 | EPS/OCS route 策略 | 需固定并说明 |
| `enableMatrixSelect` | `true` | `385 / 628` | 是否用矩阵选择 OCS | TL-OCS 控制开关 | 建议使用 |
| `trafficMatrixMode` | `"skewed"` | `386 / 629` | synthetic 矩阵模式 | W(t) workload | 可用但只是 synthetic |
| `enableEwmaSmoothing` | `false` | `387 / 630` | 是否启用 EWMA | Abar(t) | 建议用于算法一致性验证 |
| `ewmaBeta` | `0.7` | `388 / 631` | EWMA 系数 | beta | 建议使用 |
| `trafficMatrixSource` | `"synthetic"` | `389 / 632` | 矩阵来源 | W(t) 来源 | 当前不宜宣称外部 trace |
| `trafficGraphThreshold` | `0.0` | `390 / 633` | 流量图阈值 | theta_f | 建议使用 |
| `communityMode` | `"preview"` | `391 / 634` | 社区模式 | Louvain/community | 论文实验应优先明确为 Louvain |
| `louvainMode` | `"single-level"` | `392 / 635` | Louvain 模式 | 社区发现 | 建议 review 后使用 |
| `louvainMaxPasses` | `10` | `393 / 636` | 单层最大 pass | Louvain 参数 | 建议记录 |
| `louvainMaxLevels` | `10` | `394 / 637` | 多层最大 level | Louvain 参数 | 建议记录 |
| `louvainEpsilon` | `1e-9` | `395 / 638` | Louvain 收敛阈值 | Louvain 参数 | 建议记录 |
| `enableMultiPeriodControl` | `false` | `396 / 639` | 是否启用多周期控制 | t=1..T 控制周期 | 可用于控制面验证，需标注非动态数据面 |
| `controlEpochs` | `3` | `397 / 640` | 控制周期数 | 控制周期 T | 建议记录 |
| `trafficMatrixSequence` | `"static"` | `398 / 641` | 多周期矩阵序列模式 | W(t) 序列 | 可用于小规模验证 |
| `selectionMetric` | `"excess"` | `399 / 642` | OCS selection metric | B/G 相关选择目标 | 建议 review 后使用 |
| `eta` | `1.0` | `400 / 643` | 零模型强度系数 | eta | 建议使用 |
| `communityAlpha` | `0.5` | `401 / 644` | 跨社区折减系数 | h(c_i,c_j) | 建议使用 |
| `enableStateHolding` | `false` | `402 / 645` | 是否启用状态保持项 | lambda*x(t-1) | 建议用于算法验证 |
| `stateHoldingLambda` | `0.0` | `403 / 646` | 状态保持权重 | lambda | 建议使用 |
| `previousOcsMode` | `"none"` | `404 / 647` | 上一周期 OCS 状态来源 | x(t-1) | 仅验证用 |
| `previousOcsLeafA` | `0` | `405 / 648` | 上一 OCS A | x(t-1) | 仅 manual/debug |
| `previousOcsLeafB` | `1` | `406 / 649` | 上一 OCS B | x(t-1) | 仅 manual/debug |
| `enableConfigUpdateGate` | `false` | `407 / 650` | 是否启用配置更新门限 | theta_r | 建议用于算法验证 |
| `configUpdateThreshold` | `0.0` | `408 / 651` | 更新门限 | theta_r | 建议使用 |
| `configScoreMode` | `"selection-score-sum"` | `409 / 652` | 配置得分模式 | 更新收益/重构代价 | 需明确是否对应论文公式 |
| `reconfigurationPenalty` | `5.0` | `410 / 653` | 重构惩罚 | reconfiguration cost | 建议使用 |
| `enableHoldTimeGate` | `false` | `411 / 654` | 是否启用最小保持周期 | T_hold | 建议用于算法验证 |
| `minHoldCycles` | `1` | `412 / 655` | 最小保持周期 | T_hold | 建议使用 |
| `previousConfigAge` | `1` | `413 / 656` | 上一配置 age | hold time state | 仅验证用 |
| `ocsPortK` | `1` | `414 / 657` | 每个 ToR OCS 端口数 | k 约束 | 建议使用 |
| `maxSelectedOcsLinks` | `1` | `415 / 658` | 最多选择 OCS link 数 | 全局选边上限 | 建议 review 是否额外限制论文模型 |
| `enableMatrixFlows` | `true` | `416 / 659` | 是否为矩阵流安装应用 | W 到真实 flow 的桥接 | 建议用于小规模验证 |
| `matrixFlowMaxBytes` | `524288` | `417 / 660` | matrix flow 字节 | flow size | 建议记录 |
| `matrixFlowStart` | `0.35` | `418 / 661` | matrix flow 开始时间 | flow arrival | 建议记录 |
| `matrixFlowPortBase` | `11000` | `419 / 662` | matrix flow 起始端口 | 无 | 不作为论文变量 |
| `matrixFlowRxBytesTolerance` | `65536` | `420 / 663` | matrix flow 完成判定容忍 | FCT 判定 | 必须 review |
| `enableOcsAdmissionControl` | `false` | `421 / 664` | 是否启用 OCS admission | OCS 准入 | 建议用于验证 |
| `ocsAdmissionThreshold` | `100.0` | `422 / 665` | OCS admission 阈值 | OCS capacity/admission | 必须明确单位 |
| `matrixFlowDemand` | `40.0` | `423 / 666` | 每条 matrix flow demand | flow demand | 必须明确单位 |
| `enableEpsWecmp` | `false` | `424 / 667` | 是否启用 EPS-WECMP 控制 | EPS-WECMP | 建议用于验证，需标注估计负载 |
| `epsWecmpRho` | `0.7` | `425 / 668` | WECMP smoothing | rho | 建议记录 |
| `epsWecmpGamma` | `2.0` | `426 / 669` | WECMP load sensitivity | gamma | 建议记录 |
| `epsWecmpEpsilon` | `1e-6` | `427 / 670` | WECMP 数值稳定项 | epsilon | 建议记录 |
| `epsWecmpKappa` | `0.5` | `428 / 671` | WECMP 更新步长 | kappa | 建议记录 |
| `epsWecmpMaxDelta` | `0.25` | `429 / 672` | 单步权重最大变化 | max delta | 建议记录 |
| `epsWecmpEpochs` | `3` | `430 / 673` | WECMP epoch 数 | EPS 控制周期 | 可用于验证 |
| `epsWecmpCapacity` | `100.0` | `431 / 674` | EPS 路径容量参数 | link/path capacity | 必须明确不是实测容量 |
| `epsWecmpPathMetric` | `"max"` | `432 / 675` | WECMP 路径负载 metric | WECMP objective | 需 review |
| `epsWecmpDiagnosticLoadMode` | `"none"` | `433 / 676` | WECMP 诊断负载模式 | 诊断输入 | 不建议用于论文实验 |
| `epsWecmpDiagnosticLoad` | `0.0` | `434 / 677` | WECMP 诊断负载值 | 诊断输入 | 不建议 |
| `epsWecmpDiagnosticHotSpine` | `0` | `435 / 678` | hot spine 诊断对象 | 诊断输入 | 不建议 |
| `enableEpsWecmpRouting` | `false` | `436 / 679` | 是否把 WECMP 决策写入静态路由 | EPS-WECMP 数据面近似 | 建议用于小规模验证 |
| `enableMultiPeriodWecmpState` | `true` | `437 / 680` | 是否跨周期保留 WECMP state | WECMP state | 可用于验证 |
| `enableAlgorithmInvariantCheck` | `true` | `438 / 681` | 启用算法 invariant 检查 | 一致性检查 | 建议开启 |
| `strictAlgorithmInvariantCheck` | `false` | `439 / 682` | invariant 失败是否中止 | 一致性检查 | 验证阶段建议开启 |
| `enableDetailedAlgorithmTrace` | `false` | `440 / 683` | 详细算法 trace | review/debug | 建议小规模开启 |
| `detailedCandidateLogLimit` | `20` | `441 / 684` | candidate trace 上限 | review/debug | 建议记录 |
| `enableDetailedFlowTrace` | `false` | `442 / 685` | 详细 flow trace | review/debug | 建议小规模开启 |
| `detailedFlowLogLimit` | `50` | `443 / 686` | flow trace 上限 | review/debug | 建议记录 |
| `enableStructuredResultExport` | `false` | `444 / 687` | 启用 CSV 导出 | 论文数据出口 | 建议开启，但指标仍需补足 |
| `structuredResultDir` | `"../sim/results/raw"` | `445 / 688` | CSV 输出目录 | 结果路径 | 建议显式指定 |
| `enableResultValidation` | `true` | `446 / 689` | 启用结果校验 | 输出一致性 | 建议开启 |
| `validationMode` | `"warn"` | `447 / 690` | 校验失败处理 | 输出一致性 | 验证阶段建议 strict/fail |

## 五、论文算法到代码的对应关系

| 论文算法步骤 | 论文公式或概念 | 代码文件 | 函数/变量/代码片段位置 | 当前实现状态 | 风险或疑问 |
|---|---|---|---|---|---|
| 1. 原始有向流量矩阵 W(t) | `W_ij(t)` | `src/traffic/traffic-matrix.h`, `src/main/hybrid-dcn-main.cc` | `buildSyntheticDirectedTrafficMatrix`: `traffic-matrix.h:69`; 调用：`hybrid-dcn-main.cc:1510`, `2596` | 部分实现 | 当前主要是 synthetic；`trafficMatrixSource` 默认 synthetic，未看到外部矩阵读取实现 |
| 2. 无向通信强度 A(t) | `A_ij = W_ij + W_ji` | `src/traffic/traffic-matrix.h` | `buildUndirectedCommunicationIntensityMatrix`: `traffic-matrix.h:88`; 调用：`hybrid-dcn-main.cc:1512`, `2598` | 已实现 | 依赖 W 的可信度 |
| 3. EWMA 平滑 Abar(t) | `Abar(t)=beta*Abar(t-1)+(1-beta)*A(t)` | `src/traffic/traffic-matrix.h` | `updateEwmaMatrix`: `traffic-matrix.h:135`; 调用：`hybrid-dcn-main.cc:1518`, `2604` | 已实现 | 单周期默认关闭；多周期时需确认初值和 beta 语义 |
| 4. 流量图阈值稀疏化 theta_f | `A_ij < theta_f -> 0` | `src/traffic/traffic-matrix.h` | `applyTrafficGraphThreshold`: `traffic-matrix.h:113`; 调用：`hybrid-dcn-main.cc:1515`, `2601` | 已实现 | 阈值单位和 synthetic demand 单位需说明 |
| 5. 节点吞吐度 d_i | `d_i=sum_j Abar_ij` | `src/traffic/traffic-matrix.h` | `computeNodeDegree`: `traffic-matrix.h:164`; 调用：`hybrid-dcn-main.cc:1528`, `2258` | 已实现 | 是控制面矩阵度，不是 NS-3 实测吞吐 |
| 6. 有效总流量 M | `M=sum_{i<j} Abar_ij` | `src/traffic/traffic-matrix.h` | `computeTotalTraffic`: `traffic-matrix.h:178`; 调用：`hybrid-dcn-main.cc:1529`, `2259` | 已实现 | 是矩阵总量，不是测量总流量 |
| 7. 随机图零模型期望流量 P_ij | `eta*d_i*d_j/(2M)` | `src/main/hybrid-dcn-main.cc` | `expectedTraffic`: `1554`, `2274` | 已实现 | 需确认 `M` 定义与论文公式的 `2M` 是否完全一致 |
| 8. 模块度增益 B_ij | `B_ij=Abar_ij-P_ij` | `src/main/hybrid-dcn-main.cc`, `src/model/louvain.h` | `modularityGain`: `1555`, `2275`; Louvain modularity: `louvain.h:124-154` | 已实现 | Louvain 内部 modularity 与 OCS selection 中 B 的 eta 处理需人工核对 |
| 9. Louvain 社区划分 | Louvain on traffic graph | `src/model/louvain.h`, `src/main/hybrid-dcn-main.cc` | `runSingleLevelLouvain`: `louvain.h:260`; `runMultiLevelLouvain`: `louvain.h:282`; 调用：`1573-1583`, `2295-2305` | 已实现 | 默认 `communityMode="preview"`，论文实验需明确是否真正使用 Louvain |
| 10. 社区折减 h(c_i,c_j) | same-community 1, cross-community alpha | `src/main/hybrid-dcn-main.cc` | `communityFactor`: `1654-1656`, `2321-2323` | 已实现 | `communityAlpha` 默认 0.5，需与论文一致 |
| 11. 光调度增益 G_ij | `[B_ij]^+ h(c_i,c_j)` | `src/main/hybrid-dcn-main.cc` | `baseUtility`, `communityUtility`, `utility`: `1653-1674`, `2320-2334` | 已实现 | 选择分数还可叠加 state holding；需区分论文原始 G 与最终 selection utility |
| 12. 状态保持项 | `lambda*x_ij(t-1)` | `src/main/hybrid-dcn-main.cc` | `enableStateHolding`, `stateHoldingLambda`: `402-403`; utility 叠加：`1657-1674`, `2325-2334` | 已实现 | 上一状态可由参数或多周期状态产生；需 review 初始状态 |
| 13. OCS 端口约束 k | 每 ToR 最多 k 条 OCS | `src/main/hybrid-dcn-main.cc` | `ocsPortK`: `414`; greedy degree check: `1724-1775`; invariant: `6876-6898` | 已实现 | 另有 `maxSelectedOcsLinks` 全局上限，可能比论文 k 更强 |
| 14. 贪心候选边选择 | 按 utility 排序选边 | `src/main/hybrid-dcn-main.cc` | candidate 排序：`1710-1718`; selection：`1724-1776` | 已实现 | 贪心近似，非全局最优 matching；需与论文描述一致 |
| 15. 配置更新阈值 theta_r | 仅当收益超过门限才更新 | `src/main/hybrid-dcn-main.cc` | `enableConfigUpdateGate`: `407`; `configUpdateThreshold`: `408`; `computeConfigScore`: `1842-1895` | 已实现 | `configScoreMode` 有多种，需确定论文实验用哪一种 |
| 16. 最小保持周期 T_hold | 已安装光路至少保持若干周期 | `src/main/hybrid-dcn-main.cc`, `src/ocs/ocs-state.h` | `enableHoldTimeGate`: `411`; `minHoldCycles`: `412`; hold edges: `1727-1761`; age matrix: `ocs-state.h:10` | 已实现 | 多周期控制面状态存在，但是否映射到 NS-3 时间线动态重构不明确 |
| 17. OCS 光路安装 | 安装 leaf-to-leaf OCS 链路 | `src/main/hybrid-dcn-main.cc` | `AddOcsLink`: `331-350`; 安装：`3223-3275` | 部分实现 | 是 P2P 链路模拟 OCS；需要确认是否只在仿真开始前一次性安装 |
| 18. OCS 准入 | 命中 OCS 且容量允许 | `src/main/hybrid-dcn-main.cc` | `enableOcsAdmissionControl`: `421`; `applyOcsAdmission`: `3345-3391`; 调用：`3399`, `3476` | 部分实现 | 基于 demand/threshold 的抽象控制，不是链路实时队列或带宽测量 |
| 19. EPS fallback | OCS 未命中或 admission 拒绝走 EPS | `src/main/hybrid-dcn-main.cc` | residual/fallback specs：`3428-3500`; route label 写入 flow spec | 部分实现 | 仅对 matrix flows 明确标记；需要核对所有应用流是否一致受控 |
| 20. EPS-WECMP | 基于链路状态的 WECMP 分流 | `src/eps/eps-wecmp-state.h`, `src/main/hybrid-dcn-main.cc` | state：`eps-wecmp-state.h:8-57`; residual load：`1979-2007`; update：`2032-2173`; route：`3638-3704` | 部分实现 | 当前是控制面 residual load 估计，不是 NS-3 链路利用率实测 |
| 21. 链路利用率观测 | 真实 link utilization | `src/main/hybrid-dcn-main.cc`, `src/eps/eps-wecmp-state.h` | EPS/OCS MacTx 聚合：`260-267`, `3212-3213`, `5894-5900`; WECMP observed fields：`eps-wecmp-state.h:11-13` | 未充分实现 | 未看到 per-link NetDevice utilization trace 驱动 WECMP；不能假装是真实链路利用率 |
| 22. FCT/吞吐量/链路利用率/重构次数输出 | 论文指标 | `src/main/hybrid-dcn-main.cc` | flow 输出：`5549-6135`; CSV：`6337-6699` | 部分实现 | 有 per-flow FCT/goodput 和 bytes；平均 FCT、99% FCT、真实链路利用率、明确重构次数指标不足 |

## 六、当前工程能力判断

| 能力 | 已实现/部分实现/未实现/不确定 | 依据 | 是否足够用于论文实验 |
|---|---|---|---|
| Leaf-Spine EPS 拓扑 | 已实现 | node/link 构建：`src/main/hybrid-dcn-main.cc:3118-3221` | 小规模拓扑验证足够；论文需补规模和参数说明 |
| ToR/Leaf 级 OCS 链路 | 部分实现 | `AddOcsLink`: `331-350`; 安装：`3223-3275` | 可验证 OCS shortcut；不足以证明动态 OCS 重构 |
| synthetic traffic matrix | 已实现 | `buildSyntheticDirectedTrafficMatrix`: `src/traffic/traffic-matrix.h:69` | 可用于算法单元验证；论文 workload 说服力有限 |
| 外部流量矩阵输入 | 未实现 | `trafficMatrixSource` 默认 synthetic；未扫描到 parser/文件读取代码 | 不足 |
| Louvain 社区发现 | 已实现 | `src/model/louvain.h:260`, `282` | 可用于算法验证，需确认参数和默认模式 |
| B_ij 模块度收益计算 | 已实现 | `expectedTraffic`/`modularityGain`: `src/main/hybrid-dcn-main.cc:1554-1555`, `2274-2275` | 可用于公式一致性验证 |
| OCS 端口约束 | 已实现 | `ocsPortK`: `414`; degree check：`1724-1775` | 可用于验证 |
| 多周期控制 | 部分实现 | `enableMultiPeriodControl`: `396`; 多周期循环：`2586-2726` | 控制面验证可用；不应直接宣称 NS-3 动态重构 |
| OCS 重构次数统计 | 部分实现 | epoch summary 有 changed/config score 字段；CSV summary 有部分选择信息 | 论文重构次数指标仍需明确导出和定义 |
| OCS 准入控制 | 部分实现 | `applyOcsAdmission`: `3345-3391` | 抽象准入可用；非真实链路容量测量 |
| EPS fallback | 部分实现 | fallback/residual flow specs：`3428-3500` | 小规模验证可用；需确认所有失败 OCS 流都一致 fallback |
| WECMP 权重计算 | 已实现 | `runEpsWecmpUpdateForPair`: `2032-2173` | 控制面算法验证可用 |
| WECMP 是否基于真实 NS-3 链路利用率 | 未实现 | residual load 来自 `accumulateEpsResidualTraffic`: `1979-2007`；未见 per-link utilization trace 反馈 | 不足 |
| FCT 统计 | 部分实现 | `MatrixBulkFlowStats`: `94`; flow 输出：`5705-5889` | 可做小样本检查；论文平均/尾延迟不足 |
| 平均 FCT | 未实现/不充分 | 未看到明确 average FCT 汇总字段 | 不足 |
| 99% FCT | 未实现 | 未看到 p99 统计逻辑 | 不足 |
| 吞吐量 | 部分实现 | `observedGoodputMbps`、`g_ocsTxBytes`、`g_epsTxBytes` 输出 | 可做样例；需明确统计窗口和口径 |
| 链路利用率 | 部分实现 | OCS/EPS 聚合 MacTx bytes；WECMP estimated utilization | 不足以支撑真实 per-link utilization 图 |
| 结构化 CSV 输出 | 已实现 | `enableStructuredResultExport`: `444`; CSV 导出：`6337-6699` | 可用于 review；论文指标字段仍需补足 |
| 批量实验脚本 | 未实现 | `scripts/` 当前为空目录 | 不足 |
| 绘图脚本 | 未实现 | `results/figures/` 仅 `.gitkeep`；未扫描到 plot 脚本 | 不足 |

## 七、风险清单

| 风险编号 | 风险描述 | 代码依据 | 影响 | 建议后续处理 |
|---|---|---|---|---|
| R1 | WECMP 负载可能只是控制面估计，不是 NS-3 实测链路利用率 | `src/main/hybrid-dcn-main.cc:1979-2007`, `2032-2173`; `src/eps/eps-wecmp-state.h:11-13` | 若论文声称基于实时链路状态，会高估实现真实性 | 增加 per-link NetDevice/Queue trace，或在论文/结果中明确标注为 control-plane estimated load |
| R2 | 流量矩阵当前主要是 synthetic | `src/traffic/traffic-matrix.h:69`; `trafficMatrixSource`: `src/main/hybrid-dcn-main.cc:389`, `632` | workload 代表性不足，外部 trace 实验无法成立 | 后续最小任务先定义并验证外部矩阵输入格式 |
| R3 | OCS 动态重构可能没有在 NS-3 时间线上真实发生 | 多周期控制：`src/main/hybrid-dcn-main.cc:2586-2726`; OCS 安装：`3223-3275` | 控制周期重构次数不能等价于数据面动态 reconfiguration | 人工确认是否存在运行时 link/route update；若无，文稿需改为控制面选择验证 |
| R4 | OCS admission 是抽象容量准入，不是链路实时容量/排队状态 | `applyOcsAdmission`: `src/main/hybrid-dcn-main.cc:3345-3391` | 准入效果可能与实际传输性能脱节 | 明确 demand/threshold 单位，并与真实 OCS 速率或流完成情况绑定 |
| R5 | FCT 统计基于少量 BulkSend/PacketSink 流 | matrix flow 安装：`src/main/hybrid-dcn-main.cc:3733-3836`; flow 输出：`5705-5889` | 不足以支撑平均 FCT、99% FCT 等论文级指标 | 增加小规模确定性验证后，再扩展统一 flow workload 和汇总统计 |
| R6 | 链路利用率没有明确 per-link 数据面统计 | EPS/OCS 目前主要是聚合 MacTx bytes：`src/main/hybrid-dcn-main.cc:260-267`, `5894-5900` | 无法可靠画链路利用率 CDF/时序图 | 为 leaf-spine 和 OCS NetDevice 增加按链路统计 |
| R7 | baseline 公平性依赖 preset/override 规则，容易误用 | preset/override 相关参数：`src/main/hybrid-dcn-main.cc:358-359`, `601-602`; 预设逻辑集中在 `791-1165` 附近 | 不同 baseline 可能不只改变一个算法开关 | 先生成配置审计表，明确每个 preset 改了哪些参数 |
| R8 | 结构化 CSV 字段还不足以直接支撑论文图 | CSV 导出：`src/main/hybrid-dcn-main.cc:6437-6699` | 缺平均 FCT、p99 FCT、per-link utilization、明确 reconfiguration count | 先定义论文图所需字段，再补最小导出 |
| R9 | `maxSelectedOcsLinks` 是额外全局上限，可能改变论文 k-port 问题 | `maxSelectedOcsLinks`: `src/main/hybrid-dcn-main.cc:415`, `658`; selection：`1724-1776` | 当 `maxSelectedOcsLinks < floor(numLeaves*k/2)` 时，会额外限制 OCS 规模 | 论文实验中需固定并解释，或设为与 k 约束一致 |
| R10 | 主文件过大，算法、NS-3 数据面、输出混在一起 | `src/main/hybrid-dcn-main.cc` 包含 main、控制、拓扑、路由、统计、CSV | review 容易漏掉控制面/数据面边界 | 后续先做文档级审计，不急于重构；若改源码需小步拆分 |

## 八、建议的后续最小任务

以下任务仅作为建议，本次不执行。

| 任务编号 | 任务目标 | 涉及文件 | 是否改源码 | 验收标准 |
|---|---|---|---|---|
| T1 | 做一个 4-leaf 手算矩阵的一致性 review：核对 W、A、Abar、d、M、B、G、selected edge | `docs/V2.md`, `src/traffic/traffic-matrix.h`, `src/main/hybrid-dcn-main.cc`, `docs/code_map.md` | 否 | 人工表格中每个公式值都能对应到代码输出或代码变量 |
| T2 | 生成 preset 参数审计文档，明确每个 baseline/场景实际覆盖哪些参数 | `src/main/hybrid-dcn-main.cc`, `docs/` | 否 | 每个 `presetScenario` 有一行完整参数差异表 |
| T3 | 明确外部流量矩阵输入格式和最小 parser 方案 | `docs/`, 后续可能涉及 `src/traffic/traffic-matrix.h` | 暂不改源码 | 文档给出 CSV/JSON 格式、单位、时间周期和校验规则 |
| T4 | 设计 per-link 数据面利用率统计的最小实现 | `src/main/hybrid-dcn-main.cc`, `src/result/` | 后续可能改源码 | 每条 leaf-spine/OCS link 能导出 bytes、时间窗口、utilization |
| T5 | 定义论文指标 CSV schema：平均 FCT、p99 FCT、吞吐量、链路利用率、重构次数 | `docs/`, `results/raw/*.csv` | 否 | schema 文档能说明每个论文图需要哪些字段、当前已有/缺失哪些字段 |

## 本次读取和修改记录

### 已读取/扫描

- 仓库目录结构：通过 `find . -path './.git' -prune -o -type d -print` 完整扫描。
- 仓库文件清单：通过 `find . -path './.git' -prune -o -type f -print` 与 `git ls-files` 扫描。
- 主入口和源码：重点阅读 `src/main/hybrid-dcn-main.cc`、`src/traffic/traffic-matrix.h`、`src/model/louvain.h`、`src/ocs/ocs-state.h`、`src/eps/eps-wecmp-state.h`。
- 文档和结果目录：纳入 `docs/`、`results/raw/`、`results/figures/`、`results/tables/`、`experiments/`、`scripts/` 的文件或空目录状态。

### 本次新增/修改文件

- 新增：`docs/code_map.md`
- 未修改任何已有源码文件。
- 未修改 CMake、wscript、运行脚本或配置文件。
- 未运行仿真实验或大规模实验。

### 人工优先 review 位置

1. 第五节“论文算法到代码的对应关系”：确认公式和代码位置是否与 V2 文稿一致。
2. 第六节“当前工程能力判断”：确认哪些能力只能用于小规模控制面验证。
3. 第七节“风险清单”：优先处理 WECMP 真实链路利用率、OCS 动态重构、FCT/p99 指标不足三类问题。

## 文档校准记录

校准时间：2026-05-25

### 本次复核的源码文件

- `src/main/hybrid-dcn-main.cc`
- `src/traffic/traffic-matrix.h`
- `src/model/louvain.h`
- `src/ocs/ocs-state.h`
- `src/eps/eps-wecmp-state.h`

### 本次修正的具体条目

- 已按当前源码复核并修正拓扑链路参数：
  - server-leaf `PointToPointHelper serverLeafP2p` 为 `25Gbps / 1us`，位置 `src/main/hybrid-dcn-main.cc:3158-3160`。
  - leaf-spine `PointToPointHelper leafSpineP2p` 为 `40Gbps / 2us`，位置 `src/main/hybrid-dcn-main.cc:3162-3164`。
  - OCS 默认参数保持为 `ocsDataRate="100Gbps"`、`ocsDelay="5us"`，位置 `src/main/hybrid-dcn-main.cc:382-383`。
- 已复核 `main()` 位置：函数名在 `src/main/hybrid-dcn-main.cc:354`。
- 已复核 `CommandLine` 参数默认值和注册位置：默认值集中在 `src/main/hybrid-dcn-main.cc:356-447`，注册集中在 `src/main/hybrid-dcn-main.cc:599-754`。
- 已复核 CSV 输出位置和字段：summary `6437-6505`，flows `6513-6593`，WECMP `6601-6646`，OCS candidates `6654-6692`。
- 已复核 WECMP `observedTraffic` 语义：`src/eps/eps-wecmp-state.h:11-13`、`49-52` 明确标注为 control-plane estimated residual load，不是 ns-3 measured per-link bytes。
- 已复核 `trafficMatrixSource`：当前校验只允许 `synthetic`，位置 `src/main/hybrid-dcn-main.cc:1219-1224`。
- 已复核目录状态：`scripts/`、`experiments/configs/`、`experiments/runs/`、`build-meta/`、`src/utils/` 当前未发现文件；`results/raw/` 包含历史日志、CSV 和 NetAnim XML；`results/figures/`、`results/tables/` 仅有 `.gitkeep`。

### 是否修改源码

否。未修改任何 `src/` 下源码或头文件。

### 是否运行实验

否。未运行仿真、构建或大规模实验，只执行只读检查命令。

### 当前仍需人工 review 的事项

- 多周期控制是否只是控制面状态推进，而不是 ns-3 时间线上的动态 OCS 重构。
- WECMP 是否需要接入真实 per-link NetDevice/Queue 观测，避免把控制面估计误写为链路遥测。
- 结构化 CSV 是否需要补充平均 FCT、99% FCT、per-link utilization 和明确 reconfiguration count 字段。

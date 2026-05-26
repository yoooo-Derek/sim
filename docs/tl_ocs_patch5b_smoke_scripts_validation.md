# TL-OCS Patch 5B Small-Smoke Scripts Validation

## 1. 目标和边界

Patch 5B 新增一个小而可验收的 small-smoke experiment script 框架，用于验证 baseline 参数、CSV schema、路径真实性和 Patch 2.6 / Patch 3 / Patch 4B / Patch 4C 回归。

本任务不是大规模论文实验，不跑 medium-sanity 或 paper-candidate，不画最终论文图，不修改 TL-OCS / WECMP / route fix 源码，不修改任何 CSV schema。

## 2. 修改范围

新增文件：

- `scripts/tl_ocs_experiments/README.md`
- `scripts/tl_ocs_experiments/run_smoke_matrix.sh`
- `scripts/tl_ocs_experiments/validate_outputs.py`
- `scripts/tl_ocs_experiments/collect_summary.py`
- `docs/tl_ocs_patch5b_smoke_scripts_validation.md`

未修改源码：

- 未修改 `src/main/hybrid-dcn-main.cc`
- 未修改 `src/eps/eps-wecmp-state.h`
- 未修改 TL-OCS 控制算法
- 未修改 OCS admission
- 未修改 EPS fallback / residual
- 未修改 WECMP decision
- 未修改 Patch 2.6 route fix
- 未修改 Patch 3 / 4B / 4C CSV schema

## 3. 新增脚本说明

### README.md

说明 Patch 5B 只运行 small-smoke，默认输出到 `/tmp/tl-ocs-experiments/<run-id>`，不写 `results/raw`，不运行 paper-candidate，不产生论文结论。README 还记录了运行方式、输出布局、6 个主场景和失败排查入口。

### run_smoke_matrix.sh

功能：

- 默认 `RUN_ROOT=/tmp/tl-ocs-experiments`
- 默认 `RUN_ID=smoke-YYYYmmdd-HHMMSS`
- 支持环境变量覆盖 `RUN_ID`、`RUN_ROOT`、`NS3_BIN`
- 默认使用 `/home/dyn/ns-3.47/build/scratch/ns3.47-hybrid-dcn-main-optimized`
- 创建：
  - `<run-dir>/logs`
  - `<run-dir>/csv`
  - `<run-dir>/ns3-cwd`
  - `<run-dir>/sim/results/raw`
- `structuredResultDir` 指向 `<run-dir>/csv`
- 每个场景 stdout / stderr 写入 `logs/<experimentName>.log`
- 每个场景命令、开始时间、结束时间、返回码写入 `manifest.csv`
- 默认继续跑完所有场景，并在任一场景失败或 validation 失败时最终返回非零
- 所有场景跑完后自动调用 `validate_outputs.py --strict`

### validate_outputs.py

仅使用 Python 标准库。

输入：

- `--run-dir`
- `--manifest` 可选
- `--csv-dir` 可选
- `--report` 可选
- `--strict`

输出：

- `validation-report.csv`
- stdout summary
- 全部必需检查通过返回 0，否则返回非零

主要检查项：

- manifest 存在，6 个主场景均有返回码且为 0；
- 每个主场景存在 `summary.csv`、`flows.csv`、`links.csv`、`link-timeseries.csv`、`ocs-candidates.csv`；
- WECMP 场景存在 `wecmp.csv`；
- measured WECMP 场景存在 `measured-wecmp.csv`；
- `overallResultConsistency=pass`；
- `overallAlgorithmInvariant=pass`；
- `completedFlows > 0`；
- `completedFlowCount > 0`；
- `linkCounterCount=18`；
- `linkTimeseriesEnabled=true`；
- `linkTimeseriesSampleRows > 0`；
- flows 必需字段存在；
- links 必需字段存在；
- link-timeseries 单调、utilization 可解析且非负、有非零 delta；
- OCS hit 走 OCS；
- fallback / WECMP / measured later 不泄漏到 selected OCS 0-3；
- deterministic fallback 走 spine0；
- control-plane WECMP / measured no-sample fallback 走 spine1；
- measured no-sample 记录 fallback/no-sample；
- measured later proof 记录 selectedSpine 改变；
- measured-wecmp 行数为 16。

### collect_summary.py

可选辅助脚本，仅使用 Python 标准库。

功能：

- 收集 run-dir 或 csv-dir 下所有 `*-summary.csv`
- 输出 `combined-summary.csv`
- 不画图
- 不做统计结论
- 不作为 Patch 5B 必需验收条件

## 4. 实际运行

运行命令：

`RUN_ID=patch5b-smoke-20260526-000000 scripts/tl_ocs_experiments/run_smoke_matrix.sh`

实际 run-id：

`patch5b-smoke-20260526-000000`

实际输出目录：

`/tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000`

关键输出：

- `/tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000/manifest.csv`
- `/tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000/validation-report.csv`
- `/tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000/combined-summary.csv`
- `/tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000/logs/`
- `/tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000/csv/`

还单独运行：

- `scripts/tl_ocs_experiments/validate_outputs.py --run-dir /tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000 --strict`
- `scripts/tl_ocs_experiments/collect_summary.py --run-dir /tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000`

两者均返回 0。

## 5. 六个主场景和返回码

| 场景 | 目标 | 返回码 |
|---|---|---:|
| `smoke__4l2s2h__skewed__static_ocs03__hit__det__seed0` | Static OCS 0-3 hit | 0 |
| `smoke__4l2s2h__skewed__static_ocs03__admit_fallback__det__seed0` | Admission fallback route fix | 0 |
| `smoke__4l2s2h__skewed__tl_ocs__fallback__det__seed0` | TL-OCS deterministic EPS fallback | 0 |
| `smoke__4l2s2h__skewed__tl_ocs__fallback__cp_wecmp__seed0` | TL-OCS control-plane WECMP | 0 |
| `smoke__4l2s2h__skewed__tl_ocs__fallback__measured_nosample__seed0` | Patch 4B measured no-sample fallback | 0 |
| `smoke__4l2s2h__skewed__tl_ocs__fallback__measured_later__seed0` | Patch 4C later-flow selectedSpine proof | 0 |

## 6. validation-report 摘要

`validation-report.csv` 结果：

- passed checks：191
- failed checks：0

全部 required checks pass。

每个 summary 行均满足：

- `overallResultConsistency=pass`
- `overallAlgorithmInvariant=pass`
- `linkCounterCount=18`
- `linkTimeseriesSampleRows=252`

Measured later 场景额外满足：

- `measuredWecmpLaterProofPassed=true`
- `measuredWecmpLaterFlowCount=1`
- `measuredWecmpLaterFlowCompletedCount=1`
- `measuredWecmpLaterChangedSelectedSpineCount >= 1`
- `measuredWecmpLaterOcsLeakageDetected=false`

## 7. 关键 route correctness 检查

`links.csv` 关键方向级 Tx bytes：

| 场景 | OCS 0-3 a-to-b txBytes | spine0->leaf3 txBytes | spine1->leaf3 txBytes |
|---|---:|---:|---:|
| Static OCS hit | 577320 | 0 | 0 |
| Static admission fallback | 0 | 577320 | 0 |
| TL-OCS deterministic fallback | 0 | 577320 | 0 |
| TL-OCS control-plane WECMP | 0 | 0 | 577320 |
| TL-OCS measured no-sample fallback | 0 | 0 | 577320 |
| TL-OCS measured later proof | 0 | 577320 | 577320 |

结论：

- OCS hit 场景真实走 OCS；
- admission fallback 和 deterministic fallback 真实走 EPS spine0；
- control-plane WECMP 和 measured no-sample fallback 真实走 frozenSpine=1；
- measured later proof 中 early flow 保持 spine1，later flow 走 measured selected spine0；
- fallback / WECMP / measured later 场景 selected OCS 0-3 a-to-b 均为 0，Patch 2.6 route fix 保持。

## 8. Patch 3 link-timeseries 回归

6 个主场景均生成 `link-timeseries.csv`。

验证器检查：

- header 包含 `sampleTimeSeconds`, `deltaTxBytes`, `sampleThroughputMbps`, `utilizationApprox`；
- 数据行数 > 0；
- summary `linkTimeseriesSampleRows=252`；
- `sampleTimeSeconds` 单调非降；
- `utilizationApprox` 可解析且 `>=0`；
- 至少存在 `deltaTxBytes > 0` 的 sample row。

Patch 3 time-series 导出保持。

## 9. Patch 4B measured-wecmp 回归

Measured WECMP 场景：

- `smoke__4l2s2h__skewed__tl_ocs__fallback__measured_nosample__seed0`
- `smoke__4l2s2h__skewed__tl_ocs__fallback__measured_later__seed0`

均生成 `measured-wecmp.csv`，每个文件 16 行：

- 4 leaves
- 2 spines
- 2 directions

验证器检查：

- `hasSample` 字段存在；
- `measuredUtilization` 可解析且 `>=0`。

Patch 4B measured snapshot projection 保持。

## 10. Patch 4C later proof 回归

Measured later proof 场景的 later flow：

- `measuredLaterSelectedSpine=0`
- `measuredLaterControlPlaneSelectedSpine=1`
- `measuredLaterSelectedSpineChanged=true`
- `completed=true`
- `measuredLaterHasMeasuredSample=true`

`wecmp.csv` 中 later decision 满足：

- `appliesToLaterFlow=true`
- `selectedSpine != controlPlaneSelectedSpine`
- `measuredDecisionUsed=true`
- `measuredDecisionFallback=false`
- `measuredNoSample=false`

Patch 4C later new flow selectedSpine change proof 保持。

## 11. results/raw 检查

脚本未写入仓库历史 `results/raw`。

实现方式：

- runner 从 `<run-dir>/ns3-cwd` 启动 simulator；
- 创建 `<run-dir>/sim/results/raw` 接收 simulator 内部相对路径 artifact；
- structured CSV 写 `<run-dir>/csv`；
- logs 写 `<run-dir>/logs`。

只读检查未发现本轮运行后仓库 `results/raw` 下有新文件。

## 12. 结论

Patch 5B 通过。

已新增 small-smoke experiment script framework，6 个主场景均返回 0，自动 validation 191 项全部通过。Patch 2.6 route fix、Patch 3 link-timeseries、Patch 4B measured-wecmp、Patch 4C later proof 均保持。

本补丁没有修改源码，没有跑大规模论文实验，没有画最终论文图，没有写入 `results/raw`。

可以进入 Patch 5C。建议 Patch 5C 是 medium-sanity 或 collect/aggregation 增强，而不是直接 paper-candidate sweep。

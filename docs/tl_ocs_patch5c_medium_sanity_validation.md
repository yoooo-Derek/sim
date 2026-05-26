# TL-OCS Patch 5C Medium-Sanity Validation

## 1. 目标和边界

Patch 5C 的目标是增强 Patch 5B small-smoke 脚本框架的 aggregation / validation 能力，并运行一个很小的 8-leaf medium-sanity 子集，证明脚本可以扩展到稍大规模。

本任务不是 paper-candidate sweep，不是大规模论文实验，不画最终论文图，不修改 TL-OCS / WECMP / route fix 源码，不修改 Patch 3 / 4B / 4C CSV schema。

## 2. 修改范围

修改或新增文件：

- `scripts/tl_ocs_experiments/README.md`
- `scripts/tl_ocs_experiments/run_medium_sanity.sh`
- `scripts/tl_ocs_experiments/validate_outputs.py`
- `scripts/tl_ocs_experiments/collect_summary.py`
- `docs/tl_ocs_patch5c_medium_sanity_validation.md`

未修改源码：

- 未修改 `src/main/hybrid-dcn-main.cc`
- 未修改 `src/eps/eps-wecmp-state.h`
- 未修改 TL-OCS 控制算法
- 未修改 OCS admission
- 未修改 EPS fallback / residual
- 未修改 WECMP decision
- 未修改 Patch 2.6 route fix
- 未修改 Patch 3 / 4B / 4C CSV schema

`run_smoke_matrix.sh` 的 6 个 smoke 场景语义未改变。

## 3. run_medium_sanity.sh

新增 `scripts/tl_ocs_experiments/run_medium_sanity.sh`。

默认行为：

- `RUN_ROOT=/tmp/tl-ocs-experiments`
- `RUN_ID=medium-YYYYmmdd-HHMMSS`
- 支持环境变量覆盖 `RUN_ID`、`RUN_ROOT`、`NS3_BIN`
- 不自动 build；若 binary 不存在，只提示先运行 `/home/dyn/ns-3.47/ns3 build`
- 输出目录：
  - `<run-dir>/logs`
  - `<run-dir>/csv`
  - `<run-dir>/manifest.csv`
  - `<run-dir>/validation-report.csv`
  - `<run-dir>/combined-summary.csv`
- `structuredResultDir=<run-dir>/csv`
- stdout / stderr 写入 `logs/<experimentName>.log`
- 每个场景命令、开始时间、结束时间、返回码写入 `manifest.csv`
- 任一场景失败时默认继续跑完，并最终返回非零
- 场景完成后调用 `validate_outputs.py --suite medium --scenarios-from-manifest --strict`
- validation 成功后调用 `collect_summary.py`

Medium 公共参数：

- `numLeaves=8`
- `numSpines=4`
- `serversPerLeaf=2`
- `simTime=3.0`
- `enableStructuredResultExport=true`
- `linkUtilizationSampleInterval=0.1`

## 4. validate_outputs.py 增强

增强内容：

- 新增 `--suite smoke|medium`，默认 `smoke`，保持 Patch 5B 兼容。
- 新增 `--scenarios-from-manifest`，可从 `manifest.csv` 自动读取场景列表。
- 支持从 experimentName 解析 scale，例如 `4l2s2h`、`8l4s2h`。
- 根据 scale 推断 expected `linkCounterCount`：
  - `4l2s2h`: `4 * 2 * 2 + 2 = 18`
  - `8l4s2h`: `8 * 4 * 2 + 2 = 66`
- 根据 scale 推断 measured-wecmp expected rows：
  - `4l2s2h`: `4 * 2 * 2 = 16`
  - `8l4s2h`: `8 * 4 * 2 = 64`
- 不再硬编码 `link-timeseries.csv` 行数，只检查存在、行数 > 0、时间单调、utilization 可解析且非负、有 nonzero delta。
- 不再硬编码 selected OCS pair 为 0-3；从 `links.csv` 的 OCS a-to-b counter 自动识别 selected OCS link。
- deterministic fallback route correctness 从 `flows.csv` 中的 EPS flow src/dst 推断。
- WECMP route correctness 从 `flows.csv` 中的 `frozenSpine` 推断，不再硬编码 spine1。
- measured later proof 继续强制检查 `selectedSpine != controlPlaneSelectedSpine` 和 measured decision used。

## 5. collect_summary.py 增强

增强内容：

- 继续只使用 Python 标准库。
- 支持 `--run-dir`、`--csv-dir`、`--output`。
- 输出 `combined-summary.csv`。
- 每行追加 metadata：
  - `sourceFile`
  - `suite`
  - `scale`
  - `traffic`
  - `baseline`
  - `admission`
  - `wecmp`
  - `seed`
- metadata 从 `<suite>__<scale>__<traffic>__<baseline>__<admission>__<wecmp>__seed<seed>` 解析。
- 名称不符合规范时字段填 `unknown` 并在 stdout 打 warning。
- stdout 输出 baseline row count summary。
- 不画图，不做统计结论，不计算置信区间。

## 6. README 更新

README 增加：

- `run_medium_sanity.sh` 用法；
- medium-sanity 仍不是论文结论；
- `validate_outputs.py --suite medium --scenarios-from-manifest` 用法；
- medium expected counter / measured-wecmp rows 推断；
- `collect_summary.py` 解析 metadata 字段说明；
- 仍禁止直接 paper-candidate。

## 7. Smoke 兼容性检查

使用已有 Patch 5B run 做 smoke validator 兼容性检查：

`scripts/tl_ocs_experiments/validate_outputs.py --run-dir /tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000 --suite smoke --strict`

结果：

- 返回码：0
- passed checks：197
- warnings：0
- failed checks：0

Patch 5B smoke 验证仍兼容。

## 8. Medium 实际运行

运行命令：

`RUN_ID=patch5c-medium-20260526-000000 scripts/tl_ocs_experiments/run_medium_sanity.sh`

实际 run-id：

`patch5c-medium-20260526-000000`

实际输出目录：

`/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000`

关键输出：

- `/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000/manifest.csv`
- `/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000/validation-report.csv`
- `/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000/combined-summary.csv`
- `/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000/logs/`
- `/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000/csv/`

## 9. Medium 场景和返回码

| 场景 | 目标 | 返回码 |
|---|---|---:|
| `medium__8l4s2h__skewed__static_ocs07__hit__det__seed0` | static OCS 0-7 hit | 0 |
| `medium__8l4s2h__skewed__static_ocs07__admit_fallback__det__seed0` | static OCS 0-7 admission fallback | 0 |
| `medium__8l4s2h__skewed__tl_ocs__fallback__cp_wecmp__seed0` | TL-OCS + control-plane WECMP | 0 |
| `medium__8l4s2h__skewed__tl_ocs__fallback__measured_later__seed0` | TL-OCS + measured later proof | 0 |

## 10. validation-report 摘要

`validation-report.csv` 结果：

- passed checks：134
- warnings：0
- failed checks：0

所有 required checks pass。

每个 medium summary 行均满足：

- `overallResultConsistency=pass`
- `overallAlgorithmInvariant=pass`
- `linkCounterCount=66`
- `linkTimeseriesSampleRows=1914`

Measured later 场景额外满足：

- `measuredWecmpLaterProofPassed=true`
- `measuredWecmpLaterFlowCount=1`
- `measuredWecmpLaterFlowCompletedCount=1`
- `measuredWecmpLaterChangedSelectedSpineCount=1`
- `measuredWecmpLaterOcsLeakageDetected=false`

## 11. combined-summary.csv 摘要

`collect_summary.py` 生成：

`/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000/combined-summary.csv`

包含 4 行 summary，并追加 metadata 字段：

- `sourceFile`
- `suite`
- `scale`
- `traffic`
- `baseline`
- `admission`
- `wecmp`
- `seed`

Baseline row count summary：

| suite | baseline | admission | wecmp | rows |
|---|---|---|---|---:|
| medium | static_ocs07 | admit_fallback | det | 1 |
| medium | static_ocs07 | hit | det | 1 |
| medium | tl_ocs | fallback | cp_wecmp | 1 |
| medium | tl_ocs | fallback | measured_later | 1 |

## 12. Route correctness

关键 `links.csv` 检查：

| 场景 | selected OCS a-to-b | OCS txBytes | EPS spine0->leaf sum |
|---|---|---:|---:|
| static OCS hit | `ocs-leaf0-leaf7-a-to-b` | 577320 | 1207784 |
| static admission fallback | `ocs-leaf0-leaf7-a-to-b` | 0 | 1811676 |
| TL-OCS control-plane WECMP | `ocs-leaf0-leaf3-a-to-b` | 0 | 0 |
| TL-OCS measured later | `ocs-leaf0-leaf3-a-to-b` | 0 | 603892 |

结论：

- static OCS hit 场景有 OCS 0-7 a-to-b Tx；
- static admission fallback 场景 selected OCS 0-7 a-to-b 为 0，并有 EPS deterministic traffic；
- TL-OCS WECMP 场景 selected OCS 0-3 a-to-b 为 0；
- TL-OCS measured later 场景 selected OCS 0-3 a-to-b 为 0，later flow 走 measured selected EPS spine；
- Patch 2.6 route fix 保持。

## 13. Patch 3 link-timeseries 回归

4 个 medium 场景均生成 `link-timeseries.csv`。

验证器检查：

- header 包含 `sampleTimeSeconds`, `deltaTxBytes`, `sampleThroughputMbps`, `utilizationApprox`；
- 数据行数 > 0；
- summary `linkTimeseriesSampleRows=1914`；
- `sampleTimeSeconds` 单调非降；
- `utilizationApprox` 可解析且 `>=0`；
- 至少存在 `deltaTxBytes > 0` 的 sample row。

Patch 3 time-series 导出在 8l4s2h medium-sanity 下保持。

## 14. Patch 4B measured-wecmp 回归

Measured WECMP medium 场景：

- `medium__8l4s2h__skewed__tl_ocs__fallback__measured_later__seed0`

生成 `measured-wecmp.csv`，数据行数为 64：

- 8 leaves
- 4 spines
- 2 directions

验证器检查：

- `hasSample` 字段存在；
- `measuredUtilization` 可解析且 `>=0`。

Patch 4B measured snapshot projection 在 8l4s2h 下保持。

## 15. Patch 4C later proof 回归

Medium measured later proof 场景的 later flow：

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

Patch 4C later new flow selectedSpine change proof 在 medium-sanity 下保持。

## 16. results/raw 检查

Patch 5C 运行未写入仓库历史 `results/raw`。

实现方式：

- runner 从 `<run-dir>/ns3-cwd` 启动 simulator；
- simulator 相对路径 artifact 落在 `<run-dir>/sim/results/raw`；
- structured CSV 写 `<run-dir>/csv`；
- logs 写 `<run-dir>/logs`。

只读检查未发现本轮运行后仓库 `results/raw` 下有新文件。

## 17. 结论

Patch 5C 通过。

本补丁增强了 validator 的 suite / scale 支持，增强了 summary aggregation metadata，新增了 4 场景 medium-sanity runner。Smoke 兼容性验证通过，medium-sanity 4 场景均返回 0，validation 134 项全部通过，combined summary 成功生成。

Patch 2.6 route fix、Patch 3 link-timeseries、Patch 4B measured-wecmp、Patch 4C later proof 均保持。

可以进入 Patch 5D。建议 Patch 5D 聚焦 medium-sanity aggregation/reporting 增强、失败诊断改善，或设计 medium baseline 扩展；仍不应直接进入 paper-candidate sweep。

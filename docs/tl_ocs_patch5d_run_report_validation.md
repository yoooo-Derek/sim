# TL-OCS Patch 5D Run Report Validation

## 1. 目标和边界

Patch 5D 的目标是把已有 smoke / medium 输出转换为更可审计的 run-level report，并增强失败诊断与 baseline comparison readiness。

本任务不是 paper-candidate sweep，不是大规模论文实验，不画最终论文图，不修改 TL-OCS / WECMP / route fix 源码，不修改 Patch 3 / 4B / 4C CSV schema，也不改变既有 CSV 字段语义。

## 2. 修改范围

修改或新增文件：

- `scripts/tl_ocs_experiments/README.md`
- `scripts/tl_ocs_experiments/validate_outputs.py`
- `scripts/tl_ocs_experiments/collect_summary.py`
- `scripts/tl_ocs_experiments/make_run_report.py`
- `scripts/tl_ocs_experiments/run_smoke_matrix.sh`
- `scripts/tl_ocs_experiments/run_medium_sanity.sh`
- `docs/tl_ocs_patch5d_run_report_validation.md`

未修改源码：

- 未修改 `src/main/hybrid-dcn-main.cc`
- 未修改 `src/eps/eps-wecmp-state.h`
- 未修改 TL-OCS 控制算法
- 未修改 OCS admission
- 未修改 EPS fallback / residual
- 未修改 WECMP decision
- 未修改 Patch 2.6 route fix
- 未修改 Patch 3 / 4B / 4C CSV schema

## 3. make_run_report.py

新增 `scripts/tl_ocs_experiments/make_run_report.py`。

输入：

- `--run-dir`
- `--csv-dir` 可选，默认 `run-dir/csv`
- `--manifest` 可选，默认 `run-dir/manifest.csv`
- `--validation-report` 可选，默认 `run-dir/validation-report.csv`
- `--combined-summary` 可选，默认 `run-dir/combined-summary.csv`
- `--baseline-summary` 可选，默认 `run-dir/baseline-summary.csv`
- `--output` 可选，默认 `run-dir/run-report.md`

输出：

- `run-report.md`

报告内容：

- 顶部明确标注 engineering validation，不是 paper result；
- 明确无 paper-candidate sweep；
- 明确无 final figures；
- 明确无 statistical conclusion / confidence interval；
- manifest scenario count、return code、log path；
- validation pass / warn / fail count；
- failed / warning checks 列表；
- baseline rows table；
- key metrics table；
- route correctness summary；
- WECMP summary：
  - measured no-sample fallback observed；
  - measured later proof passed；
  - selectedSpine change observed。

`make_run_report.py` 只使用 Python 标准库，不画图，不生成图片，不计算置信区间，不做论文结论。

## 4. collect_summary.py 增强

保持输出 `combined-summary.csv`，并新增默认输出：

- `baseline-summary.csv`

`combined-summary.csv` 稳定追加 metadata 字段：

- `sourceFile`
- `suite`
- `scale`
- `traffic`
- `baseline`
- `admission`
- `wecmp`
- `seed`

`baseline-summary.csv` 按以下维度聚合：

- `suite`
- `scale`
- `traffic`
- `baseline`
- `admission`
- `wecmp`

输出字段：

- `rowCount`
- `completedFlowCountMean`
- `avgFctSecondsMean`
- `p99FctSecondsMean`
- `aggregateGoodputMbpsMean`
- `ocsByteShareMean`
- `epsFallbackRatioMean`
- `linkTimeseriesSampleRowsMean`

这些只是简单计数和均值，用于 baseline comparison readiness；不计算置信区间，不做统计显著性，不声称哪个 baseline 更优。

## 5. validate_outputs.py 诊断增强

`validation-report.csv` 新增字段：

- `severity`

字段语义：

- `severity=required`：严格必需检查；
- `severity=warning`：非严格告警检查；
- `status=pass|warn|fail` 保持。

诊断增强：

- 文件存在性失败时，message 包含 expected path；
- schema 缺失时，message 列出 missing fields；
- route correctness 失败时，message 包含 selected OCS link id、observed txBytes、expected relation；
- link-timeseries 失败时，message 包含 row count 和 first offending row index；
- measured-wecmp row count 失败时，message 包含 expected 和 actual；
- stdout summary 输出 pass / warn / fail count 和 report path；
- strict 模式不放宽，任一 required fail 仍返回非零。

Patch 5B smoke 和 Patch 5C medium 验证保持兼容。

## 6. Runner 增强

小幅增强：

- `run_smoke_matrix.sh`
- `run_medium_sanity.sh`

场景参数和数量未改变。

增强内容：

- validation 成功后自动调用 `collect_summary.py`；
- collection 成功后自动调用 `make_run_report.py`；
- 打印：
  - `combined-summary.csv`
  - `baseline-summary.csv`
  - `run-report.md`
- report 生成失败时 runner 返回非零。

未加入 paper-candidate，未加入画图。

## 7. 使用的 run-id

复用已有 run，不重新跑仿真场景：

| suite | run-id | 输出目录 |
|---|---|---|
| smoke | `patch5b-smoke-20260526-000000` | `/tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000` |
| medium | `patch5c-medium-20260526-000000` | `/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000` |

## 8. 实际验证命令

Smoke validation：

`scripts/tl_ocs_experiments/validate_outputs.py --run-dir /tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000 --suite smoke --strict`

Medium validation：

`scripts/tl_ocs_experiments/validate_outputs.py --run-dir /tmp/tl-ocs-experiments/patch5c-medium-20260526-000000 --suite medium --scenarios-from-manifest --strict`

Smoke aggregation：

`scripts/tl_ocs_experiments/collect_summary.py --run-dir /tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000`

Medium aggregation:

`scripts/tl_ocs_experiments/collect_summary.py --run-dir /tmp/tl-ocs-experiments/patch5c-medium-20260526-000000`

Smoke report：

`scripts/tl_ocs_experiments/make_run_report.py --run-dir /tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000`

Medium report：

`scripts/tl_ocs_experiments/make_run_report.py --run-dir /tmp/tl-ocs-experiments/patch5c-medium-20260526-000000`

## 9. Smoke validation 摘要

Run:

`/tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000`

结果：

- pass：197
- warn：0
- fail：0
- `validation-report.csv` 存在；
- `severity` 字段存在；
- `combined-summary.csv` 存在，6 行；
- `baseline-summary.csv` 存在，6 行；
- `run-report.md` 存在。

`run-report.md` 摘要：

- scenarioCount：6；
- route checks passed：11；
- route checks with warning/fail：0；
- measured no-sample fallback observed：true；
- measured later proof passed：true；
- selectedSpine change observed：true。

## 10. Medium validation 摘要

Run:

`/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000`

结果：

- pass：134
- warn：0
- fail：0
- `validation-report.csv` 存在；
- `severity` 字段存在；
- `combined-summary.csv` 存在，4 行；
- `baseline-summary.csv` 存在，4 行；
- `run-report.md` 存在。

`run-report.md` 摘要：

- scenarioCount：4；
- route checks passed：7；
- route checks with warning/fail：0；
- measured no-sample fallback observed：false；
- measured later proof passed：true；
- selectedSpine change observed：true。

## 11. baseline-summary.csv 检查

Smoke baseline summary rows：

| suite | scale | rows |
|---|---|---:|
| smoke | 4l2s2h | 6 |

Medium baseline summary rows：

| suite | scale | rows |
|---|---|---:|
| medium | 8l4s2h | 4 |

两个 `baseline-summary.csv` 均包含：

- `rowCount`
- `completedFlowCountMean`
- `avgFctSecondsMean`
- `p99FctSecondsMean`
- `aggregateGoodputMbpsMean`
- `ocsByteShareMean`
- `epsFallbackRatioMean`
- `linkTimeseriesSampleRowsMean`

这些字段仅用于后续 comparison readiness，不作为论文统计结论。

## 12. run-report.md 检查

两个 run 的 `run-report.md` 顶部均包含：

- `This is an engineering validation report, not a paper result.`
- `No paper-candidate sweep was run.`
- `No final figures were generated.`
- `No statistical conclusion or confidence interval is reported.`

报告包含：

- manifest table；
- validation summary；
- failed / warning checks；
- baseline rows table；
- key metrics table；
- route correctness summary；
- WECMP summary。

## 13. Route correctness 回归

Smoke：

- route checks passed：11；
- route checks with warning/fail：0；
- selected OCS hit / fallback / WECMP / measured later checks 均 pass。

Medium：

- route checks passed：7；
- route checks with warning/fail：0；
- static OCS hit / admission fallback / TL-OCS WECMP / measured later checks 均 pass。

Patch 2.6 route fix 保持。

## 14. Patch 3 / 4B / 4C 回归

Patch 3 link-timeseries：

- smoke run：所有场景 time-series checks pass；
- medium run：所有场景 time-series checks pass。

Patch 4B measured-wecmp：

- smoke measured scenarios：measured-wecmp checks pass；
- medium measured later scenario：measured-wecmp checks pass。

Patch 4C later proof：

- smoke measured later：later proof pass，selectedSpine change observed；
- medium measured later：later proof pass，selectedSpine change observed。

## 15. results/raw 检查

Patch 5D 未运行新的仿真场景。

只读检查未发现本轮操作后仓库 `results/raw` 下有新文件。所有生成物写入：

- `/tmp/tl-ocs-experiments/patch5b-smoke-20260526-000000`
- `/tmp/tl-ocs-experiments/patch5c-medium-20260526-000000`

## 16. 结论

Patch 5D 通过。

本补丁新增 run-level engineering report，增强 aggregation 输出，增强 validation 失败诊断，并保持 smoke / medium 验证兼容。Patch 2.6 route fix、Patch 3 link-timeseries、Patch 4B measured-wecmp、Patch 4C later proof 均保持。

可以进入 Patch 5E。建议 Patch 5E 继续围绕实验工程能力，例如失败诊断压力测试、manifest/schema versioning、或 medium baseline 扩展设计；仍禁止直接进入 paper-candidate sweep。

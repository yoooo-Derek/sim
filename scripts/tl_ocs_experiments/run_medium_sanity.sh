#!/usr/bin/env bash
set -u

RUN_ROOT="${RUN_ROOT:-/tmp/tl-ocs-experiments}"
RUN_ID="${RUN_ID:-medium-$(date +%Y%m%d-%H%M%S)}"
RUN_DIR="${RUN_ROOT}/${RUN_ID}"
LOG_DIR="${RUN_DIR}/logs"
CSV_DIR="${RUN_DIR}/csv"
NS3_CWD="${RUN_DIR}/ns3-cwd"
NS3_BIN="${NS3_BIN:-/home/dyn/ns-3.47/build/scratch/ns3.47-hybrid-dcn-main-optimized}"
CONTINUE_ON_ERROR="${CONTINUE_ON_ERROR:-1}"

mkdir -p "${LOG_DIR}" "${CSV_DIR}" "${NS3_CWD}" "${RUN_DIR}/sim/results/raw"

if [ ! -x "${NS3_BIN}" ]; then
    echo "ERROR: NS3_BIN is not executable: ${NS3_BIN}" >&2
    echo "Build first, for example: /home/dyn/ns-3.47/ns3 build" >&2
    exit 2
fi

MANIFEST="${RUN_DIR}/manifest.csv"
REPORT="${RUN_DIR}/validation-report.csv"

csv_escape() {
    local value="${1}"
    value="${value//\"/\"\"}"
    printf '"%s"' "${value}"
}

write_manifest_header() {
    printf 'experimentName,startTime,endTime,returnCode,logPath,command\n' > "${MANIFEST}"
}

append_manifest_row() {
    local name="${1}"
    local start_time="${2}"
    local end_time="${3}"
    local rc="${4}"
    local log_path="${5}"
    local command="${6}"
    {
        csv_escape "${name}"
        printf ','
        csv_escape "${start_time}"
        printf ','
        csv_escape "${end_time}"
        printf ',%s,' "${rc}"
        csv_escape "${log_path}"
        printf ','
        csv_escape "${command}"
        printf '\n'
    } >> "${MANIFEST}"
}

COMMON_ARGS=(
    --numLeaves=8
    --numSpines=4
    --serversPerLeaf=2
    --simTime=3.0
    --enableEcho=false
    --enableBulk=false
    --enableSecondBulk=false
    --enableResidualBulk=false
    --enableMatrixFlows=true
    --trafficMatrixMode=skewed
    --communityMode=louvain
    --louvainMode=single-level
    --eta=1.0
    --communityAlpha=0.5
    --ocsPortK=1
    --maxSelectedOcsLinks=1
    --routeMode=ocs-forced
    --enableStructuredResultExport=true
    "--structuredResultDir=${CSV_DIR}"
    --linkUtilizationSampleInterval=0.1
)

run_scenario() {
    local name="${1}"
    shift
    local log_path="${LOG_DIR}/${name}.log"
    local start_time
    local end_time
    local rc
    local cmd_display

    start_time="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    cmd_display="${NS3_BIN} --experimentName=${name}"
    for arg in "${COMMON_ARGS[@]}" "$@"; do
        cmd_display="${cmd_display} ${arg}"
    done

    (
        cd "${NS3_CWD}" || exit 2
        "${NS3_BIN}" "--experimentName=${name}" "${COMMON_ARGS[@]}" "$@"
    ) > "${log_path}" 2>&1
    rc=$?
    end_time="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

    append_manifest_row "${name}" "${start_time}" "${end_time}" "${rc}" "${log_path}" "${cmd_display}"
    printf '[MEDIUM] %-82s rc=%s\n' "${name}" "${rc}"

    if [ "${rc}" -ne 0 ] && [ "${CONTINUE_ON_ERROR}" != "1" ]; then
        echo "Stopping after failed scenario: ${name}" >&2
        exit "${rc}"
    fi
    return "${rc}"
}

write_manifest_header

overall_rc=0

run_scenario \
    medium__8l4s2h__skewed__static_ocs07__hit__det__seed0 \
    --enableStaticOcs=true \
    --enableMatrixSelect=false \
    --ocsLeafA=0 \
    --ocsLeafB=7 \
    --enableOcsAdmissionControl=false \
    --enableEpsWecmp=false \
    --enableEpsWecmpRouting=false || overall_rc=1

run_scenario \
    medium__8l4s2h__skewed__static_ocs07__admit_fallback__det__seed0 \
    --enableStaticOcs=true \
    --enableMatrixSelect=false \
    --ocsLeafA=0 \
    --ocsLeafB=7 \
    --enableOcsAdmissionControl=true \
    --ocsAdmissionThreshold=20 \
    --matrixFlowDemand=40 \
    --enableEpsWecmp=false \
    --enableEpsWecmpRouting=false || overall_rc=1

run_scenario \
    medium__8l4s2h__skewed__tl_ocs__fallback__cp_wecmp__seed0 \
    --enableStaticOcs=true \
    --enableMatrixSelect=true \
    --selectionMetric=community-excess \
    --enableOcsAdmissionControl=true \
    --ocsAdmissionThreshold=20 \
    --matrixFlowDemand=40 \
    --enableEpsWecmp=true \
    --enableEpsWecmpRouting=true \
    --epsWecmpLoadSource=control-plane \
    --epsWecmpDiagnosticLoadMode=hot-spine \
    --epsWecmpDiagnosticLoad=50 \
    --epsWecmpDiagnosticHotSpine=0 || overall_rc=1

run_scenario \
    medium__8l4s2h__skewed__tl_ocs__fallback__measured_later__seed0 \
    --enableStaticOcs=true \
    --enableMatrixSelect=true \
    --selectionMetric=community-excess \
    --enableOcsAdmissionControl=true \
    --ocsAdmissionThreshold=20 \
    --matrixFlowDemand=40 \
    --enableEpsWecmp=true \
    --enableEpsWecmpRouting=true \
    --epsWecmpLoadSource=measured-snapshot \
    --measuredWecmpNoSampleFallback=control-plane \
    --enableMeasuredWecmpLaterFlowProof=true \
    --measuredWecmpLaterDecisionTime=0.45 \
    --measuredWecmpLaterFlowStart=0.55 \
    --measuredWecmpLaterFlowMaxBytes=524288 \
    --measuredWecmpLaterFlowPort=13000 \
    --measuredWecmpLaterSrcLeaf=0 \
    --measuredWecmpLaterDstLeaf=7 \
    --measuredWecmpLaterSrcServer=1 \
    --measuredWecmpLaterDstServer=1 \
    --epsWecmpDiagnosticLoadMode=hot-spine \
    --epsWecmpDiagnosticLoad=50 \
    --epsWecmpDiagnosticHotSpine=0 || overall_rc=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"${SCRIPT_DIR}/validate_outputs.py" \
    --run-dir "${RUN_DIR}" \
    --suite medium \
    --scenarios-from-manifest \
    --strict
validation_rc=$?
if [ "${validation_rc}" -ne 0 ]; then
    overall_rc=1
fi

if [ "${validation_rc}" -eq 0 ]; then
    "${SCRIPT_DIR}/collect_summary.py" --run-dir "${RUN_DIR}"
    collect_rc=$?
    if [ "${collect_rc}" -ne 0 ]; then
        overall_rc=1
    fi
fi

echo "Run directory: ${RUN_DIR}"
echo "Manifest: ${MANIFEST}"
echo "Validation report: ${REPORT}"
echo "Combined summary: ${RUN_DIR}/combined-summary.csv"

exit "${overall_rc}"

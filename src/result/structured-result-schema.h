#ifndef SIM_SRC_RESULT_STRUCTURED_RESULT_SCHEMA_H
#define SIM_SRC_RESULT_STRUCTURED_RESULT_SCHEMA_H

#include <string>
#include <vector>

inline const std::vector<std::string>&
SummaryCsvHeader()
{
    static const std::vector<std::string> header = {"experimentName",
                                                    "presetScenario",
                                                    "trafficMatrixMode",
                                                    "trafficMatrixSource",
                                                    "enableEwmaSmoothing",
                                                    "trafficGraphThreshold",
                                                    "configScoreMode",
                                                    "selectionMetric",
                                                    "communityMode",
                                                    "activeCommunityCount",
                                                    "modularityQ",
                                                    "selectedOcsEdges",
                                                    "candidateConfigScore",
                                                    "previousConfigScore",
                                                    "configScoreImprovement",
                                                    "ocsCoveredFlowCount",
                                                    "epsResidualFlowCount",
                                                    "fallbackFlowCount",
                                                    "wecmpFrozenFlowCount",
                                                    "ocsTxBytes",
                                                    "epsTxBytes",
                                                    "ocsObservedUse",
                                                    "epsObservedUse",
                                                    "matrixFlowCount",
                                                    "completedFlows",
                                                    "completionRatio",
                                                    "avgGoodputMbps",
                                                    "overallResultConsistency",
                                                    "overallAlgorithmInvariant",
                                                    "completedFlowCount",
                                                    "totalRxBytes",
                                                    "aggregateGoodputMbps",
                                                    "avgFctSeconds",
                                                    "p99FctSeconds",
                                                    "ocsByteShare",
                                                    "ocsHitRatio",
                                                    "epsFallbackRatio",
                                                    "residualFlowRatio",
                                                    "linkCounterCount",
                                                    "ocsLinkCounterCount",
                                                    "epsLinkCounterCount",
                                                    "maxOcsLinkUtilizationApprox",
                                                    "maxEpsLinkUtilizationApprox",
                                                    "avgEpsLinkUtilizationApprox",
                                                    "epsLinkUtilizationStddevApprox",
                                                    "linkUtilizationSampleIntervalSeconds",
                                                    "linkTimeseriesEnabled",
                                                    "linkTimeseriesSampleRows",
                                                    "linkTimeseriesNonzeroSampleRows",
                                                    "linkTimeseriesMaxUtilizationApprox",
                                                    "linkTimeseriesAvgUtilizationApprox",
                                                    "linkTimeseriesMaxEpsUtilizationApprox",
                                                    "linkTimeseriesMaxOcsUtilizationApprox",
                                                    "epsWecmpLoadSource",
                                                    "measuredWecmpEnabled",
                                                    "measuredWecmpNoSampleFallback",
                                                    "measuredWecmpWarmupTime",
                                                    "measuredWecmpSnapshotCount",
                                                    "measuredWecmpSnapshotUpdateCount",
                                                    "measuredWecmpDecisionCount",
                                                    "measuredWecmpFallbackDecisionCount",
                                                    "measuredWecmpNoSampleDecisionCount",
                                                    "measuredWecmpCandidateRowsWithSample",
                                                    "measuredWecmpCandidateRowsWithoutSample",
                                                    "measuredWecmpChangedSelectedSpineCount",
                                                    "measuredWecmpLaterFlowProofEnabled",
                                                    "measuredWecmpLaterDecisionTime",
                                                    "measuredWecmpLaterFlowStart",
                                                    "measuredWecmpLaterFlowCount",
                                                    "measuredWecmpLaterFlowCompletedCount",
                                                    "measuredWecmpLaterDecisionCount",
                                                    "measuredWecmpLaterChangedSelectedSpineCount",
                                                    "measuredWecmpLaterRouteInstalled",
                                                    "measuredWecmpLaterOcsLeakageDetected",
                                                    "measuredWecmpLaterProofPassed"};
    return header;
}

inline const std::vector<std::string>&
OcsCandidatesCsvHeader()
{
    static const std::vector<std::string> header = {"candidateIndex",
                                                    "leafA",
                                                    "leafB",
                                                    "traffic",
                                                    "expected",
                                                    "modularityGain",
                                                    "utility",
                                                    "communityFactor",
                                                    "stateHoldingGain",
                                                    "selectionScore",
                                                    "selected",
                                                    "rejectReason"};
    return header;
}

inline const std::vector<std::string>&
WecmpCsvHeader()
{
    static const std::vector<std::string> header = {"decisionIndex",
                                                    "srcLeaf",
                                                    "dstLeaf",
                                                    "residualDemand",
                                                    "selectedSpine",
                                                    "spineIndex",
                                                    "pathLoadMetric",
                                                    "candidatePathLoad",
                                                    "attractiveness",
                                                    "normalizedAttractiveness",
                                                    "targetProbability",
                                                    "previousProbability",
                                                    "updatedProbability",
                                                    "probabilityDelta",
                                                    "boundedProbabilityDelta",
                                                    "loadSource",
                                                    "hasMeasuredSample",
                                                    "measuredSrcToSpineUtilization",
                                                    "measuredSpineToDstUtilization",
                                                    "measuredPathUtilization",
                                                    "controlPlanePathLoadMetric",
                                                    "effectivePathLoadMetric",
                                                    "noSampleFallbackMode",
                                                    "measuredDecisionRequested",
                                                    "measuredDecisionUsed",
                                                    "measuredDecisionFallback",
                                                    "measuredNoSample",
                                                    "decisionTimeSeconds",
                                                    "appliesToLaterFlow",
                                                    "controlPlaneSelectedSpine"};
    return header;
}

inline const std::vector<std::string>&
FlowsCsvHeader()
{
    static const std::vector<std::string> header = {"flowIndex",
                                                    "name",
                                                    "srcLeaf",
                                                    "dstLeaf",
                                                    "rawDemand",
                                                    "controlDemand",
                                                    "ocsPairInstalled",
                                                    "admissionMode",
                                                    "ocsAdmitted",
                                                    "ocsCovered",
                                                    "fallbackToEps",
                                                    "fallbackDataPlaneMode",
                                                    "fallbackEventMapped",
                                                    "fallbackMappingType",
                                                    "plannedResidualDemand",
                                                    "realResidualDemand",
                                                    "wecmpResidualDemand",
                                                    "requiresEpsResidualPath",
                                                    "residualPathReason",
                                                    "epsPathFrozen",
                                                    "frozenSpine",
                                                    "packetSinkPort",
                                                    "startTime",
                                                    "expectedBytes",
                                                    "rxBytes",
                                                    "completed",
                                                    "completionRatio",
                                                    "firstRx",
                                                    "lastRx",
                                                    "goodputMbps",
                                                    "pathType",
                                                    "fctSeconds",
                                                    "rxDurationSeconds",
                                                    "isMeasuredLaterProofFlow",
                                                    "measuredLaterDecisionTime",
                                                    "measuredLaterDecisionIndex",
                                                    "measuredLaterSelectedSpine",
                                                    "measuredLaterControlPlaneSelectedSpine",
                                                    "measuredLaterSelectedSpineChanged",
                                                    "measuredLaterHasMeasuredSample",
                                                    "measuredLaterAppliesToLaterFlow"};
    return header;
}

inline const std::vector<std::string>&
LinksCsvHeader()
{
    static const std::vector<std::string> header = {"linkIndex",
                                                    "linkId",
                                                    "linkType",
                                                    "direction",
                                                    "endpointAType",
                                                    "endpointA",
                                                    "endpointBType",
                                                    "endpointB",
                                                    "capacityGbps",
                                                    "delay",
                                                    "txPackets",
                                                    "txBytes",
                                                    "utilizationApprox",
                                                    "note"};
    return header;
}

inline const std::vector<std::string>&
LinkTimeseriesCsvHeader()
{
    static const std::vector<std::string> header = {"experimentName",
                                                    "sampleIndex",
                                                    "sampleTimeSeconds",
                                                    "intervalSeconds",
                                                    "linkType",
                                                    "linkId",
                                                    "direction",
                                                    "srcNode",
                                                    "dstNode",
                                                    "capacityMbps",
                                                    "deltaTxPackets",
                                                    "deltaTxBytes",
                                                    "cumulativeTxPackets",
                                                    "cumulativeTxBytes",
                                                    "sampleThroughputMbps",
                                                    "utilizationApprox"};
    return header;
}

inline const std::vector<std::string>&
MeasuredWecmpCsvHeader()
{
    static const std::vector<std::string> header = {"experimentName",
                                                    "sampleTimeSeconds",
                                                    "leaf",
                                                    "spine",
                                                    "direction",
                                                    "capacityMbps",
                                                    "deltaTxBytes",
                                                    "cumulativeTxBytes",
                                                    "sampleThroughputMbps",
                                                    "measuredUtilization",
                                                    "hasSample"};
    return header;
}

#endif

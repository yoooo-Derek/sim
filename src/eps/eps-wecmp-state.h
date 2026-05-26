#ifndef HYBRID_DCN_EPS_WECMP_STATE_H
#define HYBRID_DCN_EPS_WECMP_STATE_H

#include <cstdint>
#include <string>
#include <vector>

struct EpsWecmpLinkState
{
    uint32_t spineIndex;
    // Current implementation stores control-plane estimated residual load here,
    // not ns-3 measured per-link bytes.
    double observedTraffic;
    double utilization;
    double smoothedUtilization;
    double attractiveness;
    double normalizedAttractiveness;
    double targetProbability;
    double previousProbability;
    double updatedProbability;
    double probabilityDelta;
    double boundedProbabilityDelta;
    double pathLoadMetric;
    double candidatePathLoad;
    bool hasMeasuredSample = false;
    double measuredSrcToSpineUtilization = 0.0;
    double measuredSpineToDstUtilization = 0.0;
    double measuredPathUtilization = 0.0;
    double controlPlanePathLoadMetric = 0.0;
    double effectivePathLoadMetric = 0.0;
};

struct EpsWecmpDecision
{
    bool enabled;
    uint32_t srcLeaf;
    uint32_t dstLeaf;
    double residualDemand;
    uint32_t selectedSpine;
    std::vector<EpsWecmpLinkState> linkStates;
    std::string loadSource = "control-plane";
    std::string noSampleFallbackMode = "control-plane";
    double decisionTimeSeconds = 0.0;
    bool measuredDecisionRequested = false;
    bool measuredDecisionUsed = false;
    bool measuredDecisionFallback = false;
    bool measuredNoSample = false;
    bool appliesToLaterFlow = false;
    uint32_t controlPlaneSelectedSpine = 0;
};

struct EpsWecmpPairState
{
    uint32_t srcLeaf;
    uint32_t dstLeaf;
    std::vector<double> probabilities;
    std::vector<double> smoothedUtilizations;
    bool initialized;
};

struct EpsPhysicalLinkState
{
    uint32_t leafIndex;
    uint32_t spineIndex;
    // Current implementation stores control-plane estimated residual load here,
    // not ns-3 measured per-link bytes.
    double observedTraffic;
    double utilization;
    double smoothedUtilization;
};

struct EpsWecmpEpochSummary
{
    uint32_t epoch;
    std::string trafficMatrixMode;
    uint32_t residualPairs;
    uint32_t updatedPairs;
    double totalPlannedResidualDemand;
    double totalRealResidualDemand;
};

#endif // HYBRID_DCN_EPS_WECMP_STATE_H

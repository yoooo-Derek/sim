#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/network-module.h"
#include "ns3/netanim-module.h"
#include "ns3/point-to-point-module.h"

#if __has_include("../eps/eps-wecmp-state.h")
#include "../eps/eps-wecmp-state.h"
#include "../metrics/trace-metrics.h"
#include "../model/louvain.h"
#include "../ocs/ocs-state.h"
#include "../result/csv-utils.h"
#include "../result/structured-result-paths.h"
#include "../result/structured-result-schema.h"
#include "../traffic/traffic-matrix.h"
#else
#include "../../sim/src/eps/eps-wecmp-state.h"
#include "../../sim/src/metrics/trace-metrics.h"
#include "../../sim/src/model/louvain.h"
#include "../../sim/src/ocs/ocs-state.h"
#include "../../sim/src/result/csv-utils.h"
#include "../../sim/src/result/structured-result-paths.h"
#include "../../sim/src/result/structured-result-schema.h"
#include "../../sim/src/traffic/traffic-matrix.h"
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace ns3;

static void
RegisterDetailedTraceCommandLineOptions(CommandLine& cmd,
                                        bool& enableDetailedAlgorithmTrace,
                                        uint32_t& detailedCandidateLogLimit,
                                        bool& enableDetailedFlowTrace,
                                        uint32_t& detailedFlowLogLimit)
{
    cmd.AddValue("enableDetailedAlgorithmTrace",
                 "Enable detailed Louvain and OCS candidate diagnostics.",
                 enableDetailedAlgorithmTrace);
    cmd.AddValue("detailedCandidateLogLimit",
                 "Maximum number of detailed candidate diagnostics to print.",
                 detailedCandidateLogLimit);
    cmd.AddValue("enableDetailedFlowTrace",
                 "Enable detailed matrix-flow, admission, and path diagnostics.",
                 enableDetailedFlowTrace);
    cmd.AddValue("detailedFlowLogLimit",
                 "Maximum number of detailed flow diagnostics to print.",
                 detailedFlowLogLimit);
}

static std::string
FormatCommunityLabelVector(const std::vector<uint32_t>& labels)
{
    if (labels.empty())
    {
        return std::string("none");
    }
    std::ostringstream stream;
    for (uint32_t labelIndex = 0; labelIndex < labels.size(); ++labelIndex)
    {
        if (labelIndex > 0)
        {
            stream << ",";
        }
        stream << labelIndex << ":" << labels[labelIndex];
    }
    return stream.str();
}

static double
ComputeNearestRankP99(std::vector<double> samples)
{
    if (samples.empty())
    {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    std::size_t index =
        static_cast<std::size_t>(std::ceil(0.99 * static_cast<double>(samples.size())));
    if (index == 0)
    {
        index = 1;
    }
    index -= 1;
    if (index >= samples.size())
    {
        index = samples.size() - 1;
    }
    return samples[index];
}

static std::ofstream
OpenStructuredResultCsv(const std::string& path, bool& exportSuccess)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        exportSuccess = false;
        std::cout << "[HYBRID-DCN][EXPORT] error = failed-to-open:" << path << std::endl;
    }
    return file;
}

struct OcsInstalledLink
{
    uint32_t leafA;
    uint32_t leafB;
    Ipv4Address leafAAddress;
    Ipv4Address leafBAddress;
    uint32_t leafAIfIndex;
    uint32_t leafBIfIndex;
    NetDeviceContainer devices;
    double traffic;
    double utility;
};

struct MatrixBulkFlowSpec
{
    uint32_t srcLeaf;
    uint32_t dstLeaf;
    uint32_t srcServer;
    uint32_t dstServer;
    uint16_t port;
    bool ocsCovered;
    bool ocsPairAvailable;
    bool ocsAdmitted;
    bool epsFallback;
    double estimatedDemand;
    double ocsLoadBefore;
    double ocsLoadAfter;
    double plannedResidualDemand;
    double realResidualDemand;
    double wecmpResidualDemand;
    bool epsPathFrozen;
    int32_t frozenSpine;
    std::string name;
    std::string fallbackDataPlaneMode = "none";
    bool fallbackEventMapped = false;
    std::string fallbackMappingType = "none";
    double startTime = 0.0;
    uint64_t expectedBytes = 0;
    bool requiresEpsResidualPath = false;
    std::string residualPathReason = "none";
    bool isMeasuredLaterProofFlow = false;
    double measuredLaterDecisionTime = 0.0;
    int32_t measuredLaterDecisionIndex = -1;
    int32_t measuredLaterSelectedSpine = -1;
    int32_t measuredLaterControlPlaneSelectedSpine = -1;
    bool measuredLaterSelectedSpineChanged = false;
    bool measuredLaterHasMeasuredSample = false;
    bool measuredLaterAppliesToLaterFlow = false;
    bool measuredLaterRouteInstalled = false;
};

struct OcsAdmissionEvent
{
    uint32_t srcLeaf;
    uint32_t dstLeaf;
    bool ocsPairAvailable;
    bool ocsAdmitted;
    bool epsFallback;
    double estimatedDemand;
    double ocsLoadBefore;
    double ocsLoadAfter;
    double plannedResidualDemand;
    double realResidualDemand;
    double wecmpResidualDemand;
};

struct MeasuredWecmpPathLoad
{
    bool hasSample;
    double srcToSpineUtilization;
    double spineToDstUtilization;
    double pathUtilization;
};

struct ControlEpochSummary
{
    uint32_t epoch;
    std::string trafficMatrixMode;
    uint32_t communityCount;
    uint32_t candidateEdges;
    uint32_t candidateOcsEdges;
    uint32_t previousOcsEdges;
    uint32_t selectedOcsEdges;
    uint32_t hardHoldEdges;
    double candidateConfigScore;
    double previousConfigScore;
    double configScoreImprovement;
    double candidateRawUtilitySum;
    double previousRawUtilitySum;
    double candidateSelectionScoreSum;
    double previousSelectionScoreSum;
    uint32_t candidateChangedEdges;
    uint32_t previousChangedEdges;
    std::string configScoreMode;
    bool holdTimeActive;
    std::string decision;
    uint32_t previousConfigAgeBefore;
    uint32_t previousConfigAgeAfter;
    uint32_t minSelectedEdgeAge;
    uint32_t maxSelectedEdgeAge;
    uint32_t minHoldEdgeAge;
    uint32_t maxHoldEdgeAge;
};

struct OcsControllerDecision
{
    std::vector<OcsCandidateEdge> candidateEdges;
    std::vector<OcsCandidateEdge> candidateOcsEdges;
    std::vector<OcsCandidateEdge> previousOcsEdges;
    std::vector<OcsCandidateEdge> selectedOcsEdges;
    std::vector<OcsCandidateEdge> holdOcsEdges;
    OcsEdgeAgeMatrix previousOcsEdgeAges;
    OcsEdgeAgeMatrix selectedOcsEdgeAges;
    LouvainResult louvain;
    CommunityPreview communityPreview;
    std::vector<uint32_t> communityLabels;
    uint32_t communityCount;
    double candidateConfigScore;
    double previousConfigScore;
    double configScoreImprovement;
    double candidateRawUtilitySum;
    double previousRawUtilitySum;
    double candidateSelectionScoreSum;
    double previousSelectionScoreSum;
    uint32_t candidateChangedEdges;
    uint32_t previousChangedEdges;
    bool holdTimeActive;
    std::string decision;
    uint32_t selectedConfigAge;
    ControlEpochSummary summary;
    bool success;
};

struct MatrixEpochSummary
{
    uint32_t epoch;
    std::string rawTrafficMatrixMode;
    std::string matrixUsedForControl;
    bool ewmaEnabled;
    double ewmaBeta;
    double rawTotalTraffic;
    double controlTotalTraffic;
    double rawTraffic03;
    double controlTraffic03;
    double rawTraffic12;
    double controlTraffic12;
};

struct EpsWecmpRouteBinding
{
    uint32_t flowIndex;
    uint32_t srcLeaf;
    uint32_t dstLeaf;
    uint32_t selectedSpine;
    Ipv4Address srcServerAddress;
    Ipv4Address dstServerAddress;
    bool installed;
    bool pathFrozen;
    double residualDemand;
};

uint64_t g_bulkRxBytes = 0;
bool g_bulkSeenFirstRx = false;
double g_bulkFirstRxTime = 0.0;
double g_bulkLastRxTime = 0.0;
uint64_t g_ocsTxPackets = 0;
uint64_t g_ocsTxBytes = 0;
uint64_t g_epsTxPackets = 0;
uint64_t g_epsTxBytes = 0;
uint64_t g_residualBulkRxBytes = 0;
bool g_residualBulkSeenFirstRx = false;
double g_residualBulkFirstRxTime = 0.0;
double g_residualBulkLastRxTime = 0.0;
uint64_t g_secondBulkRxBytes = 0;
bool g_secondBulkSeenFirstRx = false;
double g_secondBulkFirstRxTime = 0.0;
double g_secondBulkLastRxTime = 0.0;

std::string
FormatIpv4Endpoint(const Address& address)
{
    if (!InetSocketAddress::IsMatchingType(address))
    {
        return "non-IPv4-address";
    }

    InetSocketAddress endpoint = InetSocketAddress::ConvertFrom(address);
    std::ostringstream stream;
    stream << endpoint.GetIpv4() << " port " << endpoint.GetPort();
    return stream.str();
}

void
BulkSinkRxTrace(Ptr<const Packet> packet, const Address& from)
{
    (void)from;
    g_bulkRxBytes += packet->GetSize();

    const double now = Simulator::Now().GetSeconds();
    if (!g_bulkSeenFirstRx)
    {
        g_bulkSeenFirstRx = true;
        g_bulkFirstRxTime = now;
    }
    g_bulkLastRxTime = now;
}

void
OcsTxTrace(Ptr<const Packet> packet)
{
    g_ocsTxPackets += 1;
    g_ocsTxBytes += packet->GetSize();
}

void
EpsTxTrace(Ptr<const Packet> packet)
{
    g_epsTxPackets += 1;
    g_epsTxBytes += packet->GetSize();
}

void
ResidualBulkSinkRxTrace(Ptr<const Packet> packet, const Address& from)
{
    (void)from;
    g_residualBulkRxBytes += packet->GetSize();

    const double now = Simulator::Now().GetSeconds();
    if (!g_residualBulkSeenFirstRx)
    {
        g_residualBulkSeenFirstRx = true;
        g_residualBulkFirstRxTime = now;
    }
    g_residualBulkLastRxTime = now;
}

void
SecondBulkSinkRxTrace(Ptr<const Packet> packet, const Address& from)
{
    (void)from;
    g_secondBulkRxBytes += packet->GetSize();

    const double now = Simulator::Now().GetSeconds();
    if (!g_secondBulkSeenFirstRx)
    {
        g_secondBulkSeenFirstRx = true;
        g_secondBulkFirstRxTime = now;
    }
    g_secondBulkLastRxTime = now;
}

void
EchoClientTxTrace(Ptr<const Packet> packet, const Address& from, const Address& to)
{
    (void)from;
    std::cout << "UdpEchoClientApplication: At time " << Simulator::Now().GetSeconds()
              << "s client sent " << packet->GetSize() << " bytes to " << FormatIpv4Endpoint(to)
              << std::endl;
}

void
EchoClientRxTrace(Ptr<const Packet> packet, const Address& from, const Address& to)
{
    (void)to;
    std::cout << "UdpEchoClientApplication: At time " << Simulator::Now().GetSeconds()
              << "s client received " << packet->GetSize() << " bytes from "
              << FormatIpv4Endpoint(from) << std::endl;
}

void
EchoServerRxTrace(Ptr<const Packet> packet, const Address& from, const Address& to)
{
    (void)to;
    std::cout << "UdpEchoServerApplication: At time " << Simulator::Now().GetSeconds()
              << "s server received " << packet->GetSize() << " bytes from "
              << FormatIpv4Endpoint(from) << std::endl;
}

Ipv4InterfaceContainer
AddOcsLink(Ptr<Node> leafA,
           Ptr<Node> leafB,
           const std::string& dataRate,
           const std::string& delay,
           Ipv4AddressHelper& ipv4,
           NetDeviceContainer& ocsDevices)
{
    // Stage-4 models OCS as a static Leaf-To-Leaf L1 lightpath.
    // No Optical Core node is created here. Later stages can call this interface
    // from graph clustering and cross-scale scheduling modules to install dynamic
    // optical circuits.
    PointToPointHelper ocsP2p;
    ocsP2p.SetDeviceAttribute("DataRate", StringValue(dataRate));
    ocsP2p.SetChannelAttribute("Delay", StringValue(delay));

    NodeContainer pair(leafA, leafB);
    ocsDevices = ocsP2p.Install(pair);
    Ipv4InterfaceContainer interfaces = ipv4.Assign(ocsDevices);
    ipv4.NewNetwork();
    return interfaces;
}

int
main(int argc, char* argv[])
{
    double simTime = 1.0;
    std::string experimentName = "stage-1-topology";
    std::string presetScenario = "manual";
    std::string presetOverrideMode = "preset-wins";
    uint32_t numSpines = 2;
    uint32_t numLeaves = 4;
    uint32_t serversPerLeaf = 4;
    bool enableEcho = true;
    uint32_t echoPacketSize = 1024;
    double echoInterval = 0.1;
    uint32_t echoCount = 5;
    bool enableBulk = true;
    uint64_t bulkMaxBytes = 1048576;
    double bulkStart = 0.2;
    uint16_t bulkPort = 10000;
    bool enableSecondBulk = true;
    uint16_t secondBulkPort = 10002;
    uint64_t secondBulkMaxBytes = 1048576;
    double secondBulkStart = 0.3;
    bool enableResidualBulk = true;
    uint16_t residualBulkPort = 10001;
    uint64_t residualBulkMaxBytes = 1048576;
    double residualBulkStart = 0.25;
    bool enableStaticOcs = true;
    uint32_t ocsLeafA = 0;
    uint32_t ocsLeafB = 3;
    std::string ocsDataRate = "100Gbps";
    std::string ocsDelay = "5us";
    std::string routeMode = "global";
    bool enableMatrixSelect = true;
    std::string trafficMatrixMode = "skewed";
    bool enableEwmaSmoothing = false;
    double ewmaBeta = 0.7;
    std::string trafficMatrixSource = "synthetic";
    double trafficGraphThreshold = 0.0;
    std::string communityMode = "preview";
    std::string louvainMode = "single-level";
    uint32_t louvainMaxPasses = 10;
    uint32_t louvainMaxLevels = 10;
    double louvainEpsilon = 1e-9;
    bool enableMultiPeriodControl = false;
    uint32_t controlEpochs = 3;
    std::string trafficMatrixSequence = "static";
    std::string selectionMetric = "excess";
    double eta = 1.0;
    double communityAlpha = 0.5;
    bool enableStateHolding = false;
    double stateHoldingLambda = 0.0;
    std::string previousOcsMode = "none";
    uint32_t previousOcsLeafA = 0;
    uint32_t previousOcsLeafB = 1;
    bool enableConfigUpdateGate = false;
    double configUpdateThreshold = 0.0;
    std::string configScoreMode = "selection-score-sum";
    double reconfigurationPenalty = 5.0;
    bool enableHoldTimeGate = false;
    uint32_t minHoldCycles = 1;
    uint32_t previousConfigAge = 1;
    uint32_t ocsPortK = 1;
    uint32_t maxSelectedOcsLinks = 1;
    bool enableMatrixFlows = true;
    uint64_t matrixFlowMaxBytes = 524288;
    double matrixFlowStart = 0.35;
    uint16_t matrixFlowPortBase = 11000;
    uint64_t matrixFlowRxBytesTolerance = 65536;
    bool enableOcsAdmissionControl = false;
    double ocsAdmissionThreshold = 100.0;
    double matrixFlowDemand = 40.0;
    bool enableEpsWecmp = false;
    double epsWecmpRho = 0.7;
    double epsWecmpGamma = 2.0;
    double epsWecmpEpsilon = 1e-6;
    double epsWecmpKappa = 0.5;
    double epsWecmpMaxDelta = 0.25;
    uint32_t epsWecmpEpochs = 3;
    double epsWecmpCapacity = 100.0;
    std::string epsWecmpPathMetric = "max";
    std::string epsWecmpLoadSource = "control-plane";
    std::string measuredWecmpNoSampleFallback = "control-plane";
    double measuredWecmpWarmupTime = 0.0;
    bool enableMeasuredWecmpLaterFlowProof = false;
    double measuredWecmpLaterDecisionTime = 0.8;
    double measuredWecmpLaterFlowStart = 0.9;
    uint64_t measuredWecmpLaterFlowMaxBytes = 524288;
    uint16_t measuredWecmpLaterFlowPort = 13000;
    uint32_t measuredWecmpLaterSrcLeaf = 0;
    uint32_t measuredWecmpLaterDstLeaf = 3;
    uint32_t measuredWecmpLaterSrcServer = 1;
    uint32_t measuredWecmpLaterDstServer = 1;
    std::string epsWecmpDiagnosticLoadMode = "none";
    double epsWecmpDiagnosticLoad = 0.0;
    uint32_t epsWecmpDiagnosticHotSpine = 0;
    bool enableEpsWecmpRouting = false;
    bool enableMultiPeriodWecmpState = true;
    bool enableAlgorithmInvariantCheck = true;
    bool strictAlgorithmInvariantCheck = false;
    bool enableDetailedAlgorithmTrace = false;
    uint32_t detailedCandidateLogLimit = 20;
    bool enableDetailedFlowTrace = false;
    uint32_t detailedFlowLogLimit = 50;
    bool enableStructuredResultExport = false;
    std::string structuredResultDir = "../sim/results/raw";
    double linkUtilizationSampleInterval = 0.1;
    bool enableResultValidation = true;
    std::string validationMode = "warn";

    const uint32_t bulkSrcLeaf = 0;
    const uint32_t bulkSrcServer = 0;
    const uint32_t bulkDstLeaf = 3;
    const uint32_t bulkDstServer = 0;
    const uint32_t secondBulkSrcLeaf = 1;
    const uint32_t secondBulkSrcServer = 0;
    const uint32_t secondBulkDstLeaf = 2;
    const uint32_t secondBulkDstServer = 0;
    const uint32_t residualBulkSrcLeaf = 0;
    const uint32_t residualBulkSrcServer = 1;
    const uint32_t residualBulkDstLeaf = 1;
    const uint32_t residualBulkDstServer = 0;

    std::vector<std::string> explicitArgNames;
    auto addExplicitArgName = [&explicitArgNames](const std::string& name) {
        if (!name.empty() &&
            std::find(explicitArgNames.begin(), explicitArgNames.end(), name) ==
                explicitArgNames.end())
        {
            explicitArgNames.push_back(name);
        }
    };

    auto isControlAffectingArg = [](const std::string& name) {
        const std::vector<std::string> controlArgs = {
            "trafficMatrixMode",
            "communityMode",
            "louvainMode",
            "louvainMaxPasses",
            "louvainMaxLevels",
            "louvainEpsilon",
            "enableMultiPeriodControl",
            "controlEpochs",
            "trafficMatrixSequence",
            "selectionMetric",
            "eta",
            "communityAlpha",
            "enableStateHolding",
            "stateHoldingLambda",
            "previousOcsMode",
            "previousOcsLeafA",
            "previousOcsLeafB",
            "enableConfigUpdateGate",
            "configUpdateThreshold",
            "configScoreMode",
            "reconfigurationPenalty",
            "enableHoldTimeGate",
            "minHoldCycles",
            "previousConfigAge",
            "ocsPortK",
            "maxSelectedOcsLinks",
            "enableMatrixSelect",
            "enableStaticOcs",
            "routeMode",
            "enableMatrixFlows",
            "matrixFlowMaxBytes",
            "matrixFlowStart",
            "matrixFlowPortBase",
            "matrixFlowRxBytesTolerance",
            "enableEwmaSmoothing",
            "ewmaBeta",
            "trafficMatrixSource",
            "trafficGraphThreshold",
            "enableOcsAdmissionControl",
            "ocsAdmissionThreshold",
            "matrixFlowDemand",
            "enableEpsWecmp",
            "epsWecmpRho",
            "epsWecmpGamma",
            "epsWecmpEpsilon",
            "epsWecmpKappa",
            "epsWecmpMaxDelta",
            "epsWecmpEpochs",
            "epsWecmpCapacity",
            "epsWecmpLoadSource",
            "measuredWecmpNoSampleFallback",
            "measuredWecmpWarmupTime",
            "enableMeasuredWecmpLaterFlowProof",
            "measuredWecmpLaterDecisionTime",
            "measuredWecmpLaterFlowStart",
            "measuredWecmpLaterFlowMaxBytes",
            "measuredWecmpLaterFlowPort",
            "measuredWecmpLaterSrcLeaf",
            "measuredWecmpLaterDstLeaf",
            "measuredWecmpLaterSrcServer",
            "measuredWecmpLaterDstServer",
            "epsWecmpDiagnosticLoadMode",
            "epsWecmpDiagnosticLoad",
            "epsWecmpDiagnosticHotSpine",
            "enableMultiPeriodWecmpState",
            "epsWecmpPathMetric",
            "enableEpsWecmpRouting",
            "enableAlgorithmInvariantCheck",
            "strictAlgorithmInvariantCheck",
            "enableDetailedAlgorithmTrace",
            "detailedCandidateLogLimit",
            "enableDetailedFlowTrace",
            "detailedFlowLogLimit",
            "numLeaves",
            "serversPerLeaf",
            "numSpines",
            "ocsDataRate",
            "ocsDelay"};
        return std::find(controlArgs.begin(), controlArgs.end(), name) != controlArgs.end();
    };

    for (int argIndex = 1; argIndex < argc; ++argIndex)
    {
        std::string arg(argv[argIndex]);
        if (arg.rfind("--", 0) != 0)
        {
            continue;
        }

        std::string name = arg.substr(2);
        const std::size_t equalsPos = name.find('=');
        if (equalsPos != std::string::npos)
        {
            name = name.substr(0, equalsPos);
        }
        addExplicitArgName(name);
    }

    std::vector<std::string> explicitControlArgNames;
    for (const auto& name : explicitArgNames)
    {
        if (isControlAffectingArg(name) &&
            std::find(explicitControlArgNames.begin(), explicitControlArgNames.end(), name) ==
                explicitControlArgNames.end())
        {
            explicitControlArgNames.push_back(name);
        }
    }

    std::vector<std::string> normalizedArgs;
    normalizedArgs.reserve(static_cast<std::size_t>(argc));
    for (int argIndex = 0; argIndex < argc; ++argIndex)
    {
        std::string arg(argv[argIndex]);
        if (argIndex > 0 && arg.rfind("--", 0) == 0 &&
            arg.find('=') == std::string::npos && argIndex + 1 < argc)
        {
            std::string nextArg(argv[argIndex + 1]);
            if (nextArg.rfind("--", 0) != 0)
            {
                normalizedArgs.push_back(arg + "=" + nextArg);
                ++argIndex;
                continue;
            }
        }

        normalizedArgs.push_back(arg);
    }

    std::vector<char*> normalizedArgv;
    normalizedArgv.reserve(normalizedArgs.size());
    for (std::string& arg : normalizedArgs)
    {
        normalizedArgv.push_back(const_cast<char*>(arg.c_str()));
    }

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation time in seconds.", simTime);
    cmd.AddValue("experimentName", "Experiment name.", experimentName);
    cmd.AddValue("presetScenario",
                 "Experiment preset: manual, baseline-excess, community-aware, state-aware, config-gated, hold-gated, full-control, or full-stack-control.",
                 presetScenario);
    cmd.AddValue("presetOverrideMode",
                 "Preset override mode: preset-wins or explicit-wins.",
                 presetOverrideMode);
    cmd.AddValue("numSpines", "Number of EPS electrical core spine switches.", numSpines);
    cmd.AddValue("numLeaves", "Number of Leaf/ToR switches.", numLeaves);
    cmd.AddValue("serversPerLeaf", "Number of servers attached to each Leaf/ToR.", serversPerLeaf);
    cmd.AddValue("enableEcho", "Enable the Stage-2 UDP Echo connectivity validation flow.", enableEcho);
    cmd.AddValue("echoPacketSize", "UDP Echo client packet size in bytes.", echoPacketSize);
    cmd.AddValue("echoInterval", "UDP Echo client send interval in seconds.", echoInterval);
    cmd.AddValue("echoCount", "UDP Echo client packet count.", echoCount);
    cmd.AddValue("enableBulk", "Enable the Stage-3 TCP BulkSend validation flow.", enableBulk);
    cmd.AddValue("bulkMaxBytes", "TCP BulkSend maximum bytes to send.", bulkMaxBytes);
    cmd.AddValue("bulkStart", "TCP BulkSend application start time in seconds.", bulkStart);
    cmd.AddValue("bulkPort", "TCP PacketSink listening port.", bulkPort);
    cmd.AddValue("enableSecondBulk", "Enable the Stage-11 second OCS-covered BulkSend validation flow.", enableSecondBulk);
    cmd.AddValue("secondBulkPort", "Second TCP PacketSink listening port.", secondBulkPort);
    cmd.AddValue("secondBulkMaxBytes", "Second TCP BulkSend maximum bytes to send.", secondBulkMaxBytes);
    cmd.AddValue("secondBulkStart", "Second TCP BulkSend application start time in seconds.", secondBulkStart);
    cmd.AddValue("enableResidualBulk", "Enable the Stage-6 residual EPS BulkSend validation flow.", enableResidualBulk);
    cmd.AddValue("residualBulkPort", "Residual TCP PacketSink listening port.", residualBulkPort);
    cmd.AddValue("residualBulkMaxBytes", "Residual TCP BulkSend maximum bytes to send.", residualBulkMaxBytes);
    cmd.AddValue("residualBulkStart", "Residual TCP BulkSend application start time in seconds.", residualBulkStart);
    cmd.AddValue("enableStaticOcs", "Enable the Stage-4 static Leaf-To-Leaf OCS lightpath.", enableStaticOcs);
    cmd.AddValue("ocsLeafA", "First Leaf/ToR index for the static OCS lightpath.", ocsLeafA);
    cmd.AddValue("ocsLeafB", "Second Leaf/ToR index for the static OCS lightpath.", ocsLeafB);
    cmd.AddValue("ocsDataRate", "Static OCS lightpath data rate.", ocsDataRate);
    cmd.AddValue("ocsDelay", "Static OCS lightpath delay.", ocsDelay);
    cmd.AddValue("routeMode", "Routing mode: global or ocs-forced.", routeMode);
    cmd.AddValue("enableMatrixSelect", "Select the static OCS Leaf pair from a built-in ToR traffic matrix.", enableMatrixSelect);
    cmd.AddValue("trafficMatrixMode", "Built-in ToR traffic matrix mode: skewed, clustered, or uniform.", trafficMatrixMode);
    cmd.AddValue("enableEwmaSmoothing",
                 "Enable EWMA smoothing from current synthetic traffic matrix A(t) to control matrix A_bar(t).",
                 enableEwmaSmoothing);
    cmd.AddValue("ewmaBeta", "EWMA smoothing coefficient beta in [0, 1).", ewmaBeta);
    cmd.AddValue("trafficMatrixSource",
                 "Traffic matrix source. Current implementation supports synthetic only.",
                 trafficMatrixSource);
    cmd.AddValue("trafficGraphThreshold",
                 "Traffic graph sparsification threshold theta_f applied to A(t).",
                 trafficGraphThreshold);
    cmd.AddValue("communityMode", "Community label source: preview or louvain.", communityMode);
    cmd.AddValue("louvainMode", "Louvain mode: single-level or multi-level.", louvainMode);
    cmd.AddValue("louvainMaxPasses", "Maximum passes per Louvain local-moving level.", louvainMaxPasses);
    cmd.AddValue("louvainMaxLevels", "Maximum graph-folding levels for multi-level Louvain.", louvainMaxLevels);
    cmd.AddValue("louvainEpsilon", "Minimum modularity gain for Louvain local moving.", louvainEpsilon);
    cmd.AddValue("enableMultiPeriodControl",
                 "Enable multi-period OCS controller decision simulation.",
                 enableMultiPeriodControl);
    cmd.AddValue("controlEpochs", "Number of multi-period control epochs.", controlEpochs);
    cmd.AddValue("trafficMatrixSequence",
                 "Traffic matrix sequence: static, skewed-to-clustered, clustered-to-skewed, or alternating.",
                 trafficMatrixSequence);
    cmd.AddValue("selectionMetric", "OCS pair selection metric: absolute, excess, or community-excess.", selectionMetric);
    cmd.AddValue("eta", "Resolution parameter for modularity gain.", eta);
    cmd.AddValue("communityAlpha", "Cross-community utility discount factor for community-excess.", communityAlpha);
    cmd.AddValue("enableStateHolding", "Enable previous-cycle OCS state holding gain.", enableStateHolding);
    cmd.AddValue("stateHoldingLambda", "State holding gain for previously installed OCS edges.", stateHoldingLambda);
    cmd.AddValue("previousOcsMode", "Previous OCS state mode: none, same-as-manual, skewed-primary, clustered-primary, or custom.", previousOcsMode);
    cmd.AddValue("previousOcsLeafA", "First Leaf/ToR index for custom previous OCS state.", previousOcsLeafA);
    cmd.AddValue("previousOcsLeafB", "Second Leaf/ToR index for custom previous OCS state.", previousOcsLeafB);
    cmd.AddValue("enableConfigUpdateGate", "Enable single-cycle OCS configuration update threshold gating.", enableConfigUpdateGate);
    cmd.AddValue("configUpdateThreshold", "Configuration update threshold for candidate-vs-previous OCS selection.", configUpdateThreshold);
    cmd.AddValue("configScoreMode",
                 "Configuration update objective: selection-score-sum or paper-objective.",
                 configScoreMode);
    cmd.AddValue("reconfigurationPenalty",
                 "Per changed OCS edge penalty used by configScoreMode=paper-objective.",
                 reconfigurationPenalty);
    cmd.AddValue("enableHoldTimeGate", "Enable minimum OCS configuration hold-time gating.", enableHoldTimeGate);
    cmd.AddValue("minHoldCycles", "Minimum OCS configuration hold cycles.", minHoldCycles);
    cmd.AddValue("previousConfigAge", "Number of cycles that the previous OCS configuration has been held.", previousConfigAge);
    cmd.AddValue("ocsPortK", "Per-Leaf OCS port budget for greedy candidate selection.", ocsPortK);
    cmd.AddValue("maxSelectedOcsLinks", "Maximum number of OCS candidate links selected by the controller.", maxSelectedOcsLinks);
    cmd.AddValue("enableMatrixFlows", "Enable Stage-12 matrix-driven multi-pair BulkSend flows.", enableMatrixFlows);
    cmd.AddValue("matrixFlowMaxBytes", "MaxBytes for each matrix-generated BulkSend flow.", matrixFlowMaxBytes);
    cmd.AddValue("matrixFlowStart", "Start time for matrix-generated BulkSend flows in seconds.", matrixFlowStart);
    cmd.AddValue("matrixFlowPortBase", "Base TCP port for matrix-generated PacketSink applications.", matrixFlowPortBase);
    cmd.AddValue("matrixFlowRxBytesTolerance",
                 "Allowed matrix-flow Rx byte overshoot for result invariants.",
                 matrixFlowRxBytesTolerance);
    cmd.AddValue("enableOcsAdmissionControl",
                 "Enable OCS matrix-flow capacity admission control.",
                 enableOcsAdmissionControl);
    cmd.AddValue("ocsAdmissionThreshold",
                 "OCS per-lightpath admission threshold in traffic-matrix units.",
                 ocsAdmissionThreshold);
    cmd.AddValue("matrixFlowDemand",
                 "Abstract matrix-flow demand used for OCS admission control.",
                 matrixFlowDemand);
    cmd.AddValue("enableEpsWecmp",
                 "Enable EPS residual-flow WECMP control-plane next-hop selection.",
                 enableEpsWecmp);
    cmd.AddValue("epsWecmpRho", "EPS-WECMP utilization smoothing coefficient.", epsWecmpRho);
    cmd.AddValue("epsWecmpGamma", "EPS-WECMP load sensitivity exponent.", epsWecmpGamma);
    cmd.AddValue("epsWecmpEpsilon", "EPS-WECMP attractiveness stability term.", epsWecmpEpsilon);
    cmd.AddValue("epsWecmpKappa", "EPS-WECMP probability update step.", epsWecmpKappa);
    cmd.AddValue("epsWecmpMaxDelta",
                 "EPS-WECMP maximum per-cycle probability change.",
                 epsWecmpMaxDelta);
    cmd.AddValue("epsWecmpEpochs",
                 "Number of EPS-WECMP control-plane update epochs.",
                 epsWecmpEpochs);
    cmd.AddValue("epsWecmpCapacity",
                 "EPS-WECMP abstract link capacity in traffic-matrix units.",
                 epsWecmpCapacity);
    cmd.AddValue("epsWecmpPathMetric",
                 "EPS-WECMP path metric over leaf-spine links: max or average.",
                 epsWecmpPathMetric);
    cmd.AddValue("epsWecmpLoadSource",
                 "EPS-WECMP load source: control-plane or measured-snapshot.",
                 epsWecmpLoadSource);
    cmd.AddValue("measuredWecmpNoSampleFallback",
                 "Fallback mode when epsWecmpLoadSource=measured-snapshot has no runtime measured sample: control-plane, zero, or error.",
                 measuredWecmpNoSampleFallback);
    cmd.AddValue("measuredWecmpWarmupTime",
                 "Warmup time in seconds before measured WECMP decisions. Patch 4B records this value but does not add delayed decisions.",
                 measuredWecmpWarmupTime);
    cmd.AddValue("enableMeasuredWecmpLaterFlowProof",
                 "Enable the Patch 4C two-stage proof that measured WECMP only affects a later new flow.",
                 enableMeasuredWecmpLaterFlowProof);
    cmd.AddValue("measuredWecmpLaterDecisionTime",
                 "Simulation time when the Patch 4C later-flow measured WECMP decision is made.",
                 measuredWecmpLaterDecisionTime);
    cmd.AddValue("measuredWecmpLaterFlowStart",
                 "Simulation time when the Patch 4C later proof BulkSend flow starts.",
                 measuredWecmpLaterFlowStart);
    cmd.AddValue("measuredWecmpLaterFlowMaxBytes",
                 "MaxBytes for the Patch 4C later proof BulkSend flow.",
                 measuredWecmpLaterFlowMaxBytes);
    cmd.AddValue("measuredWecmpLaterFlowPort",
                 "TCP port for the Patch 4C later proof flow.",
                 measuredWecmpLaterFlowPort);
    cmd.AddValue("measuredWecmpLaterSrcLeaf",
                 "Source leaf index for the Patch 4C later proof flow.",
                 measuredWecmpLaterSrcLeaf);
    cmd.AddValue("measuredWecmpLaterDstLeaf",
                 "Destination leaf index for the Patch 4C later proof flow.",
                 measuredWecmpLaterDstLeaf);
    cmd.AddValue("measuredWecmpLaterSrcServer",
                 "Source server offset for the Patch 4C later proof flow.",
                 measuredWecmpLaterSrcServer);
    cmd.AddValue("measuredWecmpLaterDstServer",
                 "Destination server offset for the Patch 4C later proof flow.",
                 measuredWecmpLaterDstServer);
    cmd.AddValue("epsWecmpDiagnosticLoadMode",
                 "EPS-WECMP diagnostic synthetic load mode: none or hot-spine.",
                 epsWecmpDiagnosticLoadMode);
    cmd.AddValue("epsWecmpDiagnosticLoad",
                 "Synthetic background load added to diagnostic EPS physical links.",
                 epsWecmpDiagnosticLoad);
    cmd.AddValue("epsWecmpDiagnosticHotSpine",
                 "Spine index receiving diagnostic synthetic background load.",
                 epsWecmpDiagnosticHotSpine);
    cmd.AddValue("enableEpsWecmpRouting",
                 "Bind residual matrix-flow EPS paths to EPS-WECMP selected spines using static routes.",
                 enableEpsWecmpRouting);
    cmd.AddValue("enableMultiPeriodWecmpState",
                 "Update EPS-WECMP pair state after each multi-period OCS control epoch.",
                 enableMultiPeriodWecmpState);
    cmd.AddValue("enableAlgorithmInvariantCheck",
                 "Enable Stage-40 core algorithm invariant checks.",
                 enableAlgorithmInvariantCheck);
    cmd.AddValue("strictAlgorithmInvariantCheck",
                 "Return non-zero when a Stage-40 invariant check fails.",
                 strictAlgorithmInvariantCheck);
    RegisterDetailedTraceCommandLineOptions(cmd,
                                            enableDetailedAlgorithmTrace,
                                            detailedCandidateLogLimit,
                                            enableDetailedFlowTrace,
                                            detailedFlowLogLimit);
    cmd.AddValue("enableStructuredResultExport",
                 "Write structured summary, flow, WECMP, and OCS-candidate CSV outputs.",
                 enableStructuredResultExport);
    cmd.AddValue("structuredResultDir",
                 "Directory for structured result CSV outputs.",
                 structuredResultDir);
    cmd.AddValue("linkUtilizationSampleInterval",
                 "Per-link measured Tx utilization sample interval in seconds. Values <= 0 disable link time-series sampling.",
                 linkUtilizationSampleInterval);
    cmd.AddValue("enableResultValidation",
                 "Enable Stage-24 result consistency validation logs.",
                 enableResultValidation);
    cmd.AddValue("validationMode", "Result validation mode: warn or silent.", validationMode);
    cmd.Parse(static_cast<int>(normalizedArgv.size()), normalizedArgv.data());

    const bool presetApplied = presetScenario != "manual";
    if (presetOverrideMode != "preset-wins" && presetOverrideMode != "explicit-wins")
    {
        std::cerr << "[HYBRID-DCN][ERROR] presetOverrideMode must be preset-wins or explicit-wins."
                  << std::endl;
        return 1;
    }

    if (presetScenario != "manual" && presetScenario != "baseline-excess" &&
        presetScenario != "community-aware" && presetScenario != "state-aware" &&
        presetScenario != "config-gated" && presetScenario != "hold-gated" &&
        presetScenario != "full-control" && presetScenario != "full-stack-control")
    {
        std::cerr << "[HYBRID-DCN][ERROR] presetScenario must be manual, baseline-excess, community-aware, state-aware, config-gated, hold-gated, full-control, or full-stack-control."
                  << std::endl;
        return 1;
    }

    if (validationMode != "warn" && validationMode != "silent")
    {
        std::cerr << "[HYBRID-DCN][ERROR] validationMode must be warn or silent."
                  << std::endl;
        return 1;
    }

    auto isExplicitArg = [&explicitArgNames](const std::string& name) {
        return std::find(explicitArgNames.begin(), explicitArgNames.end(), name) !=
               explicitArgNames.end();
    };

    auto shouldApplyPresetValue = [&](const std::string& name) {
        return presetOverrideMode == "preset-wins" || !isExplicitArg(name);
    };

    auto applyPresetCommon = [&]() {
        if (shouldApplyPresetValue("communityAlpha"))
        {
            communityAlpha = 0.5;
        }
        if (shouldApplyPresetValue("ocsPortK"))
        {
            ocsPortK = 1;
        }
        if (shouldApplyPresetValue("maxSelectedOcsLinks"))
        {
            maxSelectedOcsLinks = 2;
        }
        if (shouldApplyPresetValue("enableMatrixSelect"))
        {
            enableMatrixSelect = true;
        }
        if (shouldApplyPresetValue("enableStaticOcs"))
        {
            enableStaticOcs = true;
        }
        if (shouldApplyPresetValue("routeMode"))
        {
            routeMode = "ocs-forced";
        }
        if (shouldApplyPresetValue("enableMatrixFlows"))
        {
            enableMatrixFlows = true;
        }
        if (shouldApplyPresetValue("enableEcho"))
        {
            enableEcho = false;
        }
        if (shouldApplyPresetValue("enableBulk"))
        {
            enableBulk = false;
        }
        if (shouldApplyPresetValue("enableSecondBulk"))
        {
            enableSecondBulk = false;
        }
        if (shouldApplyPresetValue("enableResidualBulk"))
        {
            enableResidualBulk = false;
        }
    };

    if (presetScenario == "baseline-excess")
    {
        applyPresetCommon();
        if (shouldApplyPresetValue("trafficMatrixMode"))
        {
            trafficMatrixMode = "clustered";
        }
        if (shouldApplyPresetValue("communityMode"))
        {
            communityMode = "preview";
        }
        if (shouldApplyPresetValue("selectionMetric"))
        {
            selectionMetric = "excess";
        }
        if (shouldApplyPresetValue("enableStateHolding"))
        {
            enableStateHolding = false;
        }
        if (shouldApplyPresetValue("previousOcsMode"))
        {
            previousOcsMode = "none";
        }
        if (shouldApplyPresetValue("enableConfigUpdateGate"))
        {
            enableConfigUpdateGate = false;
        }
        if (shouldApplyPresetValue("enableHoldTimeGate"))
        {
            enableHoldTimeGate = false;
        }
    }
    else if (presetScenario == "community-aware")
    {
        applyPresetCommon();
        if (shouldApplyPresetValue("trafficMatrixMode"))
        {
            trafficMatrixMode = "clustered";
        }
        if (shouldApplyPresetValue("communityMode"))
        {
            communityMode = "louvain";
        }
        if (shouldApplyPresetValue("selectionMetric"))
        {
            selectionMetric = "community-excess";
        }
        if (shouldApplyPresetValue("enableStateHolding"))
        {
            enableStateHolding = false;
        }
        if (shouldApplyPresetValue("previousOcsMode"))
        {
            previousOcsMode = "none";
        }
        if (shouldApplyPresetValue("enableConfigUpdateGate"))
        {
            enableConfigUpdateGate = false;
        }
        if (shouldApplyPresetValue("enableHoldTimeGate"))
        {
            enableHoldTimeGate = false;
        }
    }
    else if (presetScenario == "state-aware")
    {
        applyPresetCommon();
        if (shouldApplyPresetValue("trafficMatrixMode"))
        {
            trafficMatrixMode = "skewed";
        }
        if (shouldApplyPresetValue("communityMode"))
        {
            communityMode = "louvain";
        }
        if (shouldApplyPresetValue("selectionMetric"))
        {
            selectionMetric = "community-excess";
        }
        if (shouldApplyPresetValue("enableStateHolding"))
        {
            enableStateHolding = true;
        }
        if (shouldApplyPresetValue("stateHoldingLambda"))
        {
            stateHoldingLambda = 5.0;
        }
        if (shouldApplyPresetValue("previousOcsMode"))
        {
            previousOcsMode = "skewed-primary";
        }
        if (shouldApplyPresetValue("enableConfigUpdateGate"))
        {
            enableConfigUpdateGate = false;
        }
        if (shouldApplyPresetValue("enableHoldTimeGate"))
        {
            enableHoldTimeGate = false;
        }
    }
    else if (presetScenario == "config-gated")
    {
        applyPresetCommon();
        if (shouldApplyPresetValue("trafficMatrixMode"))
        {
            trafficMatrixMode = "skewed";
        }
        if (shouldApplyPresetValue("communityMode"))
        {
            communityMode = "louvain";
        }
        if (shouldApplyPresetValue("selectionMetric"))
        {
            selectionMetric = "community-excess";
        }
        if (shouldApplyPresetValue("enableStateHolding"))
        {
            enableStateHolding = true;
        }
        if (shouldApplyPresetValue("stateHoldingLambda"))
        {
            stateHoldingLambda = 5.0;
        }
        if (shouldApplyPresetValue("previousOcsMode"))
        {
            previousOcsMode = "skewed-primary";
        }
        if (shouldApplyPresetValue("enableConfigUpdateGate"))
        {
            enableConfigUpdateGate = true;
        }
        if (shouldApplyPresetValue("configUpdateThreshold"))
        {
            configUpdateThreshold = 0.0;
        }
        if (shouldApplyPresetValue("enableHoldTimeGate"))
        {
            enableHoldTimeGate = false;
        }
    }
    else if (presetScenario == "hold-gated")
    {
        applyPresetCommon();
        if (shouldApplyPresetValue("trafficMatrixMode"))
        {
            trafficMatrixMode = "skewed";
        }
        if (shouldApplyPresetValue("communityMode"))
        {
            communityMode = "louvain";
        }
        if (shouldApplyPresetValue("selectionMetric"))
        {
            selectionMetric = "community-excess";
        }
        if (shouldApplyPresetValue("enableStateHolding"))
        {
            enableStateHolding = true;
        }
        if (shouldApplyPresetValue("stateHoldingLambda"))
        {
            stateHoldingLambda = 5.0;
        }
        if (shouldApplyPresetValue("previousOcsMode"))
        {
            previousOcsMode = "skewed-primary";
        }
        if (shouldApplyPresetValue("enableConfigUpdateGate"))
        {
            enableConfigUpdateGate = true;
        }
        if (shouldApplyPresetValue("configUpdateThreshold"))
        {
            configUpdateThreshold = 0.0;
        }
        if (shouldApplyPresetValue("enableHoldTimeGate"))
        {
            enableHoldTimeGate = true;
        }
        if (shouldApplyPresetValue("minHoldCycles"))
        {
            minHoldCycles = 3;
        }
        if (shouldApplyPresetValue("previousConfigAge"))
        {
            previousConfigAge = 1;
        }
    }
    else if (presetScenario == "full-control")
    {
        applyPresetCommon();
        if (shouldApplyPresetValue("trafficMatrixMode"))
        {
            trafficMatrixMode = "skewed";
        }
        if (shouldApplyPresetValue("communityMode"))
        {
            communityMode = "louvain";
        }
        if (shouldApplyPresetValue("selectionMetric"))
        {
            selectionMetric = "community-excess";
        }
        if (shouldApplyPresetValue("enableStateHolding"))
        {
            enableStateHolding = true;
        }
        if (shouldApplyPresetValue("stateHoldingLambda"))
        {
            stateHoldingLambda = 5.0;
        }
        if (shouldApplyPresetValue("previousOcsMode"))
        {
            previousOcsMode = "skewed-primary";
        }
        if (shouldApplyPresetValue("enableConfigUpdateGate"))
        {
            enableConfigUpdateGate = true;
        }
        if (shouldApplyPresetValue("configUpdateThreshold"))
        {
            configUpdateThreshold = 0.0;
        }
        if (shouldApplyPresetValue("enableHoldTimeGate"))
        {
            enableHoldTimeGate = true;
        }
        if (shouldApplyPresetValue("minHoldCycles"))
        {
            minHoldCycles = 3;
        }
        if (shouldApplyPresetValue("previousConfigAge"))
        {
            previousConfigAge = 3;
        }
    }
    else if (presetScenario == "full-stack-control")
    {
        applyPresetCommon();
        if (shouldApplyPresetValue("trafficMatrixMode"))
        {
            trafficMatrixMode = "skewed";
        }
        if (shouldApplyPresetValue("communityMode"))
        {
            communityMode = "louvain";
        }
        if (shouldApplyPresetValue("selectionMetric"))
        {
            selectionMetric = "community-excess";
        }
        if (shouldApplyPresetValue("enableStateHolding"))
        {
            enableStateHolding = true;
        }
        if (shouldApplyPresetValue("stateHoldingLambda"))
        {
            stateHoldingLambda = 5.0;
        }
        if (shouldApplyPresetValue("previousOcsMode"))
        {
            previousOcsMode = "skewed-primary";
        }
        if (shouldApplyPresetValue("enableConfigUpdateGate"))
        {
            enableConfigUpdateGate = true;
        }
        if (shouldApplyPresetValue("configUpdateThreshold"))
        {
            configUpdateThreshold = 0.0;
        }
        if (shouldApplyPresetValue("configScoreMode"))
        {
            configScoreMode = "paper-objective";
        }
        if (shouldApplyPresetValue("reconfigurationPenalty"))
        {
            reconfigurationPenalty = 5.0;
        }
        if (shouldApplyPresetValue("enableHoldTimeGate"))
        {
            enableHoldTimeGate = true;
        }
        if (shouldApplyPresetValue("minHoldCycles"))
        {
            minHoldCycles = 3;
        }
        if (shouldApplyPresetValue("previousConfigAge"))
        {
            previousConfigAge = 3;
        }
        if (shouldApplyPresetValue("enableOcsAdmissionControl"))
        {
            enableOcsAdmissionControl = true;
        }
        if (shouldApplyPresetValue("ocsAdmissionThreshold"))
        {
            ocsAdmissionThreshold = 60.0;
        }
        if (shouldApplyPresetValue("matrixFlowDemand"))
        {
            matrixFlowDemand = 40.0;
        }
        if (shouldApplyPresetValue("enableEpsWecmp"))
        {
            enableEpsWecmp = true;
        }
        if (shouldApplyPresetValue("enableEpsWecmpRouting"))
        {
            enableEpsWecmpRouting = true;
        }
        if (shouldApplyPresetValue("enableMultiPeriodControl"))
        {
            enableMultiPeriodControl = true;
        }
        if (shouldApplyPresetValue("controlEpochs"))
        {
            controlEpochs = 4;
        }
        if (shouldApplyPresetValue("trafficMatrixSequence"))
        {
            trafficMatrixSequence = "skewed-to-clustered";
        }
        if (shouldApplyPresetValue("enableMultiPeriodWecmpState"))
        {
            enableMultiPeriodWecmpState = true;
        }
    }

    if (simTime <= 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] simTime must be greater than 0." << std::endl;
        return 1;
    }

    if (numSpines == 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] numSpines must be greater than 0." << std::endl;
        return 1;
    }

    if (numLeaves <= 1)
    {
        std::cerr << "[HYBRID-DCN][ERROR] numLeaves must be greater than 1." << std::endl;
        return 1;
    }

    if (serversPerLeaf == 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] serversPerLeaf must be greater than 0." << std::endl;
        return 1;
    }

    if (ocsPortK == 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] ocsPortK must be greater than 0." << std::endl;
        return 1;
    }

    if (maxSelectedOcsLinks == 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] maxSelectedOcsLinks must be greater than 0."
                  << std::endl;
        return 1;
    }

    if (routeMode != "global" && routeMode != "ocs-forced")
    {
        std::cerr << "[HYBRID-DCN][ERROR] routeMode must be global or ocs-forced." << std::endl;
        return 1;
    }

    if (trafficMatrixMode != "skewed" && trafficMatrixMode != "clustered" &&
        trafficMatrixMode != "uniform")
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] trafficMatrixMode must be skewed, clustered, or uniform."
            << std::endl;
        return 1;
    }

    if (trafficMatrixSource != "synthetic")
    {
        std::cerr << "[HYBRID-DCN][ERROR] trafficMatrixSource must be synthetic."
                  << std::endl;
        return 1;
    }

    if (ewmaBeta < 0 || ewmaBeta >= 1)
    {
        std::cerr << "[HYBRID-DCN][ERROR] ewmaBeta must be in [0, 1)." << std::endl;
        return 1;
    }

    if (trafficGraphThreshold < 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] trafficGraphThreshold must be non-negative."
                  << std::endl;
        return 1;
    }

    if (trafficMatrixSequence != "static" && trafficMatrixSequence != "skewed-to-clustered" &&
        trafficMatrixSequence != "clustered-to-skewed" && trafficMatrixSequence != "alternating")
    {
        std::cerr << "[HYBRID-DCN][ERROR] trafficMatrixSequence must be static, skewed-to-clustered, clustered-to-skewed, or alternating."
                  << std::endl;
        return 1;
    }

    if (enableMultiPeriodControl && controlEpochs == 0)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] controlEpochs must be greater than 0 when enableMultiPeriodControl is true."
            << std::endl;
        return 1;
    }

    if (ocsAdmissionThreshold <= 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] ocsAdmissionThreshold must be greater than 0."
                  << std::endl;
        return 1;
    }

    if (matrixFlowDemand <= 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] matrixFlowDemand must be greater than 0."
                  << std::endl;
        return 1;
    }

    if (epsWecmpRho <= 0 || epsWecmpRho >= 1)
    {
        std::cerr << "[HYBRID-DCN][ERROR] epsWecmpRho must be in (0, 1)." << std::endl;
        return 1;
    }

    if (epsWecmpGamma <= 1)
    {
        std::cerr << "[HYBRID-DCN][ERROR] epsWecmpGamma must be greater than 1."
                  << std::endl;
        return 1;
    }

    if (epsWecmpEpsilon <= 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] epsWecmpEpsilon must be greater than 0."
                  << std::endl;
        return 1;
    }

    if (epsWecmpKappa <= 0 || epsWecmpKappa > 1)
    {
        std::cerr << "[HYBRID-DCN][ERROR] epsWecmpKappa must be in (0, 1]."
                  << std::endl;
        return 1;
    }

    if (epsWecmpMaxDelta <= 0 || epsWecmpMaxDelta > 1)
    {
        std::cerr << "[HYBRID-DCN][ERROR] epsWecmpMaxDelta must be in (0, 1]."
                  << std::endl;
        return 1;
    }

    if (enableEpsWecmp && epsWecmpEpochs == 0)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] epsWecmpEpochs must be greater than 0 when enableEpsWecmp is true."
            << std::endl;
        return 1;
    }

    if (epsWecmpCapacity <= 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] epsWecmpCapacity must be greater than 0."
                  << std::endl;
        return 1;
    }

    if (epsWecmpPathMetric != "max" && epsWecmpPathMetric != "average")
    {
        std::cerr << "[HYBRID-DCN][ERROR] epsWecmpPathMetric must be max or average."
                  << std::endl;
        return 1;
    }

    if (epsWecmpLoadSource != "control-plane" &&
        epsWecmpLoadSource != "measured-snapshot")
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] epsWecmpLoadSource must be control-plane or measured-snapshot."
            << std::endl;
        return 1;
    }

    if (measuredWecmpNoSampleFallback != "control-plane" &&
        measuredWecmpNoSampleFallback != "zero" &&
        measuredWecmpNoSampleFallback != "error")
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] measuredWecmpNoSampleFallback must be control-plane, zero, or error."
            << std::endl;
        return 1;
    }

    if (measuredWecmpWarmupTime < 0)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] measuredWecmpWarmupTime must be greater than or equal to 0."
            << std::endl;
        return 1;
    }

    if (enableMeasuredWecmpLaterFlowProof)
    {
        if (!enableMatrixFlows || !enableEpsWecmp || !enableEpsWecmpRouting)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] enableMeasuredWecmpLaterFlowProof requires enableMatrixFlows, enableEpsWecmp, and enableEpsWecmpRouting."
                << std::endl;
            return 1;
        }
        if (epsWecmpLoadSource != "measured-snapshot")
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] enableMeasuredWecmpLaterFlowProof requires epsWecmpLoadSource=measured-snapshot."
                << std::endl;
            return 1;
        }
        if (measuredWecmpLaterDecisionTime < 0)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] measuredWecmpLaterDecisionTime must be greater than or equal to 0."
                << std::endl;
            return 1;
        }
        if (measuredWecmpLaterDecisionTime >= measuredWecmpLaterFlowStart)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] measuredWecmpLaterDecisionTime must be earlier than measuredWecmpLaterFlowStart."
                << std::endl;
            return 1;
        }
        if (measuredWecmpLaterFlowStart >= simTime)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] measuredWecmpLaterFlowStart must be earlier than simTime."
                << std::endl;
            return 1;
        }
        if (measuredWecmpLaterFlowMaxBytes == 0)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] measuredWecmpLaterFlowMaxBytes must be greater than 0."
                << std::endl;
            return 1;
        }
        if (measuredWecmpLaterFlowPort == 0)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] measuredWecmpLaterFlowPort must be greater than 0."
                << std::endl;
            return 1;
        }
        if (measuredWecmpLaterSrcLeaf >= numLeaves ||
            measuredWecmpLaterDstLeaf >= numLeaves ||
            measuredWecmpLaterSrcServer >= serversPerLeaf ||
            measuredWecmpLaterDstServer >= serversPerLeaf)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] measured later proof leaf/server index is out of range."
                << std::endl;
            return 1;
        }
        if (measuredWecmpLaterSrcLeaf == measuredWecmpLaterDstLeaf &&
            measuredWecmpLaterSrcServer == measuredWecmpLaterDstServer)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] measured later proof source and destination host must differ."
                << std::endl;
            return 1;
        }
    }

    if (epsWecmpDiagnosticLoadMode != "none" &&
        epsWecmpDiagnosticLoadMode != "hot-spine")
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] epsWecmpDiagnosticLoadMode must be none or hot-spine."
            << std::endl;
        return 1;
    }

    if (epsWecmpDiagnosticLoad < 0)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] epsWecmpDiagnosticLoad must be greater than or equal to 0."
            << std::endl;
        return 1;
    }

    if (epsWecmpDiagnosticHotSpine >= numSpines)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] epsWecmpDiagnosticHotSpine must be less than numSpines."
            << std::endl;
        return 1;
    }

    if (enableEpsWecmpRouting && !enableEpsWecmp)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] enableEpsWecmpRouting requires enableEpsWecmp=true."
            << std::endl;
        return 1;
    }

    if (communityMode != "preview" && communityMode != "louvain")
    {
        std::cerr << "[HYBRID-DCN][ERROR] communityMode must be preview or louvain."
                  << std::endl;
        return 1;
    }

    if (louvainMode != "single-level" && louvainMode != "multi-level")
    {
        std::cerr << "[HYBRID-DCN][ERROR] louvainMode must be single-level or multi-level."
                  << std::endl;
        return 1;
    }

    if (communityMode == "louvain" && louvainMaxPasses == 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] louvainMaxPasses must be greater than 0."
                  << std::endl;
        return 1;
    }

    if (communityMode == "louvain" && louvainEpsilon <= 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] louvainEpsilon must be greater than 0."
                  << std::endl;
        return 1;
    }

    if (communityMode == "louvain" && louvainMode == "multi-level" && louvainMaxLevels == 0)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] louvainMaxLevels must be greater than 0 for multi-level Louvain."
            << std::endl;
        return 1;
    }

    if (selectionMetric != "absolute" && selectionMetric != "excess" &&
        selectionMetric != "community-excess")
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] selectionMetric must be absolute, excess, or community-excess."
                  << std::endl;
        return 1;
    }

    if (eta <= 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] eta must be greater than 0." << std::endl;
        return 1;
    }

    if (communityAlpha <= 0 || communityAlpha >= 1)
    {
        std::cerr << "[HYBRID-DCN][ERROR] communityAlpha must be in (0, 1)." << std::endl;
        return 1;
    }

    if (stateHoldingLambda < 0)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] stateHoldingLambda must be greater than or equal to 0."
            << std::endl;
        return 1;
    }

    if (previousOcsMode != "none" && previousOcsMode != "same-as-manual" &&
        previousOcsMode != "skewed-primary" && previousOcsMode != "clustered-primary" &&
        previousOcsMode != "custom")
    {
        std::cerr << "[HYBRID-DCN][ERROR] previousOcsMode must be none, same-as-manual, "
                     "skewed-primary, clustered-primary, or custom."
                  << std::endl;
        return 1;
    }

    if (previousOcsMode == "custom" &&
        (previousOcsLeafA >= numLeaves || previousOcsLeafB >= numLeaves ||
         previousOcsLeafA == previousOcsLeafB))
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] previousOcs custom leaves must be valid and different."
            << std::endl;
        return 1;
    }

    if (previousOcsMode == "skewed-primary" && numLeaves < 4)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] previousOcsMode=skewed-primary requires numLeaves >= 4."
            << std::endl;
        return 1;
    }

    if (previousOcsMode == "clustered-primary" && numLeaves < 2)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] previousOcsMode=clustered-primary requires numLeaves >= 2."
            << std::endl;
        return 1;
    }

    if (configUpdateThreshold < 0)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] configUpdateThreshold must be greater than or equal to 0."
            << std::endl;
        return 1;
    }

    if (configScoreMode != "selection-score-sum" && configScoreMode != "paper-objective")
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] configScoreMode must be selection-score-sum or paper-objective."
            << std::endl;
        return 1;
    }

    if (reconfigurationPenalty < 0)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] reconfigurationPenalty must be greater than or equal to 0."
            << std::endl;
        return 1;
    }

    if (enableHoldTimeGate && minHoldCycles == 0)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] minHoldCycles must be greater than 0 when enableHoldTimeGate is true."
            << std::endl;
        return 1;
    }

    if (enableMatrixSelect)
    {
        if (numLeaves < 4)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] numLeaves must be at least 4 when enableMatrixSelect is true."
                << std::endl;
            return 1;
        }

        if (!enableStaticOcs)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] enableMatrixSelect=true requires enableStaticOcs=true."
                << std::endl;
            return 1;
        }
    }

    DirectedTrafficMatrix rawDirectedTrafficMatrix =
        buildSyntheticDirectedTrafficMatrix(trafficMatrixMode, numLeaves);
    WeightedMatrix rawTrafficMatrix =
        buildUndirectedCommunicationIntensityMatrix(rawDirectedTrafficMatrix);
    WeightedMatrix trafficGraphMatrix =
        applyTrafficGraphThreshold(rawTrafficMatrix, trafficGraphThreshold);
    WeightedMatrix controlTrafficMatrix =
        enableEwmaSmoothing
            ? updateEwmaMatrix(WeightedMatrix{}, trafficGraphMatrix, ewmaBeta, false)
            : trafficGraphMatrix;
    // Compatibility name used by the existing control-plane code path. It
    // represents the control matrix, not necessarily the raw demand matrix.
    std::vector<std::vector<double>> torTrafficMatrix = controlTrafficMatrix;
    std::string matrixUsedForControl = enableEwmaSmoothing ? "ewma" : "raw";

    const CommunityPreview communityPreview =
        buildCommunityPreview(trafficMatrixMode, numLeaves);

    std::vector<double> nodeDegree = computeNodeDegree(torTrafficMatrix);
    double totalTraffic = computeTotalTraffic(torTrafficMatrix);

    if (enableMatrixSelect && selectionMetric == "excess" && totalTraffic <= 0)
    {
        std::cerr << "[HYBRID-DCN][ERROR] totalTraffic must be greater than 0 when selectionMetric=excess."
                  << std::endl;
        return 1;
    }

    std::vector<std::vector<double>> expectedTraffic(numLeaves,
                                                     std::vector<double>(numLeaves, 0.0));
    std::vector<std::vector<double>> modularityGain(numLeaves,
                                                    std::vector<double>(numLeaves, 0.0));
    std::vector<std::vector<double>> ocsUtility(numLeaves,
                                                std::vector<double>(numLeaves, 0.0));
    if (totalTraffic > 0)
    {
        for (uint32_t i = 0; i < numLeaves; ++i)
        {
            for (uint32_t j = 0; j < numLeaves; ++j)
            {
                if (i == j)
                {
                    continue;
                }
                expectedTraffic[i][j] = nodeDegree[i] * nodeDegree[j] / (2.0 * totalTraffic);
                modularityGain[i][j] = torTrafficMatrix[i][j] - eta * expectedTraffic[i][j];
                ocsUtility[i][j] = std::max(modularityGain[i][j], 0.0);
            }
        }
    }

    LouvainResult louvainResult{communityPreview.labels,
                                communityPreview.communityCount,
                                0,
                                false,
                                computeModularityQ(torTrafficMatrix, communityPreview.labels, eta),
                                0,
                                false,
                                {}};
    if (communityMode == "louvain")
    {
        louvainResult =
            louvainMode == "multi-level"
                ? runMultiLevelLouvain(torTrafficMatrix,
                                       numLeaves,
                                       louvainMaxPasses,
                                       louvainMaxLevels,
                                       louvainEpsilon,
                                       eta)
                : runSingleLevelLouvain(torTrafficMatrix,
                                        numLeaves,
                                        louvainMaxPasses,
                                        louvainEpsilon,
                                        eta);
    }
    std::vector<uint32_t> activeCommunityLabels =
        communityMode == "louvain" ? louvainResult.labels : communityPreview.labels;
    uint32_t activeCommunityCount =
        communityMode == "louvain" ? louvainResult.communityCount
                                   : communityPreview.communityCount;

    auto isIntraCommunity = [&activeCommunityLabels](uint32_t leafA, uint32_t leafB) {
        return activeCommunityLabels[leafA] == activeCommunityLabels[leafB];
    };

    std::vector<std::vector<bool>> previousOcsState(numLeaves,
                                                    std::vector<bool>(numLeaves, false));
    std::vector<OcsCandidateEdge> previousOcsEdges;

    auto markPreviousOcsEdge = [&](uint32_t leafA, uint32_t leafB) {
        previousOcsState[leafA][leafB] = true;
        previousOcsState[leafB][leafA] = true;
    };

    if (previousOcsMode == "same-as-manual")
    {
        if (ocsLeafA >= numLeaves || ocsLeafB >= numLeaves || ocsLeafA == ocsLeafB)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] previousOcsMode=same-as-manual requires valid and different ocsLeafA/ocsLeafB."
                << std::endl;
            return 1;
        }
        markPreviousOcsEdge(ocsLeafA, ocsLeafB);
    }
    else if (previousOcsMode == "skewed-primary")
    {
        markPreviousOcsEdge(0, 3);
    }
    else if (previousOcsMode == "clustered-primary")
    {
        markPreviousOcsEdge(0, 1);
    }
    else if (previousOcsMode == "custom")
    {
        markPreviousOcsEdge(previousOcsLeafA, previousOcsLeafB);
    }

    double selectedOcsWeight = 0.0;
    double selectedExpectedTraffic = 0.0;
    double selectedModularityGain = 0.0;
    double selectedOcsUtility = 0.0;
    double selectedBaseUtility = 0.0;
    double selectedCommunityFactor = 0.0;
    double selectedCommunityUtility = 0.0;
    bool selectedIntraCommunity = false;
    bool selectedWasPreviouslyInstalled = false;
    double selectedStateHoldingGain = 0.0;
    double selectedSelectionScore = 0.0;

    auto edgeScoreWithoutState = [&selectionMetric](const OcsCandidateEdge& edge) {
        if (selectionMetric == "absolute")
        {
            return edge.traffic;
        }
        if (selectionMetric == "excess")
        {
            return edge.baseUtility;
        }
        return edge.communityUtility;
    };

    auto makeCandidateEdge = [&](uint32_t leafA, uint32_t leafB) {
        const double baseUtility = ocsUtility[leafA][leafB];
        const bool intraCommunity = isIntraCommunity(leafA, leafB);
        const double communityFactor = intraCommunity ? 1.0 : communityAlpha;
        const double communityUtility = baseUtility * communityFactor;
        const bool wasPreviouslyInstalled = previousOcsState[leafA][leafB];
        const double stateHoldingGain =
            enableStateHolding && wasPreviouslyInstalled ? stateHoldingLambda : 0.0;
        OcsCandidateEdge edge{leafA,
                              leafB,
                              torTrafficMatrix[leafA][leafB],
                              expectedTraffic[leafA][leafB],
                              modularityGain[leafA][leafB],
                              0.0,
                              baseUtility,
                              communityFactor,
                              communityUtility,
                              intraCommunity,
                              wasPreviouslyInstalled,
                              stateHoldingGain,
                              0.0};
        edge.utility = edgeScoreWithoutState(edge);
        edge.selectionScore = edge.utility + edge.stateHoldingGain;
        return edge;
    };

    for (uint32_t leafA = 0; leafA < numLeaves; ++leafA)
    {
        for (uint32_t leafB = leafA + 1; leafB < numLeaves; ++leafB)
        {
            if (previousOcsState[leafA][leafB])
            {
                previousOcsEdges.push_back(makeCandidateEdge(leafA, leafB));
            }
        }
    }

    OcsEdgeAgeMatrix previousOcsEdgeAges = MakeZeroOcsEdgeAgeMatrix(numLeaves);
    for (const auto& edge : previousOcsEdges)
    {
        SetOcsEdgeAge(previousOcsEdgeAges, edge.leafA, edge.leafB, previousConfigAge);
    }

    std::vector<OcsCandidateEdge> candidateEdges;
    for (uint32_t i = 0; i < numLeaves; ++i)
    {
        for (uint32_t j = i + 1; j < numLeaves; ++j)
        {
            const OcsCandidateEdge edge = makeCandidateEdge(i, j);
            if (edge.selectionScore <= 0)
            {
                continue;
            }

            candidateEdges.push_back(edge);
        }
    }

    std::sort(candidateEdges.begin(),
              candidateEdges.end(),
              [](const OcsCandidateEdge& lhs, const OcsCandidateEdge& rhs) {
                  if (lhs.selectionScore != rhs.selectionScore)
                  {
                      return lhs.selectionScore > rhs.selectionScore;
                  }
                  if (lhs.leafA != rhs.leafA)
                  {
                      return lhs.leafA < rhs.leafA;
                  }
                  return lhs.leafB < rhs.leafB;
              });

    std::vector<uint32_t> ocsDegree(numLeaves, 0);
    std::vector<OcsCandidateEdge> candidateOcsEdges;
    std::vector<OcsCandidateEdge> holdOcsEdges;
    bool previousConfigAvailable = !previousOcsEdges.empty();
    if (enableHoldTimeGate && previousConfigAvailable)
    {
        for (const auto& edge : previousOcsEdges)
        {
            if (GetOcsEdgeAge(previousOcsEdgeAges, edge.leafA, edge.leafB) < minHoldCycles)
            {
                holdOcsEdges.push_back(edge);
            }
        }
    }

    auto isHeldEdge = [&holdOcsEdges](uint32_t leafA, uint32_t leafB) {
        for (const auto& heldEdge : holdOcsEdges)
        {
            if ((heldEdge.leafA == leafA && heldEdge.leafB == leafB) ||
                (heldEdge.leafA == leafB && heldEdge.leafB == leafA))
            {
                return true;
            }
        }
        return false;
    };

    for (const auto& edge : holdOcsEdges)
    {
        if (ocsDegree[edge.leafA] >= ocsPortK || ocsDegree[edge.leafB] >= ocsPortK)
        {
            std::cerr << "[HYBRID-DCN][ERROR] hold OCS edge exceeds ocsPortK." << std::endl;
            return 1;
        }
        candidateOcsEdges.push_back(edge);
        ocsDegree[edge.leafA]++;
        ocsDegree[edge.leafB]++;
    }
    for (const auto& edge : candidateEdges)
    {
        if (candidateOcsEdges.size() >= maxSelectedOcsLinks)
        {
            break;
        }

        if (!isHeldEdge(edge.leafA, edge.leafB) && ocsDegree[edge.leafA] < ocsPortK &&
            ocsDegree[edge.leafB] < ocsPortK)
        {
            candidateOcsEdges.push_back(edge);
            ocsDegree[edge.leafA]++;
            ocsDegree[edge.leafB]++;
        }
    }

    struct ConfigScoreBreakdown
    {
        double score;
        double rawUtilitySum;
        double selectionScoreSum;
        uint32_t changedEdgeCount;
    };

    auto makeEdgeSet = [](const std::vector<OcsCandidateEdge>& edges) {
        std::set<std::pair<uint32_t, uint32_t>> edgeSet;
        for (const auto& edge : edges)
        {
            edgeSet.insert(NormalizeEdgePair(edge.leafA, edge.leafB));
        }
        return edgeSet;
    };

    auto formatEdgePairSet = [](const std::set<std::pair<uint32_t, uint32_t>>& edgeSet) {
        if (edgeSet.empty())
        {
            return std::string("none");
        }

        std::ostringstream stream;
        bool first = true;
        for (const auto& pair : edgeSet)
        {
            if (!first)
            {
                stream << ",";
            }
            stream << pair.first << "-" << pair.second;
            first = false;
        }
        return stream.str();
    };

    auto computeChangedEdgeSet = [&](const std::vector<OcsCandidateEdge>& edges,
                                     const std::vector<OcsCandidateEdge>& previousEdges) {
        const auto currentSet = makeEdgeSet(edges);
        const auto previousSet = makeEdgeSet(previousEdges);
        std::set<std::pair<uint32_t, uint32_t>> changedSet;
        std::set_symmetric_difference(currentSet.begin(),
                                      currentSet.end(),
                                      previousSet.begin(),
                                      previousSet.end(),
                                      std::inserter(changedSet, changedSet.begin()));
        return changedSet;
    };

    auto countChangedEdges = [&](const std::vector<OcsCandidateEdge>& edges,
                                 const std::vector<OcsCandidateEdge>& previousEdges) {
        return static_cast<uint32_t>(computeChangedEdgeSet(edges, previousEdges).size());
    };

    auto formatEdgeSet = [&](const std::vector<OcsCandidateEdge>& edges) {
        return formatEdgePairSet(makeEdgeSet(edges));
    };

    auto formatChangedEdgeSet = [&](const std::vector<OcsCandidateEdge>& edges,
                                    const std::vector<OcsCandidateEdge>& previousEdges) {
        return formatEdgePairSet(computeChangedEdgeSet(edges, previousEdges));
    };

    auto computeConfigScore =
        [&](const std::vector<OcsCandidateEdge>& edges,
            const std::vector<OcsCandidateEdge>& previousEdges,
            const std::string& mode) {
            ConfigScoreBreakdown breakdown{0.0, 0.0, 0.0, 0};
            for (const auto& edge : edges)
            {
                breakdown.rawUtilitySum += edge.utility;
                breakdown.selectionScoreSum += edge.selectionScore;
            }
            breakdown.changedEdgeCount = countChangedEdges(edges, previousEdges);
            if (mode == "paper-objective")
            {
                breakdown.score =
                    breakdown.rawUtilitySum -
                    reconfigurationPenalty * static_cast<double>(breakdown.changedEdgeCount);
            }
            else
            {
                breakdown.score = breakdown.selectionScoreSum;
            }
            return breakdown;
    };

    ConfigScoreBreakdown candidateConfigBreakdown =
        computeConfigScore(candidateOcsEdges, previousOcsEdges, configScoreMode);
    ConfigScoreBreakdown previousConfigBreakdown =
        computeConfigScore(previousOcsEdges, previousOcsEdges, configScoreMode);
    double candidateConfigScore = candidateConfigBreakdown.score;
    double previousConfigScore = previousConfigBreakdown.score;
    double configScoreImprovement = candidateConfigScore - previousConfigScore;
    double candidateRawUtilitySum = candidateConfigBreakdown.rawUtilitySum;
    double previousRawUtilitySum = previousConfigBreakdown.rawUtilitySum;
    double candidateSelectionScoreSum = candidateConfigBreakdown.selectionScoreSum;
    double previousSelectionScoreSum = previousConfigBreakdown.selectionScoreSum;
    uint32_t candidateChangedEdges = candidateConfigBreakdown.changedEdgeCount;
    uint32_t previousChangedEdges = previousConfigBreakdown.changedEdgeCount;
    bool holdTimeActive = !holdOcsEdges.empty();
    std::string configGateDecision = "disabled";
    std::vector<OcsCandidateEdge> selectedOcsEdges;
    if (!enableConfigUpdateGate)
    {
        selectedOcsEdges = candidateOcsEdges;
    }
    else if (configScoreImprovement > configUpdateThreshold)
    {
        selectedOcsEdges = candidateOcsEdges;
        configGateDecision = "use-candidate";
    }
    else
    {
        selectedOcsEdges = previousOcsEdges;
        configGateDecision = "keep-previous";
    }
    OcsEdgeAgeMatrix selectedOcsEdgeAges =
        UpdateOcsEdgeAges(previousOcsEdgeAges, previousOcsEdges, selectedOcsEdges);

    uint32_t intraCandidateEdges = 0;
    uint32_t interCandidateEdges = 0;
    for (uint32_t leafA = 0; leafA < numLeaves; ++leafA)
    {
        for (uint32_t leafB = leafA + 1; leafB < numLeaves; ++leafB)
        {
            if (torTrafficMatrix[leafA][leafB] <= 0)
            {
                continue;
            }

            if (isIntraCommunity(leafA, leafB))
            {
                intraCandidateEdges++;
            }
            else
            {
                interCandidateEdges++;
            }
        }
    }

    std::vector<ControlEpochSummary> controlEpochSummaries;
    std::vector<MatrixEpochSummary> matrixEpochSummaries;
    std::vector<EpsWecmpEpochSummary> epsWecmpEpochSummaries;
    std::vector<EpsWecmpPairState> epsWecmpPairStates;
    std::vector<std::vector<EpsPhysicalLinkState>> epsPhysicalLinkStates(
        numLeaves,
        std::vector<EpsPhysicalLinkState>(numSpines));
    const MeasuredEpsDirectedLinkSnapshot emptyMeasuredEpsSnapshot{
        false, 0.0, 0.0, 0.0, 0, 0, 0.0, 0};
    std::vector<std::vector<MeasuredEpsDirectedLinkSnapshot>>
        epsMeasuredLeafToSpineSnapshots(
            numLeaves,
            std::vector<MeasuredEpsDirectedLinkSnapshot>(numSpines,
                                                         emptyMeasuredEpsSnapshot));
    std::vector<std::vector<MeasuredEpsDirectedLinkSnapshot>>
        epsMeasuredSpineToLeafSnapshots(
            numLeaves,
            std::vector<MeasuredEpsDirectedLinkSnapshot>(numSpines,
                                                         emptyMeasuredEpsSnapshot));
    uint64_t measuredWecmpSnapshotUpdateCount = 0;
    std::string finalEpochMatrixMode = trafficMatrixMode;

    for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
    {
        for (uint32_t spineIndex = 0; spineIndex < numSpines; ++spineIndex)
        {
            epsPhysicalLinkStates[leafIndex][spineIndex] =
                EpsPhysicalLinkState{leafIndex, spineIndex, 0.0, 0.0, 0.0};
        }
    }

    auto getOrCreateEpsWecmpPairState = [&](uint32_t srcLeaf,
                                            uint32_t dstLeaf) -> EpsWecmpPairState& {
        const uint32_t pairA = std::min(srcLeaf, dstLeaf);
        const uint32_t pairB = std::max(srcLeaf, dstLeaf);
        const double initialProbability =
            numSpines > 0 ? 1.0 / static_cast<double>(numSpines) : 0.0;

        for (auto& state : epsWecmpPairStates)
        {
            if (state.srcLeaf == pairA && state.dstLeaf == pairB)
            {
                if (state.probabilities.size() != numSpines ||
                    state.smoothedUtilizations.size() != numSpines)
                {
                    state.probabilities.assign(numSpines, initialProbability);
                    state.smoothedUtilizations.assign(numSpines, 0.0);
                    state.initialized = true;
                }
                return state;
            }
        }

        epsWecmpPairStates.push_back({pairA,
                                      pairB,
                                      std::vector<double>(numSpines, initialProbability),
                                      std::vector<double>(numSpines, 0.0),
                                      true});
        return epsWecmpPairStates.back();
    };

    auto resetEpsPhysicalObservedTraffic = [&]() {
        for (auto& leafStates : epsPhysicalLinkStates)
        {
            for (auto& linkState : leafStates)
            {
                linkState.observedTraffic = 0.0;
            }
        }
    };

    auto accumulateEpsResidualTraffic = [&](uint32_t srcLeaf,
                                            uint32_t dstLeaf,
                                            double residualDemand) {
        if (residualDemand <= 0 || numSpines == 0)
        {
            return;
        }

        EpsWecmpPairState& pairState = getOrCreateEpsWecmpPairState(srcLeaf, dstLeaf);
        for (uint32_t spineIndex = 0; spineIndex < numSpines; ++spineIndex)
        {
            const double allocation = residualDemand * pairState.probabilities[spineIndex];
            epsPhysicalLinkStates[srcLeaf][spineIndex].observedTraffic += allocation;
            epsPhysicalLinkStates[dstLeaf][spineIndex].observedTraffic += allocation;
        }
    };

    auto updateEpsPhysicalSmoothedUtilization = [&]() {
        for (auto& leafStates : epsPhysicalLinkStates)
        {
            for (auto& linkState : leafStates)
            {
                linkState.utilization = linkState.observedTraffic / epsWecmpCapacity;
                linkState.smoothedUtilization =
                    epsWecmpRho * linkState.smoothedUtilization +
                    (1.0 - epsWecmpRho) * linkState.utilization;
            }
        }
    };

    bool epsWecmpDiagnosticLoadApplied = false;
    uint32_t epsWecmpDiagnosticLoadApplyCount = 0;
    double epsWecmpDiagnosticTotalInjected = 0.0;
    auto applyEpsWecmpDiagnosticLoad = [&]() {
        if (epsWecmpDiagnosticLoadMode != "hot-spine" ||
            epsWecmpDiagnosticLoad <= 0 ||
            epsWecmpDiagnosticHotSpine >= numSpines)
        {
            return false;
        }

        for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
        {
            epsPhysicalLinkStates[leafIndex][epsWecmpDiagnosticHotSpine].observedTraffic +=
                epsWecmpDiagnosticLoad;
        }
        epsWecmpDiagnosticLoadApplied = true;
        epsWecmpDiagnosticLoadApplyCount++;
        epsWecmpDiagnosticTotalInjected +=
            epsWecmpDiagnosticLoad * static_cast<double>(numLeaves);
        return true;
    };

    auto getMeasuredWecmpPathLoad = [&](uint32_t srcLeaf,
                                        uint32_t dstLeaf,
                                        uint32_t spineIndex) {
        MeasuredWecmpPathLoad pathLoad{false, 0.0, 0.0, 0.0};
        if (srcLeaf >= epsMeasuredLeafToSpineSnapshots.size() ||
            dstLeaf >= epsMeasuredSpineToLeafSnapshots.size() ||
            spineIndex >= epsMeasuredLeafToSpineSnapshots[srcLeaf].size() ||
            spineIndex >= epsMeasuredSpineToLeafSnapshots[dstLeaf].size())
        {
            return pathLoad;
        }

        const auto& srcToSpine = epsMeasuredLeafToSpineSnapshots[srcLeaf][spineIndex];
        const auto& spineToDst = epsMeasuredSpineToLeafSnapshots[dstLeaf][spineIndex];
        pathLoad.hasSample = srcToSpine.hasSample && spineToDst.hasSample;
        pathLoad.srcToSpineUtilization = srcToSpine.utilizationApprox;
        pathLoad.spineToDstUtilization = spineToDst.utilizationApprox;
        if (epsWecmpPathMetric == "max")
        {
            pathLoad.pathUtilization =
                std::max(pathLoad.srcToSpineUtilization,
                         pathLoad.spineToDstUtilization);
        }
        else
        {
            pathLoad.pathUtilization =
                0.5 * (pathLoad.srcToSpineUtilization +
                       pathLoad.spineToDstUtilization);
        }
        return pathLoad;
    };

    bool measuredWecmpNoSampleError = false;
    auto runEpsWecmpUpdateForPair = [&](uint32_t srcLeaf,
                                        uint32_t dstLeaf,
                                        double residualDemand,
                                        bool recordDecision,
                                        const std::string& decisionLoadSource,
                                        bool appliesToLaterFlow,
                                        bool commitPairState) {
        EpsWecmpDecision decision{enableEpsWecmp && recordDecision,
                                  srcLeaf,
                                  dstLeaf,
                                  residualDemand,
                                  0,
                                  {}};
        decision.loadSource = decisionLoadSource;
        decision.noSampleFallbackMode = measuredWecmpNoSampleFallback;
        decision.decisionTimeSeconds = Simulator::Now().GetSeconds();
        decision.measuredDecisionRequested = decisionLoadSource == "measured-snapshot";
        decision.appliesToLaterFlow = appliesToLaterFlow;
        if (!enableEpsWecmp || numSpines == 0 || residualDemand <= 0)
        {
            return decision;
        }

        EpsWecmpPairState& pairState = getOrCreateEpsWecmpPairState(srcLeaf, dstLeaf);
        const double initialProbability = 1.0 / static_cast<double>(numSpines);
        std::vector<double> currentProbabilities = pairState.probabilities;
        const std::vector<double> decisionStartProbabilities = pairState.probabilities;
        std::vector<double> boundedProbabilityDeltas(numSpines, 0.0);
        decision.linkStates.reserve(numSpines);
        for (uint32_t spineIndex = 0; spineIndex < numSpines; ++spineIndex)
        {
            decision.linkStates.push_back({spineIndex,
                                           0.0,
                                           0.0,
                                           pairState.smoothedUtilizations[spineIndex],
                                           0.0,
                                           0.0,
                                           initialProbability,
                                           currentProbabilities[spineIndex],
                                           currentProbabilities[spineIndex],
                                           0.0,
                                           0.0,
                                           pairState.smoothedUtilizations[spineIndex],
                                           0.0});
        }

        for (uint32_t epsEpoch = 0; epsEpoch < epsWecmpEpochs; ++epsEpoch)
        {
            double sumAttractiveness = 0.0;
            for (auto& state : decision.linkStates)
            {
                const auto& srcLinkState = epsPhysicalLinkStates[srcLeaf][state.spineIndex];
                const auto& dstLinkState = epsPhysicalLinkStates[dstLeaf][state.spineIndex];
                if (epsWecmpPathMetric == "max")
                {
                    state.observedTraffic =
                        std::max(srcLinkState.observedTraffic, dstLinkState.observedTraffic);
                    state.smoothedUtilization = std::max(srcLinkState.smoothedUtilization,
                                                         dstLinkState.smoothedUtilization);
                }
                else
                {
                    state.observedTraffic =
                        0.5 * (srcLinkState.observedTraffic + dstLinkState.observedTraffic);
                    state.smoothedUtilization =
                        0.5 * (srcLinkState.smoothedUtilization +
                               dstLinkState.smoothedUtilization);
                }
                state.utilization = state.smoothedUtilization;
                state.pathLoadMetric = state.smoothedUtilization;
                state.candidatePathLoad = state.observedTraffic;
                state.controlPlanePathLoadMetric = state.pathLoadMetric;
                state.effectivePathLoadMetric = state.pathLoadMetric;
                if (decision.measuredDecisionRequested)
                {
                    const MeasuredWecmpPathLoad measuredPathLoad =
                        getMeasuredWecmpPathLoad(srcLeaf, dstLeaf, state.spineIndex);
                    state.hasMeasuredSample = measuredPathLoad.hasSample;
                    state.measuredSrcToSpineUtilization =
                        measuredPathLoad.srcToSpineUtilization;
                    state.measuredSpineToDstUtilization =
                        measuredPathLoad.spineToDstUtilization;
                    state.measuredPathUtilization =
                        measuredPathLoad.pathUtilization;
                    if (measuredPathLoad.hasSample)
                    {
                        decision.measuredDecisionUsed = true;
                        state.pathLoadMetric = measuredPathLoad.pathUtilization;
                        state.smoothedUtilization = measuredPathLoad.pathUtilization;
                        state.utilization = measuredPathLoad.pathUtilization;
                        state.effectivePathLoadMetric = measuredPathLoad.pathUtilization;
                    }
                    else
                    {
                        decision.measuredNoSample = true;
                        decision.measuredDecisionFallback = true;
                        if (measuredWecmpNoSampleFallback == "zero")
                        {
                            state.pathLoadMetric = 0.0;
                            state.smoothedUtilization = 0.0;
                            state.utilization = 0.0;
                            state.effectivePathLoadMetric = 0.0;
                        }
                        else if (measuredWecmpNoSampleFallback == "error")
                        {
                            measuredWecmpNoSampleError = true;
                        }
                    }
                }
                state.attractiveness =
                    1.0 /
                    (std::pow(state.smoothedUtilization, epsWecmpGamma) + epsWecmpEpsilon);
                sumAttractiveness += state.attractiveness;
            }

            double probabilitySum = 0.0;
            for (auto& state : decision.linkStates)
            {
                const double previousProbability = currentProbabilities[state.spineIndex];
                state.previousProbability = previousProbability;
                state.targetProbability =
                    sumAttractiveness > 0 ? state.attractiveness / sumAttractiveness
                                          : initialProbability;
                state.normalizedAttractiveness = state.targetProbability;
                const double rawUpdated =
                    (1.0 - epsWecmpKappa) * previousProbability +
                    epsWecmpKappa * state.targetProbability;
                const double lower = previousProbability - epsWecmpMaxDelta;
                const double upper = previousProbability + epsWecmpMaxDelta;
                state.updatedProbability = std::max(0.0, std::min(rawUpdated, upper));
                state.updatedProbability = std::max(state.updatedProbability, lower);
                probabilitySum += state.updatedProbability;
            }

            if (probabilitySum > 0)
            {
                for (auto& state : decision.linkStates)
                {
                    state.updatedProbability /= probabilitySum;
                }
            }
            else
            {
                for (auto& state : decision.linkStates)
                {
                    state.updatedProbability = initialProbability;
                }
            }

            for (auto& state : decision.linkStates)
            {
                currentProbabilities[state.spineIndex] = state.updatedProbability;
                boundedProbabilityDeltas[state.spineIndex] =
                    state.updatedProbability - state.previousProbability;
            }
        }

        for (auto& state : decision.linkStates)
        {
            state.previousProbability = decisionStartProbabilities[state.spineIndex];
            state.probabilityDelta = state.updatedProbability - state.previousProbability;
            state.boundedProbabilityDelta = boundedProbabilityDeltas[state.spineIndex];
        }

        if (commitPairState)
        {
            for (const auto& state : decision.linkStates)
            {
                pairState.probabilities[state.spineIndex] = state.updatedProbability;
                pairState.smoothedUtilizations[state.spineIndex] = state.smoothedUtilization;
            }
        }

        for (const auto& state : decision.linkStates)
        {
            const auto& selectedState = decision.linkStates[decision.selectedSpine];
            if (state.updatedProbability > selectedState.updatedProbability ||
                (state.updatedProbability == selectedState.updatedProbability &&
                 state.spineIndex < selectedState.spineIndex))
            {
                decision.selectedSpine = state.spineIndex;
            }
        }
        decision.controlPlaneSelectedSpine = decision.selectedSpine;

        if (!recordDecision)
        {
            decision.linkStates.clear();
        }

        return decision;
    };

    auto isEdgeInSet = [](const std::vector<OcsCandidateEdge>& edges,
                          uint32_t leafA,
                          uint32_t leafB) {
        for (const auto& edge : edges)
        {
            if ((edge.leafA == leafA && edge.leafB == leafB) ||
                (edge.leafA == leafB && edge.leafB == leafA))
            {
                return true;
            }
        }
        return false;
    };

    auto getEpochMatrixMode = [&](uint32_t epoch) {
        if (trafficMatrixSequence == "static")
        {
            return trafficMatrixMode;
        }
        if (trafficMatrixSequence == "skewed-to-clustered")
        {
            return epoch < (controlEpochs / 2) ? std::string("skewed")
                                               : std::string("clustered");
        }
        if (trafficMatrixSequence == "clustered-to-skewed")
        {
            return epoch < (controlEpochs / 2) ? std::string("clustered")
                                               : std::string("skewed");
        }
        return epoch % 2 == 0 ? std::string("skewed") : std::string("clustered");
    };

    auto recomputeTrafficMetrics = [&](const WeightedMatrix& matrix) {
        nodeDegree.assign(numLeaves, 0.0);
        for (uint32_t i = 0; i < numLeaves; ++i)
        {
            for (uint32_t j = 0; j < numLeaves; ++j)
            {
                nodeDegree[i] += matrix[i][j];
            }
        }

        totalTraffic = computeTotalTraffic(matrix);
        expectedTraffic.assign(numLeaves, std::vector<double>(numLeaves, 0.0));
        modularityGain.assign(numLeaves, std::vector<double>(numLeaves, 0.0));
        ocsUtility.assign(numLeaves, std::vector<double>(numLeaves, 0.0));
        if (totalTraffic <= 0)
        {
            return;
        }

        for (uint32_t i = 0; i < numLeaves; ++i)
        {
            for (uint32_t j = 0; j < numLeaves; ++j)
            {
                if (i == j)
                {
                    continue;
                }
                expectedTraffic[i][j] = nodeDegree[i] * nodeDegree[j] / (2.0 * totalTraffic);
                modularityGain[i][j] = matrix[i][j] - eta * expectedTraffic[i][j];
                ocsUtility[i][j] = std::max(modularityGain[i][j], 0.0);
            }
        }
    };

    auto buildPreviousStateFromEdges = [&](const std::vector<OcsCandidateEdge>& edges) {
        std::vector<std::vector<bool>> state(numLeaves, std::vector<bool>(numLeaves, false));
        for (const auto& edge : edges)
        {
            state[edge.leafA][edge.leafB] = true;
            state[edge.leafB][edge.leafA] = true;
        }
        return state;
    };

    auto runControlDecisionForMatrix =
        [&](const WeightedMatrix& matrix,
            const std::string& matrixModeName,
            const std::vector<OcsCandidateEdge>& epochPreviousEdges,
            const OcsEdgeAgeMatrix& epochPreviousAgeMatrix,
            uint32_t epochPreviousAge,
            uint32_t epoch) {
            std::vector<double> epochDegree = computeNodeDegree(matrix);
            const double epochTotalTraffic = computeTotalTraffic(matrix);
            WeightedMatrix epochExpected(numLeaves, std::vector<double>(numLeaves, 0.0));
            WeightedMatrix epochGain(numLeaves, std::vector<double>(numLeaves, 0.0));
            WeightedMatrix epochUtility(numLeaves, std::vector<double>(numLeaves, 0.0));
            if (epochTotalTraffic > 0)
            {
                for (uint32_t i = 0; i < numLeaves; ++i)
                {
                    for (uint32_t j = 0; j < numLeaves; ++j)
                    {
                        if (i == j)
                        {
                            continue;
                        }
                        epochExpected[i][j] =
                            epochDegree[i] * epochDegree[j] / (2.0 * epochTotalTraffic);
                        epochGain[i][j] = matrix[i][j] - eta * epochExpected[i][j];
                        epochUtility[i][j] = std::max(epochGain[i][j], 0.0);
                    }
                }
            }

            const CommunityPreview epochPreview =
                buildCommunityPreview(matrixModeName, numLeaves);
            LouvainResult epochLouvain{epochPreview.labels,
                                       epochPreview.communityCount,
                                       0,
                                       false,
                                       computeModularityQ(matrix, epochPreview.labels, eta),
                                       0,
                                       false,
                                       {}};
            if (communityMode == "louvain")
            {
                epochLouvain =
                    louvainMode == "multi-level"
                        ? runMultiLevelLouvain(matrix,
                                               numLeaves,
                                               louvainMaxPasses,
                                               louvainMaxLevels,
                                               louvainEpsilon,
                                               eta)
                        : runSingleLevelLouvain(matrix,
                                                numLeaves,
                                                louvainMaxPasses,
                                                louvainEpsilon,
                                                eta);
            }

            std::vector<uint32_t> epochLabels =
                communityMode == "louvain" ? epochLouvain.labels : epochPreview.labels;
            const uint32_t epochCommunityCount =
                communityMode == "louvain" ? epochLouvain.communityCount
                                           : epochPreview.communityCount;
            auto epochIsIntraCommunity = [&epochLabels](uint32_t leafA, uint32_t leafB) {
                return epochLabels[leafA] == epochLabels[leafB];
            };
            const std::vector<std::vector<bool>> epochPreviousState =
                buildPreviousStateFromEdges(epochPreviousEdges);

            auto makeEpochCandidateEdge = [&](uint32_t leafA, uint32_t leafB) {
                const double baseUtility = epochUtility[leafA][leafB];
                const bool intraCommunity = epochIsIntraCommunity(leafA, leafB);
                const double communityFactor = intraCommunity ? 1.0 : communityAlpha;
                const double communityUtility = baseUtility * communityFactor;
                const bool wasPreviouslyInstalled = epochPreviousState[leafA][leafB];
                const double stateHoldingGain =
                    enableStateHolding && wasPreviouslyInstalled ? stateHoldingLambda : 0.0;
                OcsCandidateEdge edge{leafA,
                                      leafB,
                                      matrix[leafA][leafB],
                                      epochExpected[leafA][leafB],
                                      epochGain[leafA][leafB],
                                      0.0,
                                      baseUtility,
                                      communityFactor,
                                      communityUtility,
                                      intraCommunity,
                                      wasPreviouslyInstalled,
                                      stateHoldingGain,
                                      0.0};
                edge.utility = edgeScoreWithoutState(edge);
                edge.selectionScore = edge.utility + edge.stateHoldingGain;
                return edge;
            };

            std::vector<OcsCandidateEdge> epochPreviousOcsEdges;
            for (const auto& edge : epochPreviousEdges)
            {
                epochPreviousOcsEdges.push_back(makeEpochCandidateEdge(edge.leafA, edge.leafB));
            }

            std::vector<OcsCandidateEdge> epochCandidateEdges;
            for (uint32_t leafA = 0; leafA < numLeaves; ++leafA)
            {
                for (uint32_t leafB = leafA + 1; leafB < numLeaves; ++leafB)
                {
                    const OcsCandidateEdge edge = makeEpochCandidateEdge(leafA, leafB);
                    if (edge.selectionScore > 0)
                    {
                        epochCandidateEdges.push_back(edge);
                    }
                }
            }
            std::sort(epochCandidateEdges.begin(),
                      epochCandidateEdges.end(),
                      [](const OcsCandidateEdge& lhs, const OcsCandidateEdge& rhs) {
                          if (lhs.selectionScore != rhs.selectionScore)
                          {
                              return lhs.selectionScore > rhs.selectionScore;
                          }
                          if (lhs.leafA != rhs.leafA)
                          {
                              return lhs.leafA < rhs.leafA;
                          }
                          return lhs.leafB < rhs.leafB;
                      });

            std::vector<uint32_t> epochOcsDegree(numLeaves, 0);
            std::vector<OcsCandidateEdge> epochCandidateOcsEdges;
            std::vector<OcsCandidateEdge> epochHoldOcsEdges;
            const bool epochPreviousConfigAvailable = !epochPreviousOcsEdges.empty();
            if (enableHoldTimeGate && epochPreviousConfigAvailable)
            {
                for (const auto& edge : epochPreviousOcsEdges)
                {
                    if (GetOcsEdgeAge(epochPreviousAgeMatrix, edge.leafA, edge.leafB) <
                        minHoldCycles)
                    {
                        epochHoldOcsEdges.push_back(edge);
                    }
                }
            }

            auto isEpochHeldEdge = [&epochHoldOcsEdges](uint32_t leafA, uint32_t leafB) {
                for (const auto& heldEdge : epochHoldOcsEdges)
                {
                    if ((heldEdge.leafA == leafA && heldEdge.leafB == leafB) ||
                        (heldEdge.leafA == leafB && heldEdge.leafB == leafA))
                    {
                        return true;
                    }
                }
                return false;
            };

            for (const auto& edge : epochHoldOcsEdges)
            {
                if (epochOcsDegree[edge.leafA] >= ocsPortK ||
                    epochOcsDegree[edge.leafB] >= ocsPortK)
                {
                    std::cerr << "[HYBRID-DCN][ERROR] hold OCS edge exceeds ocsPortK."
                              << std::endl;
                    const ConfigScoreBreakdown epochPreviousBreakdown =
                        computeConfigScore(epochPreviousOcsEdges,
                                           epochPreviousOcsEdges,
                                           configScoreMode);
                    ControlEpochSummary summary{epoch,
                                                matrixModeName,
                                                epochCommunityCount,
                                                static_cast<uint32_t>(epochCandidateEdges.size()),
                                                0,
                                                static_cast<uint32_t>(epochPreviousOcsEdges.size()),
                                                0,
                                                static_cast<uint32_t>(epochHoldOcsEdges.size()),
                                                0.0,
                                                epochPreviousBreakdown.score,
                                                0.0,
                                                0.0,
                                                epochPreviousBreakdown.rawUtilitySum,
                                                0.0,
                                                epochPreviousBreakdown.selectionScoreSum,
                                                0,
                                                epochPreviousBreakdown.changedEdgeCount,
                                                configScoreMode,
                                                !epochHoldOcsEdges.empty(),
                                                "error",
                                                epochPreviousAge,
                                                epochPreviousAge,
                                                0,
                                                0,
                                                0,
                                                0};
                    return OcsControllerDecision{epochCandidateEdges,
                                                 {},
                                                 epochPreviousOcsEdges,
                                                 {},
                                                 epochHoldOcsEdges,
                                                 epochPreviousAgeMatrix,
                                                 MakeZeroOcsEdgeAgeMatrix(numLeaves),
                                                 epochLouvain,
                                                 epochPreview,
                                                 epochLabels,
                                                 epochCommunityCount,
                                                 0.0,
                                                 epochPreviousBreakdown.score,
                                                 0.0,
                                                 0.0,
                                                 epochPreviousBreakdown.rawUtilitySum,
                                                 0.0,
                                                 epochPreviousBreakdown.selectionScoreSum,
                                                 0,
                                                 epochPreviousBreakdown.changedEdgeCount,
                                                 !epochHoldOcsEdges.empty(),
                                                 "error",
                                                 epochPreviousAge,
                                                 summary,
                                                 false};
                }
                epochCandidateOcsEdges.push_back(edge);
                epochOcsDegree[edge.leafA]++;
                epochOcsDegree[edge.leafB]++;
            }

            for (const auto& edge : epochCandidateEdges)
            {
                if (epochCandidateOcsEdges.size() >= maxSelectedOcsLinks)
                {
                    break;
                }
                if (!isEpochHeldEdge(edge.leafA, edge.leafB) &&
                    epochOcsDegree[edge.leafA] < ocsPortK &&
                    epochOcsDegree[edge.leafB] < ocsPortK)
                {
                    epochCandidateOcsEdges.push_back(edge);
                    epochOcsDegree[edge.leafA]++;
                    epochOcsDegree[edge.leafB]++;
                }
            }

            const ConfigScoreBreakdown epochCandidateBreakdown =
                computeConfigScore(epochCandidateOcsEdges,
                                   epochPreviousOcsEdges,
                                   configScoreMode);
            const ConfigScoreBreakdown epochPreviousBreakdown =
                computeConfigScore(epochPreviousOcsEdges,
                                   epochPreviousOcsEdges,
                                   configScoreMode);
            const double epochCandidateConfigScore = epochCandidateBreakdown.score;
            const double epochPreviousConfigScore = epochPreviousBreakdown.score;
            const double epochConfigScoreImprovement =
                epochCandidateConfigScore - epochPreviousConfigScore;
            const bool epochHoldTimeActive = !epochHoldOcsEdges.empty();
            std::string epochDecision = "disabled";
            std::vector<OcsCandidateEdge> epochSelectedEdges;
            if (!enableConfigUpdateGate)
            {
                epochSelectedEdges = epochCandidateOcsEdges;
            }
            else if (epochConfigScoreImprovement > configUpdateThreshold)
            {
                epochSelectedEdges = epochCandidateOcsEdges;
                epochDecision = "use-candidate";
            }
            else
            {
                epochSelectedEdges = epochPreviousOcsEdges;
                epochDecision = "keep-previous";
            }

            OcsEdgeAgeMatrix epochSelectedAgeMatrix =
                UpdateOcsEdgeAges(epochPreviousAgeMatrix,
                                  epochPreviousOcsEdges,
                                  epochSelectedEdges);
            const auto selectedAgeRange =
                GetOcsEdgeAgeRange(epochSelectedAgeMatrix, epochSelectedEdges);
            const auto holdAgeRange =
                GetOcsEdgeAgeRange(epochPreviousAgeMatrix, epochHoldOcsEdges);
            const uint32_t epochAgeAfter = selectedAgeRange.second;

            ControlEpochSummary summary{epoch,
                                        matrixModeName,
                                        epochCommunityCount,
                                        static_cast<uint32_t>(epochCandidateEdges.size()),
                                        static_cast<uint32_t>(epochCandidateOcsEdges.size()),
                                        static_cast<uint32_t>(epochPreviousOcsEdges.size()),
                                        static_cast<uint32_t>(epochSelectedEdges.size()),
                                        static_cast<uint32_t>(epochHoldOcsEdges.size()),
                                        epochCandidateConfigScore,
                                        epochPreviousConfigScore,
                                        epochConfigScoreImprovement,
                                        epochCandidateBreakdown.rawUtilitySum,
                                        epochPreviousBreakdown.rawUtilitySum,
                                        epochCandidateBreakdown.selectionScoreSum,
                                        epochPreviousBreakdown.selectionScoreSum,
                                        epochCandidateBreakdown.changedEdgeCount,
                                        epochPreviousBreakdown.changedEdgeCount,
                                        configScoreMode,
                                        epochHoldTimeActive,
                                        epochDecision,
                                        epochPreviousAge,
                                        epochAgeAfter,
                                        selectedAgeRange.first,
                                        selectedAgeRange.second,
                                        holdAgeRange.first,
                                        holdAgeRange.second};

            return OcsControllerDecision{epochCandidateEdges,
                                         epochCandidateOcsEdges,
                                         epochPreviousOcsEdges,
                                         epochSelectedEdges,
                                         epochHoldOcsEdges,
                                         epochPreviousAgeMatrix,
                                         epochSelectedAgeMatrix,
                                         epochLouvain,
                                         epochPreview,
                                         epochLabels,
                                         epochCommunityCount,
                                         epochCandidateConfigScore,
                                         epochPreviousConfigScore,
                                         epochConfigScoreImprovement,
                                         epochCandidateBreakdown.rawUtilitySum,
                                         epochPreviousBreakdown.rawUtilitySum,
                                         epochCandidateBreakdown.selectionScoreSum,
                                         epochPreviousBreakdown.selectionScoreSum,
                                         epochCandidateBreakdown.changedEdgeCount,
                                         epochPreviousBreakdown.changedEdgeCount,
                                         epochHoldTimeActive,
                                         epochDecision,
                                         epochAgeAfter,
                                         summary,
                                         true};
        };

    const bool multiPeriodWecmpStateEnabled =
        enableMultiPeriodControl && enableMultiPeriodWecmpState && enableEpsWecmp;

    if (enableMultiPeriodControl)
    {
        std::vector<OcsCandidateEdge> epochPreviousEdges = previousOcsEdges;
        OcsEdgeAgeMatrix epochPreviousAgeMatrix = previousOcsEdgeAges;
        uint32_t epochPreviousAge = previousConfigAge;
        WeightedMatrix previousAbar;
        bool hasPreviousAbar = false;
        for (uint32_t epoch = 0; epoch < controlEpochs; ++epoch)
        {
            const std::string epochMatrixMode = getEpochMatrixMode(epoch);
            const DirectedTrafficMatrix currentW =
                buildSyntheticDirectedTrafficMatrix(epochMatrixMode, numLeaves);
            const WeightedMatrix currentA =
                buildUndirectedCommunicationIntensityMatrix(currentW);
            const WeightedMatrix currentTrafficGraph =
                applyTrafficGraphThreshold(currentA, trafficGraphThreshold);
            const WeightedMatrix epochControlMatrix =
                enableEwmaSmoothing
                    ? updateEwmaMatrix(previousAbar,
                                       currentTrafficGraph,
                                       ewmaBeta,
                                       hasPreviousAbar)
                    : currentTrafficGraph;
            if (enableEwmaSmoothing)
            {
                previousAbar = epochControlMatrix;
                hasPreviousAbar = true;
            }
            OcsControllerDecision epochDecision =
                runControlDecisionForMatrix(epochControlMatrix,
                                            epochMatrixMode,
                                            epochPreviousEdges,
                                            epochPreviousAgeMatrix,
                                            epochPreviousAge,
                                            epoch);
            if (!epochDecision.success)
            {
                return 1;
            }

            candidateEdges = epochDecision.candidateEdges;
            candidateOcsEdges = epochDecision.candidateOcsEdges;
            previousOcsEdges = epochDecision.previousOcsEdges;
            selectedOcsEdges = epochDecision.selectedOcsEdges;
            holdOcsEdges = epochDecision.holdOcsEdges;
            previousOcsEdgeAges = epochDecision.previousOcsEdgeAges;
            selectedOcsEdgeAges = epochDecision.selectedOcsEdgeAges;
            louvainResult = epochDecision.louvain;
            activeCommunityLabels = epochDecision.communityLabels;
            activeCommunityCount = epochDecision.communityCount;
            candidateConfigScore = epochDecision.candidateConfigScore;
            previousConfigScore = epochDecision.previousConfigScore;
            configScoreImprovement = epochDecision.configScoreImprovement;
            candidateRawUtilitySum = epochDecision.candidateRawUtilitySum;
            previousRawUtilitySum = epochDecision.previousRawUtilitySum;
            candidateSelectionScoreSum = epochDecision.candidateSelectionScoreSum;
            previousSelectionScoreSum = epochDecision.previousSelectionScoreSum;
            candidateChangedEdges = epochDecision.candidateChangedEdges;
            previousChangedEdges = epochDecision.previousChangedEdges;
            holdTimeActive = epochDecision.holdTimeActive;
            configGateDecision = epochDecision.decision;
            epochPreviousAge = epochDecision.selectedConfigAge;
            matrixEpochSummaries.push_back({epoch,
                                            epochMatrixMode,
                                            enableEwmaSmoothing ? "ewma" : "raw",
                                            enableEwmaSmoothing,
                                            ewmaBeta,
                                            computeTotalTraffic(currentA),
                                            computeTotalTraffic(epochControlMatrix),
                                            numLeaves >= 4 ? currentA[0][3] : 0.0,
                                            numLeaves >= 4 ? epochControlMatrix[0][3] : 0.0,
                                            numLeaves >= 3 ? currentA[1][2] : 0.0,
                                            numLeaves >= 3 ? epochControlMatrix[1][2] : 0.0});
            controlEpochSummaries.push_back(epochDecision.summary);

            if (multiPeriodWecmpStateEnabled)
            {
                EpsWecmpEpochSummary wecmpSummary{epoch, epochMatrixMode, 0, 0, 0.0, 0.0};
                struct EpochResidualPair
                {
                    uint32_t leafA;
                    uint32_t leafB;
                    double residualDemand;
                };
                std::vector<EpochResidualPair> epochResidualPairs;
                resetEpsPhysicalObservedTraffic();
                for (uint32_t leafA = 0; leafA < numLeaves; ++leafA)
                {
                    for (uint32_t leafB = leafA + 1; leafB < numLeaves; ++leafB)
                    {
                        const double demand = currentA[leafA][leafB];
                        const bool pairSelected =
                            isEdgeInSet(epochDecision.selectedOcsEdges, leafA, leafB);
                        double plannedResidualDemand = 0.0;
                        if (pairSelected)
                        {
                            plannedResidualDemand =
                                enableOcsAdmissionControl
                                    ? std::max(demand - ocsAdmissionThreshold, 0.0)
                                    : 0.0;
                        }
                        else
                        {
                            plannedResidualDemand = demand;
                        }

                        const double realResidualDemand = plannedResidualDemand;
                        if (plannedResidualDemand <= 0)
                        {
                            continue;
                        }

                        wecmpSummary.residualPairs++;
                        wecmpSummary.totalPlannedResidualDemand += plannedResidualDemand;
                        wecmpSummary.totalRealResidualDemand += realResidualDemand;
                        epochResidualPairs.push_back({leafA, leafB, plannedResidualDemand});
                        accumulateEpsResidualTraffic(leafA, leafB, plannedResidualDemand);
                    }
                }

                applyEpsWecmpDiagnosticLoad();
                updateEpsPhysicalSmoothedUtilization();
                for (const auto& residualPair : epochResidualPairs)
                {
                    runEpsWecmpUpdateForPair(residualPair.leafA,
                                             residualPair.leafB,
                                             residualPair.residualDemand,
                                             false,
                                             epsWecmpLoadSource,
                                             false,
                                             true);
                    wecmpSummary.updatedPairs++;
                }
                epsWecmpEpochSummaries.push_back(wecmpSummary);
            }

            epochPreviousEdges = selectedOcsEdges;
            epochPreviousAgeMatrix = selectedOcsEdgeAges;
            finalEpochMatrixMode = epochMatrixMode;
            rawTrafficMatrix = currentA;
            controlTrafficMatrix = epochControlMatrix;
            torTrafficMatrix = controlTrafficMatrix;
            matrixUsedForControl = enableEwmaSmoothing ? "ewma" : "raw";
        }

        trafficMatrixMode = finalEpochMatrixMode;
        previousConfigAge =
            controlEpochSummaries.empty() ? previousConfigAge
                                          : controlEpochSummaries.back().previousConfigAgeAfter;
        recomputeTrafficMetrics(controlTrafficMatrix);
        previousConfigAvailable = !selectedOcsEdges.empty();

        intraCandidateEdges = 0;
        interCandidateEdges = 0;
        for (uint32_t leafA = 0; leafA < numLeaves; ++leafA)
        {
            for (uint32_t leafB = leafA + 1; leafB < numLeaves; ++leafB)
            {
                if (torTrafficMatrix[leafA][leafB] <= 0)
                {
                    continue;
                }
                if (isIntraCommunity(leafA, leafB))
                {
                    intraCandidateEdges++;
                }
                else
                {
                    interCandidateEdges++;
                }
            }
        }
    }

    if (enableMatrixSelect)
    {
        if (candidateOcsEdges.empty())
        {
            std::cerr << "[HYBRID-DCN][ERROR] no OCS candidate edge selected." << std::endl;
            return 1;
        }

        if (selectedOcsEdges.empty())
        {
            std::cerr << "[HYBRID-DCN][ERROR] no final OCS edge selected." << std::endl;
            return 1;
        }

        const OcsCandidateEdge& instantiatedEdge = selectedOcsEdges.front();
        ocsLeafA = instantiatedEdge.leafA;
        ocsLeafB = instantiatedEdge.leafB;
        selectedOcsWeight = instantiatedEdge.traffic;
        selectedExpectedTraffic = instantiatedEdge.expected;
        selectedModularityGain = instantiatedEdge.modularityGain;
        selectedOcsUtility = instantiatedEdge.utility;
        selectedBaseUtility = instantiatedEdge.baseUtility;
        selectedCommunityFactor = instantiatedEdge.communityFactor;
        selectedCommunityUtility = instantiatedEdge.communityUtility;
        selectedIntraCommunity = instantiatedEdge.intraCommunity;
        selectedWasPreviouslyInstalled = instantiatedEdge.wasPreviouslyInstalled;
        selectedStateHoldingGain = instantiatedEdge.stateHoldingGain;
        selectedSelectionScore = instantiatedEdge.selectionScore;

        if (routeMode != "ocs-forced")
        {
            std::cout << "[HYBRID-DCN][MATRIX][WARN] Matrix-selected OCS pair is most meaningful with routeMode=ocs-forced."
                      << std::endl;
        }
    }

    if (enableEcho)
    {
        if (numLeaves < 4)
        {
            std::cerr << "[HYBRID-DCN][ERROR] numLeaves must be at least 4 when enableEcho is true."
                      << std::endl;
            return 1;
        }

        if (serversPerLeaf < 1)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] serversPerLeaf must be at least 1 when enableEcho is true."
                << std::endl;
            return 1;
        }

        if (echoPacketSize == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] echoPacketSize must be greater than 0."
                      << std::endl;
            return 1;
        }

        if (echoInterval <= 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] echoInterval must be greater than 0." << std::endl;
            return 1;
        }

        if (echoCount == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] echoCount must be greater than 0." << std::endl;
            return 1;
        }

        if (simTime <= 0.3)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] simTime must be greater than 0.3 when enableEcho is true."
                << std::endl;
            return 1;
        }
    }

    if (enableBulk)
    {
        if (numLeaves < 4)
        {
            std::cerr << "[HYBRID-DCN][ERROR] numLeaves must be at least 4 when enableBulk is true."
                      << std::endl;
            return 1;
        }

        if (serversPerLeaf < 1)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] serversPerLeaf must be at least 1 when enableBulk is true."
                << std::endl;
            return 1;
        }

        if (bulkMaxBytes == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] bulkMaxBytes must be greater than 0." << std::endl;
            return 1;
        }

        if (bulkStart < 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] bulkStart must be greater than or equal to 0."
                      << std::endl;
            return 1;
        }

        if (bulkStart >= simTime)
        {
            std::cerr << "[HYBRID-DCN][ERROR] bulkStart must be less than simTime." << std::endl;
            return 1;
        }

        if ((simTime - bulkStart) <= 0.1)
        {
            std::cerr << "[HYBRID-DCN][ERROR] simTime - bulkStart must be greater than 0.1."
                      << std::endl;
            return 1;
        }

        if (bulkPort == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] bulkPort must be greater than 0." << std::endl;
            return 1;
        }
    }

    if (enableSecondBulk)
    {
        if (numLeaves < 3)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] numLeaves must be at least 3 when enableSecondBulk is true."
                << std::endl;
            return 1;
        }

        if (serversPerLeaf < 1)
        {
            std::cerr << "[HYBRID-DCN][ERROR] serversPerLeaf must be at least 1 when "
                         "enableSecondBulk is true."
                      << std::endl;
            return 1;
        }

        if (secondBulkPort == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] secondBulkPort must be greater than 0."
                      << std::endl;
            return 1;
        }

        if (secondBulkMaxBytes == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] secondBulkMaxBytes must be greater than 0."
                      << std::endl;
            return 1;
        }

        if (secondBulkStart < 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] secondBulkStart must be greater than or equal to 0."
                      << std::endl;
            return 1;
        }

        if (secondBulkStart >= simTime)
        {
            std::cerr << "[HYBRID-DCN][ERROR] secondBulkStart must be less than simTime."
                      << std::endl;
            return 1;
        }

        if ((simTime - secondBulkStart) <= 0.1)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] simTime - secondBulkStart must be greater than 0.1."
                << std::endl;
            return 1;
        }
    }

    if (enableResidualBulk)
    {
        if (numLeaves < 2)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] numLeaves must be at least 2 when enableResidualBulk is true."
                << std::endl;
            return 1;
        }

        if (serversPerLeaf < 2)
        {
            std::cerr << "[HYBRID-DCN][ERROR] enableResidualBulk requires serversPerLeaf >= 2."
                      << std::endl;
            return 1;
        }

        if (residualBulkPort == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] residualBulkPort must be greater than 0."
                      << std::endl;
            return 1;
        }

        if (residualBulkMaxBytes == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] residualBulkMaxBytes must be greater than 0."
                      << std::endl;
            return 1;
        }

        if (residualBulkStart < 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] residualBulkStart must be greater than or equal to 0."
                      << std::endl;
            return 1;
        }

        if (residualBulkStart >= simTime)
        {
            std::cerr << "[HYBRID-DCN][ERROR] residualBulkStart must be less than simTime."
                      << std::endl;
            return 1;
        }

        if ((simTime - residualBulkStart) <= 0.1)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] simTime - residualBulkStart must be greater than 0.1."
                << std::endl;
            return 1;
        }
    }

    if (enableMatrixFlows)
    {
        if (matrixFlowMaxBytes == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] matrixFlowMaxBytes must be greater than 0."
                      << std::endl;
            return 1;
        }

        if (matrixFlowStart < 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] matrixFlowStart must be greater than or equal to 0."
                      << std::endl;
            return 1;
        }

        if (matrixFlowStart >= simTime)
        {
            std::cerr << "[HYBRID-DCN][ERROR] matrixFlowStart must be less than simTime."
                      << std::endl;
            return 1;
        }

        if ((simTime - matrixFlowStart) <= 0.1)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] simTime - matrixFlowStart must be greater than 0.1."
                << std::endl;
            return 1;
        }

        if (matrixFlowPortBase == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] matrixFlowPortBase must be greater than 0."
                      << std::endl;
            return 1;
        }

        if (serversPerLeaf < 2)
        {
            std::cerr << "[HYBRID-DCN][ERROR] enableMatrixFlows requires serversPerLeaf >= 2."
                      << std::endl;
            return 1;
        }

        if (numLeaves < 4)
        {
            std::cerr << "[HYBRID-DCN][ERROR] enableMatrixFlows requires numLeaves >= 4."
                      << std::endl;
            return 1;
        }
    }

    if (enableStaticOcs)
    {
        if (numLeaves <= 1)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] numLeaves must be greater than 1 when enableStaticOcs is true."
                << std::endl;
            return 1;
        }

        if (ocsLeafA >= numLeaves)
        {
            std::cerr << "[HYBRID-DCN][ERROR] ocsLeafA must be less than numLeaves." << std::endl;
            return 1;
        }

        if (ocsLeafB >= numLeaves)
        {
            std::cerr << "[HYBRID-DCN][ERROR] ocsLeafB must be less than numLeaves." << std::endl;
            return 1;
        }

        if (ocsLeafA == ocsLeafB)
        {
            std::cerr << "[HYBRID-DCN][ERROR] ocsLeafA and ocsLeafB must be different."
                      << std::endl;
            return 1;
        }

        if (ocsDataRate.empty())
        {
            std::cerr << "[HYBRID-DCN][ERROR] ocsDataRate must not be empty." << std::endl;
            return 1;
        }

        if (ocsDelay.empty())
        {
            std::cerr << "[HYBRID-DCN][ERROR] ocsDelay must not be empty." << std::endl;
            return 1;
        }
    }

    if (routeMode == "ocs-forced")
    {
        if (!enableStaticOcs)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] routeMode=ocs-forced requires enableStaticOcs=true."
                << std::endl;
            return 1;
        }

        if (ocsLeafA == ocsLeafB)
        {
            std::cerr << "[HYBRID-DCN][ERROR] ocsLeafA and ocsLeafB must be different."
                      << std::endl;
            return 1;
        }
    }

    const uint32_t totalServers = numLeaves * serversPerLeaf;
    const uint32_t totalNodes = totalServers + numLeaves + numSpines;
    const uint32_t serverLeafLinks = totalServers;
    const uint32_t leafSpineLinks = numLeaves * numSpines;
    const uint32_t epsLinksCount = serverLeafLinks + leafSpineLinks;
    uint32_t staticOcsLinks = 0;
    uint32_t reservedOcsLinks = 0;

    NodeContainer servers;
    NodeContainer leaves;
    NodeContainer spines;
    servers.Create(totalServers);
    leaves.Create(numLeaves);
    spines.Create(numSpines);

    auto serverIndex = [serversPerLeaf](uint32_t leafIndex, uint32_t serverOffset) {
        return leafIndex * serversPerLeaf + serverOffset;
    };

    for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
    {
        std::ostringstream leafName;
        leafName << "leaf-" << leafIndex;
        Names::Add(leafName.str(), leaves.Get(leafIndex));

        for (uint32_t serverOffset = 0; serverOffset < serversPerLeaf; ++serverOffset)
        {
            std::ostringstream serverName;
            serverName << "server-l" << leafIndex << "-s" << serverOffset;
            Names::Add(serverName.str(), servers.Get(serverIndex(leafIndex, serverOffset)));
        }
    }

    for (uint32_t spineIndex = 0; spineIndex < numSpines; ++spineIndex)
    {
        std::ostringstream spineName;
        spineName << "spine-" << spineIndex;
        Names::Add(spineName.str(), spines.Get(spineIndex));
    }

    NodeContainer allNodes;
    allNodes.Add(servers);
    allNodes.Add(leaves);
    allNodes.Add(spines);

    InternetStackHelper internet;
    internet.Install(allNodes);

    PointToPointHelper serverLeafP2p;
    serverLeafP2p.SetDeviceAttribute("DataRate", StringValue("25Gbps"));
    serverLeafP2p.SetChannelAttribute("Delay", StringValue("1us"));

    PointToPointHelper leafSpineP2p;
    leafSpineP2p.SetDeviceAttribute("DataRate", StringValue("40Gbps"));
    leafSpineP2p.SetChannelAttribute("Delay", StringValue("2us"));

    std::vector<std::vector<Ipv4Address>> serverIpv4(numLeaves,
                                                     std::vector<Ipv4Address>(serversPerLeaf));
    std::vector<std::vector<Ipv4Address>> leafServerIpv4(
        numLeaves,
        std::vector<Ipv4Address>(serversPerLeaf));
    std::vector<std::vector<uint32_t>> serverIfIndex(numLeaves,
                                                     std::vector<uint32_t>(serversPerLeaf));
    std::vector<std::vector<uint32_t>> leafServerIfIndex(
        numLeaves,
        std::vector<uint32_t>(serversPerLeaf));
    std::vector<std::vector<Ipv4Address>> leafToSpineNextHop(
        numLeaves,
        std::vector<Ipv4Address>(numSpines, Ipv4Address("0.0.0.0")));
    std::vector<std::vector<Ipv4Address>> spineToLeafNextHop(
        numSpines,
        std::vector<Ipv4Address>(numLeaves, Ipv4Address("0.0.0.0")));
    std::vector<std::vector<uint32_t>> leafToSpineIfIndex(numLeaves,
                                                          std::vector<uint32_t>(numSpines, 0));
    std::vector<std::vector<uint32_t>> spineToLeafIfIndex(numSpines,
                                                          std::vector<uint32_t>(numLeaves, 0));
    std::vector<LinkCounter> linkCounters;

    auto dataRateToGbps = [](const std::string& dataRate) {
        return static_cast<double>(DataRate(dataRate).GetBitRate()) / 1e9;
    };
    const double leafSpineCapacityGbps = dataRateToGbps("40Gbps");
    const std::string leafSpineDelay = "2us";
    const double ocsCapacityGbps = dataRateToGbps(ocsDataRate);

    auto addLinkCounter = [&](const std::string& linkId,
                              const std::string& linkType,
                              const std::string& direction,
                              const std::string& endpointAType,
                              uint32_t endpointA,
                              const std::string& endpointBType,
                              uint32_t endpointB,
                              double capacityGbps,
                              const std::string& delay,
                              const std::string& note) {
        linkCounters.push_back({linkId,
                                linkType,
                                direction,
                                endpointAType,
                                endpointA,
                                endpointBType,
                                endpointB,
                                capacityGbps,
                                delay,
                                0,
                                0,
                                0,
                                0,
                                0.0,
                                note});
        return static_cast<uint32_t>(linkCounters.size() - 1);
    };

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.0.0.0", "255.255.255.252");

    for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
    {
        for (uint32_t serverOffset = 0; serverOffset < serversPerLeaf; ++serverOffset)
        {
            NodeContainer pair(servers.Get(serverIndex(leafIndex, serverOffset)),
                               leaves.Get(leafIndex));
            NetDeviceContainer devices = serverLeafP2p.Install(pair);
            Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
            serverIpv4[leafIndex][serverOffset] = interfaces.GetAddress(0);
            leafServerIpv4[leafIndex][serverOffset] = interfaces.GetAddress(1);
            serverIfIndex[leafIndex][serverOffset] = interfaces.Get(0).second;
            leafServerIfIndex[leafIndex][serverOffset] = interfaces.Get(1).second;
            ipv4.NewNetwork();
        }
    }

    for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
    {
        for (uint32_t spineIndex = 0; spineIndex < numSpines; ++spineIndex)
        {
            NodeContainer pair(leaves.Get(leafIndex), spines.Get(spineIndex));
            NetDeviceContainer devices = leafSpineP2p.Install(pair);
            devices.Get(0)->TraceConnectWithoutContext("MacTx", MakeCallback(&EpsTxTrace));
            devices.Get(1)->TraceConnectWithoutContext("MacTx", MakeCallback(&EpsTxTrace));
            std::ostringstream leafToSpineId;
            leafToSpineId << "eps-leaf" << leafIndex << "-spine" << spineIndex
                          << "-leaf-to-spine";
            const uint32_t leafToSpineCounterIndex =
                addLinkCounter(leafToSpineId.str(),
                               "eps-leaf-spine",
                               "a-to-b",
                               "leaf",
                               leafIndex,
                               "spine",
                               spineIndex,
                               leafSpineCapacityGbps,
                               leafSpineDelay,
                               "eps-fabric");
            devices.Get(0)->TraceConnectWithoutContext(
                "MacTx",
                MakeBoundCallback(&LinkTxTrace, &linkCounters, leafToSpineCounterIndex));
            std::ostringstream spineToLeafId;
            spineToLeafId << "eps-leaf" << leafIndex << "-spine" << spineIndex
                          << "-spine-to-leaf";
            const uint32_t spineToLeafCounterIndex =
                addLinkCounter(spineToLeafId.str(),
                               "eps-leaf-spine",
                               "b-to-a",
                               "spine",
                               spineIndex,
                               "leaf",
                               leafIndex,
                               leafSpineCapacityGbps,
                               leafSpineDelay,
                               "eps-fabric");
            devices.Get(1)->TraceConnectWithoutContext(
                "MacTx",
                MakeBoundCallback(&LinkTxTrace, &linkCounters, spineToLeafCounterIndex));
            Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
            leafToSpineNextHop[leafIndex][spineIndex] = interfaces.GetAddress(1);
            spineToLeafNextHop[spineIndex][leafIndex] = interfaces.GetAddress(0);
            leafToSpineIfIndex[leafIndex][spineIndex] = interfaces.Get(0).second;
            spineToLeafIfIndex[spineIndex][leafIndex] = interfaces.Get(1).second;
            ipv4.NewNetwork();
        }
    }

    Ipv4Address ocsLeafAAddress("0.0.0.0");
    Ipv4Address ocsLeafBAddress("0.0.0.0");
    std::vector<OcsInstalledLink> installedOcsLinks;
    if (enableStaticOcs)
    {
        std::vector<OcsCandidateEdge> edgesToInstall;
        if (enableMatrixSelect)
        {
            edgesToInstall = selectedOcsEdges;
        }
        else
        {
            edgesToInstall.push_back(makeCandidateEdge(ocsLeafA, ocsLeafB));
        }

        for (const auto& edge : edgesToInstall)
        {
            NetDeviceContainer linkDevices;
            Ipv4InterfaceContainer ocsInterfaces =
                AddOcsLink(leaves.Get(edge.leafA),
                           leaves.Get(edge.leafB),
                           ocsDataRate,
                           ocsDelay,
                           ipv4,
                           linkDevices);
            linkDevices.Get(0)->TraceConnectWithoutContext("MacTx", MakeCallback(&OcsTxTrace));
            linkDevices.Get(1)->TraceConnectWithoutContext("MacTx", MakeCallback(&OcsTxTrace));
            std::ostringstream ocsAToBId;
            ocsAToBId << "ocs-leaf" << edge.leafA << "-leaf" << edge.leafB << "-a-to-b";
            const uint32_t ocsAToBCounterIndex =
                addLinkCounter(ocsAToBId.str(),
                               "ocs",
                               "a-to-b",
                               "leaf",
                               edge.leafA,
                               "leaf",
                               edge.leafB,
                               ocsCapacityGbps,
                               ocsDelay,
                               "selected-ocs");
            linkDevices.Get(0)->TraceConnectWithoutContext(
                "MacTx",
                MakeBoundCallback(&LinkTxTrace, &linkCounters, ocsAToBCounterIndex));
            std::ostringstream ocsBToAId;
            ocsBToAId << "ocs-leaf" << edge.leafA << "-leaf" << edge.leafB << "-b-to-a";
            const uint32_t ocsBToACounterIndex =
                addLinkCounter(ocsBToAId.str(),
                               "ocs",
                               "b-to-a",
                               "leaf",
                               edge.leafB,
                               "leaf",
                               edge.leafA,
                               ocsCapacityGbps,
                               ocsDelay,
                               "selected-ocs");
            linkDevices.Get(1)->TraceConnectWithoutContext(
                "MacTx",
                MakeBoundCallback(&LinkTxTrace, &linkCounters, ocsBToACounterIndex));

            installedOcsLinks.push_back({edge.leafA,
                                         edge.leafB,
                                         ocsInterfaces.GetAddress(0),
                                         ocsInterfaces.GetAddress(1),
                                         ocsInterfaces.Get(0).second,
                                         ocsInterfaces.Get(1).second,
                                         linkDevices,
                                         edge.traffic,
                                         edge.utility});
        }

        if (installedOcsLinks.empty())
        {
            std::cerr << "[HYBRID-DCN][ERROR] enableStaticOcs=true but no OCS link was installed."
                      << std::endl;
            return 1;
        }

        ocsLeafA = installedOcsLinks.front().leafA;
        ocsLeafB = installedOcsLinks.front().leafB;
        ocsLeafAAddress = installedOcsLinks.front().leafAAddress;
        ocsLeafBAddress = installedOcsLinks.front().leafBAddress;
        staticOcsLinks = static_cast<uint32_t>(installedOcsLinks.size());
        reservedOcsLinks = staticOcsLinks;
    }

    auto isOcsPairInstalled = [&installedOcsLinks](uint32_t leafA, uint32_t leafB) {
        for (const auto& link : installedOcsLinks)
        {
            if ((link.leafA == leafA && link.leafB == leafB) ||
                (link.leafA == leafB && link.leafB == leafA))
            {
                return true;
            }
        }
        return false;
    };

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    const Ipv4Address bulkSrcAddress = enableBulk ? serverIpv4[bulkSrcLeaf][bulkSrcServer]
                                                   : Ipv4Address("0.0.0.0");
    const Ipv4Address bulkDstAddress = enableBulk ? serverIpv4[bulkDstLeaf][bulkDstServer]
                                                   : Ipv4Address("0.0.0.0");
    const Ipv4Address secondBulkDstAddress =
        enableSecondBulk ? serverIpv4[secondBulkDstLeaf][secondBulkDstServer]
                         : Ipv4Address("0.0.0.0");
    const Ipv4Address residualBulkDstAddress =
        enableResidualBulk ? serverIpv4[residualBulkDstLeaf][residualBulkDstServer]
                           : Ipv4Address("0.0.0.0");
    bool ocsForced = false;
    uint32_t ocsPairHostRoutes = 0;
    uint32_t ocsPairHostRoutesSkippedForEpsResidual = 0;
    uint32_t routeFixSkippedOcsRoutesForFallback = 0;
    uint32_t routeFixEpsResidualHostRoutes = 0;
    uint32_t routeFixDeterministicEpsRoutes = 0;
    uint32_t routeFixWecmpFrozenRoutes = 0;
    struct RouteFixRecord
    {
        uint32_t flowIndex;
        uint32_t srcLeaf;
        uint32_t dstLeaf;
        uint32_t spine;
        std::string reason;
        std::string mode;
        uint32_t hostRoutes;
        bool installed;
    };
    std::vector<RouteFixRecord> routeFixRecords;

    const uint32_t echoPort = 9000;
    const uint32_t echoSrcLeaf = 0;
    const uint32_t echoSrcServer = 0;
    const uint32_t echoDstLeaf = 3;
    const uint32_t echoDstServer = 0;
    const std::string echoSrcName = "server-l0-s0";
    const std::string echoDstName = "server-l3-s0";
    const Ipv4Address echoDstAddress = enableEcho ? serverIpv4[echoDstLeaf][echoDstServer]
                                                   : Ipv4Address("0.0.0.0");
    const std::string bulkSrcName = "server-l0-s0";
    const std::string bulkDstName = "server-l3-s0";
    const std::string secondBulkSrcName = "server-l1-s0";
    const std::string secondBulkDstName = "server-l2-s0";
    const std::string residualBulkSrcName = "server-l0-s1";
    const std::string residualBulkDstName = "server-l1-s0";

    auto makeServerName = [](uint32_t leafIndex, uint32_t serverOffset) {
        std::ostringstream name;
        name << "server-l" << leafIndex << "-s" << serverOffset;
        return name.str();
    };

    std::vector<MatrixBulkFlowSpec> matrixFlowSpecs;
    std::vector<MatrixBulkFlowStats> matrixFlowStats;
    std::vector<Ptr<PacketSink>> matrixFlowSinks;
    std::vector<OcsAdmissionEvent> ocsAdmissionEvents;
    std::vector<EpsWecmpDecision> epsWecmpDecisions;
    std::vector<int32_t> matrixFlowWecmpDecisionIndex;
    std::vector<EpsWecmpRouteBinding> epsWecmpRouteBindings;
    int32_t measuredWecmpLaterFlowIndex = -1;
    std::vector<std::vector<double>> ocsAdmittedLoad(numLeaves,
                                                     std::vector<double>(numLeaves, 0.0));

    auto computePlannedResidualDemand = [&](uint32_t srcLeaf, uint32_t dstLeaf) {
        const double demand = rawTrafficMatrix[srcLeaf][dstLeaf];
        const bool pairAvailable = isOcsPairInstalled(srcLeaf, dstLeaf);
        const double theta = enableOcsAdmissionControl ? ocsAdmissionThreshold : demand;
        const double plannedResidual = demand - (pairAvailable ? theta : 0.0);
        return std::max(plannedResidual, 0.0);
    };

    auto applyOcsAdmission = [&](uint32_t srcLeaf, uint32_t dstLeaf) {
        const bool pairAvailable = isOcsPairInstalled(srcLeaf, dstLeaf);
        const double loadBefore = ocsAdmittedLoad[srcLeaf][dstLeaf];
        const double plannedResidualDemand = computePlannedResidualDemand(srcLeaf, dstLeaf);
        bool admitted = false;
        bool fallback = false;
        double loadAfter = loadBefore;

        if (!enableOcsAdmissionControl)
        {
            admitted = pairAvailable;
            if (admitted)
            {
                loadAfter = loadBefore + matrixFlowDemand;
            }
        }
        else if (pairAvailable && loadBefore + matrixFlowDemand <= ocsAdmissionThreshold)
        {
            admitted = true;
            loadAfter = loadBefore + matrixFlowDemand;
        }
        else
        {
            fallback = pairAvailable;
        }

        if (admitted)
        {
            ocsAdmittedLoad[srcLeaf][dstLeaf] = loadAfter;
            ocsAdmittedLoad[dstLeaf][srcLeaf] = loadAfter;
        }

        const double realResidualDemand = admitted ? 0.0 : matrixFlowDemand;
        const double wecmpResidualDemand = std::max(plannedResidualDemand, realResidualDemand);

        return OcsAdmissionEvent{srcLeaf,
                                 dstLeaf,
                                 pairAvailable,
                                 admitted,
                                 fallback,
                                 matrixFlowDemand,
                                 loadBefore,
                                 loadAfter,
                                 plannedResidualDemand,
                                 realResidualDemand,
                                 wecmpResidualDemand};
    };

    if (enableMatrixFlows)
    {
        std::vector<std::vector<bool>> matrixFlowPairUsed(numLeaves,
                                                          std::vector<bool>(numLeaves, false));
        for (const auto& link : installedOcsLinks)
        {
            OcsAdmissionEvent admission = applyOcsAdmission(link.leafA, link.leafB);
            ocsAdmissionEvents.push_back(admission);
            matrixFlowPairUsed[link.leafA][link.leafB] = true;
            matrixFlowPairUsed[link.leafB][link.leafA] = true;

            std::ostringstream flowName;
            flowName << "matrix-flow-" << matrixFlowSpecs.size();
            if (admission.ocsAdmitted)
            {
                matrixFlowSpecs.push_back({link.leafA,
                                           link.leafB,
                                           0,
                                           0,
                                           static_cast<uint16_t>(matrixFlowPortBase +
                                                                 matrixFlowSpecs.size()),
                                           true,
                                           admission.ocsPairAvailable,
                                           true,
                                           false,
                                           admission.estimatedDemand,
                                           admission.ocsLoadBefore,
                                           admission.ocsLoadAfter,
                                           admission.plannedResidualDemand,
                                           admission.realResidualDemand,
                                           admission.wecmpResidualDemand,
                                           false,
                                           -1,
                                           flowName.str()});
            }
            else if (admission.epsFallback)
            {
                matrixFlowSpecs.push_back({link.leafA,
                                           link.leafB,
                                           0,
                                           0,
                                           static_cast<uint16_t>(matrixFlowPortBase +
                                                                 matrixFlowSpecs.size()),
                                           false,
                                           admission.ocsPairAvailable,
                                           false,
                                           true,
                                           admission.estimatedDemand,
                                           admission.ocsLoadBefore,
                                           admission.ocsLoadAfter,
                                           admission.plannedResidualDemand,
                                           admission.realResidualDemand,
                                           admission.wecmpResidualDemand,
                                           false,
                                           -1,
                                           flowName.str()});
                MatrixBulkFlowSpec& fallbackSpec = matrixFlowSpecs.back();
                fallbackSpec.fallbackDataPlaneMode = "admission-direct-eps-fallback";
                fallbackSpec.fallbackEventMapped = true;
                fallbackSpec.fallbackMappingType = "admission-direct";
                fallbackSpec.requiresEpsResidualPath = true;
                fallbackSpec.residualPathReason = "admission-fallback";
            }
        }

        const size_t minMatrixFlowCount = 3;
        uint32_t residualMatrixFlowsAdded = 0;
        for (uint32_t srcLeaf = 0;
             srcLeaf < numLeaves &&
             (matrixFlowSpecs.size() < minMatrixFlowCount || residualMatrixFlowsAdded == 0);
             ++srcLeaf)
        {
            for (uint32_t dstLeaf = srcLeaf + 1; dstLeaf < numLeaves; ++dstLeaf)
            {
                if (rawTrafficMatrix[srcLeaf][dstLeaf] <= 0 ||
                    isOcsPairInstalled(srcLeaf, dstLeaf) ||
                    matrixFlowPairUsed[srcLeaf][dstLeaf])
                {
                    continue;
                }

                std::ostringstream flowName;
                flowName << "matrix-flow-" << matrixFlowSpecs.size();
                OcsAdmissionEvent admission = applyOcsAdmission(srcLeaf, dstLeaf);
                ocsAdmissionEvents.push_back(admission);
                matrixFlowSpecs.push_back({srcLeaf,
                                           dstLeaf,
                                           1,
                                           0,
                                           static_cast<uint16_t>(matrixFlowPortBase +
                                                                 matrixFlowSpecs.size()),
                                           admission.ocsAdmitted,
                                           admission.ocsPairAvailable,
                                           admission.ocsAdmitted,
                                           admission.epsFallback,
                                           admission.estimatedDemand,
                                           admission.ocsLoadBefore,
                                           admission.ocsLoadAfter,
                                           admission.plannedResidualDemand,
                                           admission.realResidualDemand,
                                           admission.wecmpResidualDemand,
                                           false,
                                           -1,
                                           flowName.str()});
                MatrixBulkFlowSpec& residualSpec = matrixFlowSpecs.back();
                residualSpec.fallbackDataPlaneMode = "eps-residual-path";
                residualSpec.requiresEpsResidualPath = true;
                residualSpec.residualPathReason = "ordinary-residual";
                matrixFlowPairUsed[srcLeaf][dstLeaf] = true;
                matrixFlowPairUsed[dstLeaf][srcLeaf] = true;
                residualMatrixFlowsAdded++;
                if (matrixFlowSpecs.size() >= minMatrixFlowCount && residualMatrixFlowsAdded > 0)
                {
                    break;
                }
            }
        }

        if (residualMatrixFlowsAdded == 0)
        {
            std::cerr << "[HYBRID-DCN][ERROR] no residual matrix flow candidate found."
                      << std::endl;
            return 1;
        }

        if (enableMeasuredWecmpLaterFlowProof)
        {
            std::ostringstream flowName;
            flowName << "matrix-flow-" << matrixFlowSpecs.size()
                     << "-measured-later-proof";
            matrixFlowSpecs.push_back({measuredWecmpLaterSrcLeaf,
                                       measuredWecmpLaterDstLeaf,
                                       measuredWecmpLaterSrcServer,
                                       measuredWecmpLaterDstServer,
                                       measuredWecmpLaterFlowPort,
                                       false,
                                       isOcsPairInstalled(measuredWecmpLaterSrcLeaf,
                                                          measuredWecmpLaterDstLeaf),
                                       false,
                                       true,
                                       matrixFlowDemand,
                                       0.0,
                                       0.0,
                                       matrixFlowDemand,
                                       matrixFlowDemand,
                                       matrixFlowDemand,
                                       false,
                                       -1,
                                       flowName.str()});
            MatrixBulkFlowSpec& laterSpec = matrixFlowSpecs.back();
            laterSpec.fallbackDataPlaneMode = "measured-wecmp-later-proof";
            laterSpec.fallbackEventMapped = true;
            laterSpec.fallbackMappingType = "measured-later-proof";
            laterSpec.startTime = measuredWecmpLaterFlowStart;
            laterSpec.expectedBytes = measuredWecmpLaterFlowMaxBytes;
            laterSpec.requiresEpsResidualPath = true;
            laterSpec.residualPathReason = "measured-later-proof";
            laterSpec.isMeasuredLaterProofFlow = true;
            laterSpec.measuredLaterDecisionTime = measuredWecmpLaterDecisionTime;
            measuredWecmpLaterFlowIndex = static_cast<int32_t>(matrixFlowSpecs.size() - 1);
        }

        matrixFlowStats.resize(matrixFlowSpecs.size(), {0, false, 0.0, 0.0});
    }

    auto requiresEpsResidualPath = [](const MatrixBulkFlowSpec& spec) {
        return spec.requiresEpsResidualPath && !spec.ocsAdmitted && spec.wecmpResidualDemand > 0;
    };

    matrixFlowWecmpDecisionIndex.assign(matrixFlowSpecs.size(), -1);
    if (enableEpsWecmp && enableMatrixFlows)
    {
        resetEpsPhysicalObservedTraffic();
        for (const auto& spec : matrixFlowSpecs)
        {
            if (requiresEpsResidualPath(spec) && !spec.isMeasuredLaterProofFlow)
            {
                accumulateEpsResidualTraffic(spec.srcLeaf,
                                             spec.dstLeaf,
                                             spec.wecmpResidualDemand);
            }
        }
        applyEpsWecmpDiagnosticLoad();
        updateEpsPhysicalSmoothedUtilization();

        for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
        {
            const auto& spec = matrixFlowSpecs[flowIndex];
            if (!requiresEpsResidualPath(spec) || spec.isMeasuredLaterProofFlow)
            {
                continue;
            }

            EpsWecmpDecision decision =
                runEpsWecmpUpdateForPair(spec.srcLeaf,
                                         spec.dstLeaf,
                                         spec.wecmpResidualDemand,
                                         true,
                                         epsWecmpLoadSource,
                                         false,
                                         true);
            matrixFlowWecmpDecisionIndex[flowIndex] =
                static_cast<int32_t>(epsWecmpDecisions.size());
            epsWecmpDecisions.push_back(decision);
        }
    }

    if (measuredWecmpNoSampleError)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] measured WECMP requested but no measured sample was available and measuredWecmpNoSampleFallback=error."
            << std::endl;
        return 1;
    }

    if (routeMode == "ocs-forced")
    {
        Ipv4StaticRoutingHelper staticRoutingHelper;

        auto shouldSkipOcsHostRouteForEpsResidual = [&](const OcsInstalledLink& link,
                                                        uint32_t serverOffsetA,
                                                        uint32_t serverOffsetB) {
            for (const auto& spec : matrixFlowSpecs)
            {
                if (!requiresEpsResidualPath(spec))
                {
                    continue;
                }
                const bool forwardMatch =
                    spec.srcLeaf == link.leafA && spec.dstLeaf == link.leafB &&
                    (spec.srcServer == serverOffsetA || spec.dstServer == serverOffsetB);
                const bool reverseMatch =
                    spec.srcLeaf == link.leafB && spec.dstLeaf == link.leafA &&
                    (spec.srcServer == serverOffsetB || spec.dstServer == serverOffsetA);
                if (forwardMatch || reverseMatch)
                {
                    return true;
                }
            }
            return false;
        };

        auto applyOcsPairHostRoutes = [&](const OcsInstalledLink& link) {
            Ptr<Ipv4StaticRouting> leafARouting =
                staticRoutingHelper.GetStaticRouting(leaves.Get(link.leafA)->GetObject<Ipv4>());
            Ptr<Ipv4StaticRouting> leafBRouting =
                staticRoutingHelper.GetStaticRouting(leaves.Get(link.leafB)->GetObject<Ipv4>());

            for (uint32_t serverOffsetA = 0; serverOffsetA < serversPerLeaf; ++serverOffsetA)
            {
                Ptr<Ipv4StaticRouting> serverARouting = staticRoutingHelper.GetStaticRouting(
                    servers.Get(serverIndex(link.leafA, serverOffsetA))->GetObject<Ipv4>());

                for (uint32_t serverOffsetB = 0; serverOffsetB < serversPerLeaf; ++serverOffsetB)
                {
                    if (shouldSkipOcsHostRouteForEpsResidual(link,
                                                             serverOffsetA,
                                                             serverOffsetB))
                    {
                        ocsPairHostRoutesSkippedForEpsResidual += 4;
                        routeFixSkippedOcsRoutesForFallback += 4;
                        continue;
                    }

                    const Ipv4Address dstB = serverIpv4[link.leafB][serverOffsetB];
                    const Ipv4Address dstA = serverIpv4[link.leafA][serverOffsetA];

                    serverARouting->AddHostRouteTo(dstB,
                                                   leafServerIpv4[link.leafA][serverOffsetA],
                                                   serverIfIndex[link.leafA][serverOffsetA]);
                    leafARouting->AddHostRouteTo(dstB, link.leafBAddress, link.leafAIfIndex);

                    leafBRouting->AddHostRouteTo(dstA, link.leafAAddress, link.leafBIfIndex);

                    Ptr<Ipv4StaticRouting> serverBRouting = staticRoutingHelper.GetStaticRouting(
                        servers.Get(serverIndex(link.leafB, serverOffsetB))->GetObject<Ipv4>());
                    serverBRouting->AddHostRouteTo(dstA,
                                                   leafServerIpv4[link.leafB][serverOffsetB],
                                                   serverIfIndex[link.leafB][serverOffsetB]);
                    ocsPairHostRoutes += 4;
                }
            }
        };

        for (const auto& link : installedOcsLinks)
        {
            applyOcsPairHostRoutes(link);
        }

        ocsForced = true;
    }

    auto installEpsHostRoutesForMatrixFlow = [&](MatrixBulkFlowSpec& spec,
                                                 uint32_t selectedSpine) {
        Ipv4StaticRoutingHelper staticRoutingHelper;
        const Ipv4Address srcServerAddress = serverIpv4[spec.srcLeaf][spec.srcServer];
        const Ipv4Address dstServerAddress = serverIpv4[spec.dstLeaf][spec.dstServer];
        bool installed = false;
        uint32_t installedHostRoutes = 0;

        if (selectedSpine < numSpines)
        {
            Ptr<Ipv4StaticRouting> srcServerRouting = staticRoutingHelper.GetStaticRouting(
                servers.Get(serverIndex(spec.srcLeaf, spec.srcServer))->GetObject<Ipv4>());
            Ptr<Ipv4StaticRouting> dstServerRouting = staticRoutingHelper.GetStaticRouting(
                servers.Get(serverIndex(spec.dstLeaf, spec.dstServer))->GetObject<Ipv4>());
            Ptr<Ipv4StaticRouting> srcLeafRouting =
                staticRoutingHelper.GetStaticRouting(leaves.Get(spec.srcLeaf)->GetObject<Ipv4>());
            Ptr<Ipv4StaticRouting> dstLeafRouting =
                staticRoutingHelper.GetStaticRouting(leaves.Get(spec.dstLeaf)->GetObject<Ipv4>());
            Ptr<Ipv4StaticRouting> spineRouting =
                staticRoutingHelper.GetStaticRouting(spines.Get(selectedSpine)->GetObject<Ipv4>());

            srcServerRouting->AddHostRouteTo(dstServerAddress,
                                             leafServerIpv4[spec.srcLeaf][spec.srcServer],
                                             serverIfIndex[spec.srcLeaf][spec.srcServer]);
            installedHostRoutes++;
            srcLeafRouting->AddHostRouteTo(dstServerAddress,
                                           leafToSpineNextHop[spec.srcLeaf][selectedSpine],
                                           leafToSpineIfIndex[spec.srcLeaf][selectedSpine]);
            installedHostRoutes++;
            spineRouting->AddHostRouteTo(dstServerAddress,
                                         spineToLeafNextHop[selectedSpine][spec.dstLeaf],
                                         spineToLeafIfIndex[selectedSpine][spec.dstLeaf]);
            installedHostRoutes++;

            dstLeafRouting->AddHostRouteTo(srcServerAddress,
                                           leafToSpineNextHop[spec.dstLeaf][selectedSpine],
                                           leafToSpineIfIndex[spec.dstLeaf][selectedSpine]);
            installedHostRoutes++;
            spineRouting->AddHostRouteTo(srcServerAddress,
                                         spineToLeafNextHop[selectedSpine][spec.srcLeaf],
                                         spineToLeafIfIndex[selectedSpine][spec.srcLeaf]);
            installedHostRoutes++;
            dstServerRouting->AddHostRouteTo(srcServerAddress,
                                             leafServerIpv4[spec.dstLeaf][spec.dstServer],
                                             serverIfIndex[spec.dstLeaf][spec.dstServer]);
            installedHostRoutes++;
            installed = true;
        }

        return std::make_pair(installed, installedHostRoutes);
    };

    if (enableMatrixFlows)
    {
        for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
        {
            auto& spec = matrixFlowSpecs[flowIndex];
            if (!requiresEpsResidualPath(spec) || spec.isMeasuredLaterProofFlow)
            {
                continue;
            }

            uint32_t selectedSpine = 0;
            const int32_t decisionIndex = matrixFlowWecmpDecisionIndex[flowIndex];
            const bool hasWecmpRouteBinding =
                enableEpsWecmpRouting && enableEpsWecmp && decisionIndex >= 0;
            if (hasWecmpRouteBinding)
            {
                const auto& decision =
                    epsWecmpDecisions[static_cast<std::size_t>(decisionIndex)];
                selectedSpine = decision.selectedSpine;
            }
            const Ipv4Address srcServerAddress = serverIpv4[spec.srcLeaf][spec.srcServer];
            const Ipv4Address dstServerAddress = serverIpv4[spec.dstLeaf][spec.dstServer];
            const auto installResult = installEpsHostRoutesForMatrixFlow(spec, selectedSpine);
            const bool installed = installResult.first;
            const uint32_t installedHostRoutes = installResult.second;

            routeFixEpsResidualHostRoutes += installedHostRoutes;
            if (hasWecmpRouteBinding && installed)
            {
                spec.epsPathFrozen = true;
                spec.frozenSpine = static_cast<int32_t>(selectedSpine);
                routeFixWecmpFrozenRoutes++;
            }
            else if (installed)
            {
                routeFixDeterministicEpsRoutes++;
            }

            routeFixRecords.push_back({flowIndex,
                                       spec.srcLeaf,
                                       spec.dstLeaf,
                                       selectedSpine,
                                       spec.residualPathReason,
                                       hasWecmpRouteBinding ? "wecmp-frozen" : "deterministic-spine0",
                                       installedHostRoutes,
                                       installed});

            if (hasWecmpRouteBinding)
            {
                epsWecmpRouteBindings.push_back({flowIndex,
                                                 spec.srcLeaf,
                                                 spec.dstLeaf,
                                                 selectedSpine,
                                                 srcServerAddress,
                                                 dstServerAddress,
                                                 installed,
                                                 installed,
                                                 spec.wecmpResidualDemand});
            }
        }
    }

    if (enableEcho)
    {
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

        UdpEchoServerHelper echoServer(echoPort);
        ApplicationContainer serverApps =
            echoServer.Install(servers.Get(serverIndex(echoDstLeaf, echoDstServer)));
        serverApps.Get(0)->TraceConnectWithoutContext("RxWithAddresses",
                                                      MakeCallback(&EchoServerRxTrace));
        serverApps.Start(Seconds(0.1));
        serverApps.Stop(Seconds(simTime));

        UdpEchoClientHelper echoClient(echoDstAddress, echoPort);
        echoClient.SetAttribute("MaxPackets", UintegerValue(echoCount));
        echoClient.SetAttribute("Interval", TimeValue(Seconds(echoInterval)));
        echoClient.SetAttribute("PacketSize", UintegerValue(echoPacketSize));
        ApplicationContainer clientApps =
            echoClient.Install(servers.Get(serverIndex(echoSrcLeaf, echoSrcServer)));
        clientApps.Get(0)->TraceConnectWithoutContext("TxWithAddresses",
                                                      MakeCallback(&EchoClientTxTrace));
        clientApps.Get(0)->TraceConnectWithoutContext("RxWithAddresses",
                                                      MakeCallback(&EchoClientRxTrace));
        clientApps.Start(Seconds(0.2));
        clientApps.Stop(Seconds(simTime));
    }

    if (enableBulk)
    {
        g_bulkRxBytes = 0;
        g_bulkSeenFirstRx = false;
        g_bulkFirstRxTime = 0.0;
        g_bulkLastRxTime = 0.0;

        Address sinkAddress(InetSocketAddress(Ipv4Address::GetAny(), bulkPort));
        PacketSinkHelper packetSink("ns3::TcpSocketFactory", sinkAddress);
        ApplicationContainer sinkApps =
            packetSink.Install(servers.Get(serverIndex(bulkDstLeaf, bulkDstServer)));
        sinkApps.Get(0)->TraceConnectWithoutContext("Rx", MakeCallback(&BulkSinkRxTrace));
        sinkApps.Start(Seconds(0.1));
        sinkApps.Stop(Seconds(simTime));

        Address bulkRemoteAddress(InetSocketAddress(bulkDstAddress, bulkPort));
        BulkSendHelper bulkSender("ns3::TcpSocketFactory", bulkRemoteAddress);
        bulkSender.SetAttribute("MaxBytes", UintegerValue(bulkMaxBytes));
        ApplicationContainer bulkApps =
            bulkSender.Install(servers.Get(serverIndex(bulkSrcLeaf, bulkSrcServer)));
        bulkApps.Start(Seconds(bulkStart));
        bulkApps.Stop(Seconds(simTime));
    }

    if (enableSecondBulk)
    {
        g_secondBulkRxBytes = 0;
        g_secondBulkSeenFirstRx = false;
        g_secondBulkFirstRxTime = 0.0;
        g_secondBulkLastRxTime = 0.0;

        Address secondSinkAddress(InetSocketAddress(Ipv4Address::GetAny(), secondBulkPort));
        PacketSinkHelper secondPacketSink("ns3::TcpSocketFactory", secondSinkAddress);
        ApplicationContainer secondSinkApps = secondPacketSink.Install(
            servers.Get(serverIndex(secondBulkDstLeaf, secondBulkDstServer)));
        secondSinkApps.Get(0)->TraceConnectWithoutContext("Rx",
                                                          MakeCallback(&SecondBulkSinkRxTrace));
        secondSinkApps.Start(Seconds(0.1));
        secondSinkApps.Stop(Seconds(simTime));

        Address secondRemoteAddress(InetSocketAddress(secondBulkDstAddress, secondBulkPort));
        BulkSendHelper secondBulkSender("ns3::TcpSocketFactory", secondRemoteAddress);
        secondBulkSender.SetAttribute("MaxBytes", UintegerValue(secondBulkMaxBytes));
        ApplicationContainer secondBulkApps = secondBulkSender.Install(
            servers.Get(serverIndex(secondBulkSrcLeaf, secondBulkSrcServer)));
        secondBulkApps.Start(Seconds(secondBulkStart));
        secondBulkApps.Stop(Seconds(simTime));
    }

    if (enableResidualBulk)
    {
        g_residualBulkRxBytes = 0;
        g_residualBulkSeenFirstRx = false;
        g_residualBulkFirstRxTime = 0.0;
        g_residualBulkLastRxTime = 0.0;

        Address residualSinkAddress(InetSocketAddress(Ipv4Address::GetAny(), residualBulkPort));
        PacketSinkHelper residualPacketSink("ns3::TcpSocketFactory", residualSinkAddress);
        ApplicationContainer residualSinkApps = residualPacketSink.Install(
            servers.Get(serverIndex(residualBulkDstLeaf, residualBulkDstServer)));
        residualSinkApps.Get(0)->TraceConnectWithoutContext(
            "Rx",
            MakeCallback(&ResidualBulkSinkRxTrace));
        residualSinkApps.Start(Seconds(0.1));
        residualSinkApps.Stop(Seconds(simTime));

        Address residualRemoteAddress(InetSocketAddress(residualBulkDstAddress, residualBulkPort));
        BulkSendHelper residualBulkSender("ns3::TcpSocketFactory", residualRemoteAddress);
        residualBulkSender.SetAttribute("MaxBytes", UintegerValue(residualBulkMaxBytes));
        ApplicationContainer residualBulkApps = residualBulkSender.Install(
            servers.Get(serverIndex(residualBulkSrcLeaf, residualBulkSrcServer)));
        residualBulkApps.Start(Seconds(residualBulkStart));
        residualBulkApps.Stop(Seconds(simTime));
    }

    if (enableMatrixFlows)
    {
        for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
        {
            auto& spec = matrixFlowSpecs[flowIndex];
            if (!spec.isMeasuredLaterProofFlow)
            {
                spec.startTime = matrixFlowStart + (0.02 * static_cast<double>(flowIndex));
                spec.expectedBytes = matrixFlowMaxBytes;
            }
            Address sinkAddress(InetSocketAddress(Ipv4Address::GetAny(), spec.port));
            PacketSinkHelper matrixPacketSink("ns3::TcpSocketFactory", sinkAddress);
            ApplicationContainer sinkApps = matrixPacketSink.Install(
                servers.Get(serverIndex(spec.dstLeaf, spec.dstServer)));
            Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
            matrixFlowSinks.push_back(sink);
            sinkApps.Get(0)->TraceConnectWithoutContext(
                "Rx",
                MakeBoundCallback(&MatrixBulkSinkRxTrace, &matrixFlowStats, flowIndex));
            sinkApps.Start(Seconds(0.1));
            sinkApps.Stop(Seconds(simTime));

            Address remoteAddress(
                InetSocketAddress(serverIpv4[spec.dstLeaf][spec.dstServer], spec.port));
            BulkSendHelper matrixBulkSender("ns3::TcpSocketFactory", remoteAddress);
            matrixBulkSender.SetAttribute("MaxBytes", UintegerValue(spec.expectedBytes));
            ApplicationContainer bulkApps =
                matrixBulkSender.Install(servers.Get(serverIndex(spec.srcLeaf, spec.srcServer)));
            bulkApps.Start(Seconds(spec.startTime));
            bulkApps.Stop(Seconds(simTime));
        }
    }

    std::cout << "[HYBRID-DCN] experimentName   = " << experimentName << std::endl;
    std::cout << "[HYBRID-DCN] simTime          = " << simTime << " s" << std::endl;
    std::cout << "[HYBRID-DCN] numSpines        = " << numSpines << std::endl;
    std::cout << "[HYBRID-DCN] numLeaves        = " << numLeaves << std::endl;
    std::cout << "[HYBRID-DCN] serversPerLeaf   = " << serversPerLeaf << std::endl;
    std::cout << "[HYBRID-DCN] totalServers     = " << totalServers << std::endl;
    std::cout << "[HYBRID-DCN] totalNodes       = " << totalNodes << std::endl;
    std::cout << "[HYBRID-DCN] serverLeafLinks  = " << serverLeafLinks << std::endl;
    std::cout << "[HYBRID-DCN] leafSpineLinks   = " << leafSpineLinks << std::endl;
    std::cout << "[HYBRID-DCN] epsLinksCount    = " << epsLinksCount << std::endl;
    std::cout << "[HYBRID-DCN] reservedOcsLinks = " << reservedOcsLinks << std::endl;
    std::cout << "[HYBRID-DCN] enableStaticOcs = " << (enableStaticOcs ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN] ocsLeafA        = " << ocsLeafA << std::endl;
    std::cout << "[HYBRID-DCN] ocsLeafB        = " << ocsLeafB << std::endl;
    std::cout << "[HYBRID-DCN] ocsDataRate     = " << ocsDataRate << std::endl;
    std::cout << "[HYBRID-DCN] ocsDelay        = " << ocsDelay << std::endl;
    std::cout << "[HYBRID-DCN] staticOcsLinks  = " << staticOcsLinks << std::endl;
    std::cout << "[HYBRID-DCN] ocsLeafAAddress = ";
    if (enableStaticOcs)
    {
        std::cout << ocsLeafAAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN] ocsLeafBAddress = ";
    if (enableStaticOcs)
    {
        std::cout << ocsLeafBAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN][OCS-LINK] installedOcsLinks = "
              << installedOcsLinks.size() << std::endl;
    for (uint32_t linkIndex = 0; linkIndex < installedOcsLinks.size(); ++linkIndex)
    {
        const auto& link = installedOcsLinks[linkIndex];
        std::cout << "[HYBRID-DCN][OCS-LINK] link[" << linkIndex << "] = " << link.leafA
                  << "-" << link.leafB << " addrA=" << link.leafAAddress
                  << " addrB=" << link.leafBAddress << " utility=" << link.utility
                  << std::endl;
    }
    std::cout << "[HYBRID-DCN] enableEcho      = " << (enableEcho ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN] echoSrc         = " << (enableEcho ? echoSrcName : "N/A")
              << std::endl;
    std::cout << "[HYBRID-DCN] echoDst         = " << (enableEcho ? echoDstName : "N/A")
              << std::endl;
    std::cout << "[HYBRID-DCN] echoDstAddress  = ";
    if (enableEcho)
    {
        std::cout << echoDstAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN] echoPacketSize  = " << (enableEcho ? echoPacketSize : 0)
              << std::endl;
    std::cout << "[HYBRID-DCN] echoInterval    = " << (enableEcho ? echoInterval : 0.0)
              << " s" << std::endl;
    std::cout << "[HYBRID-DCN] echoCount       = " << (enableEcho ? echoCount : 0)
              << std::endl;
    std::cout << "[HYBRID-DCN] enableBulk     = " << (enableBulk ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN] bulkSrc        = " << (enableBulk ? bulkSrcName : "N/A")
              << std::endl;
    std::cout << "[HYBRID-DCN] bulkDst        = " << (enableBulk ? bulkDstName : "N/A")
              << std::endl;
    std::cout << "[HYBRID-DCN] bulkDstAddress = ";
    if (enableBulk)
    {
        std::cout << bulkDstAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN] bulkMaxBytes   = " << (enableBulk ? bulkMaxBytes : 0)
              << std::endl;
    std::cout << "[HYBRID-DCN] bulkStart      = " << (enableBulk ? bulkStart : 0.0) << " s"
              << std::endl;
    std::cout << "[HYBRID-DCN] bulkPort       = " << (enableBulk ? bulkPort : 0)
              << std::endl;
    std::cout << "[HYBRID-DCN] enableSecondBulk     = "
              << (enableSecondBulk ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN] secondBulkSrc        = "
              << (enableSecondBulk ? secondBulkSrcName : "N/A") << std::endl;
    std::cout << "[HYBRID-DCN] secondBulkDst        = "
              << (enableSecondBulk ? secondBulkDstName : "N/A") << std::endl;
    std::cout << "[HYBRID-DCN] secondBulkDstAddress = ";
    if (enableSecondBulk)
    {
        std::cout << secondBulkDstAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN] secondBulkMaxBytes   = "
              << (enableSecondBulk ? secondBulkMaxBytes : 0) << std::endl;
    std::cout << "[HYBRID-DCN] secondBulkStart      = "
              << (enableSecondBulk ? secondBulkStart : 0.0) << " s" << std::endl;
    std::cout << "[HYBRID-DCN] secondBulkPort       = "
              << (enableSecondBulk ? secondBulkPort : 0) << std::endl;
    std::cout << "[HYBRID-DCN] enableResidualBulk     = "
              << (enableResidualBulk ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN] residualBulkSrc        = "
              << (enableResidualBulk ? residualBulkSrcName : "N/A") << std::endl;
    std::cout << "[HYBRID-DCN] residualBulkDst        = "
              << (enableResidualBulk ? residualBulkDstName : "N/A") << std::endl;
    std::cout << "[HYBRID-DCN] residualBulkDstAddress = ";
    if (enableResidualBulk)
    {
        std::cout << residualBulkDstAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN] residualBulkMaxBytes   = "
              << (enableResidualBulk ? residualBulkMaxBytes : 0) << std::endl;
    std::cout << "[HYBRID-DCN] residualBulkStart      = "
              << (enableResidualBulk ? residualBulkStart : 0.0) << " s" << std::endl;
    std::cout << "[HYBRID-DCN] residualBulkPort       = "
              << (enableResidualBulk ? residualBulkPort : 0) << std::endl;
    std::cout << "[HYBRID-DCN] enableMatrixFlows      = "
              << (enableMatrixFlows ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN] matrixFlowMaxBytes     = "
              << (enableMatrixFlows ? matrixFlowMaxBytes : 0) << std::endl;
    std::cout << "[HYBRID-DCN] matrixFlowStart        = "
              << (enableMatrixFlows ? matrixFlowStart : 0.0) << " s" << std::endl;
    std::cout << "[HYBRID-DCN] matrixFlowPortBase     = "
              << (enableMatrixFlows ? matrixFlowPortBase : 0) << std::endl;
    std::cout << "[HYBRID-DCN] matrixFlowRxBytesTolerance = "
              << matrixFlowRxBytesTolerance << std::endl;

    uint32_t ocsCoveredFlows = 0;
    uint32_t admissionControlledAdmittedFlows = 0;
    uint32_t directOcsFlows = 0;
    uint32_t admissionAdmittedFlows = 0;
    uint32_t admissionFallbackFlows = 0;
    uint32_t admissionResidualFlows = 0;
    for (const auto& event : ocsAdmissionEvents)
    {
        if (event.ocsAdmitted)
        {
            if (enableOcsAdmissionControl)
            {
                admissionControlledAdmittedFlows++;
            }
            else
            {
                directOcsFlows++;
            }
        }
        if (event.epsFallback)
        {
            admissionFallbackFlows++;
        }
    }
    for (const auto& spec : matrixFlowSpecs)
    {
        if (!spec.ocsCovered)
        {
            admissionResidualFlows++;
        }
        else
        {
            ocsCoveredFlows++;
        }
    }
    admissionAdmittedFlows =
        enableOcsAdmissionControl ? admissionControlledAdmittedFlows : 0;
    const uint32_t ocsPairAvailableFlows =
        admissionControlledAdmittedFlows + directOcsFlows + admissionFallbackFlows;
    const std::string fallbackDataPlaneMode = "eps-residual-path";
    uint32_t fallbackMatrixFlowCount = 0;
    uint32_t directFallbackMatrixFlowCount = 0;
    uint32_t syntheticFallbackMatrixFlowCount = 0;
    uint32_t epsResidualPathFlowCount = 0;
    uint32_t plannedResidualPositiveButOcsAdmitted = 0;
    for (auto& spec : matrixFlowSpecs)
    {
        if (requiresEpsResidualPath(spec))
        {
            epsResidualPathFlowCount++;
        }
        if (spec.ocsAdmitted && spec.plannedResidualDemand > 0)
        {
            plannedResidualPositiveButOcsAdmitted++;
        }
        if (spec.epsFallback)
        {
            fallbackMatrixFlowCount++;
            directFallbackMatrixFlowCount++;
        }
        else if (admissionFallbackFlows > 0 && requiresEpsResidualPath(spec))
        {
            fallbackMatrixFlowCount++;
            syntheticFallbackMatrixFlowCount++;
        }
    }
    std::string fallbackEventToMatrixFlowMapping = "none";
    if (admissionFallbackFlows > 0)
    {
        if (directFallbackMatrixFlowCount >= admissionFallbackFlows)
        {
            fallbackEventToMatrixFlowMapping = "admission-direct";
        }
        else if (fallbackMatrixFlowCount > 0)
        {
            fallbackEventToMatrixFlowMapping = "synthetic-residual";
        }
    }
    for (auto& spec : matrixFlowSpecs)
    {
        if (spec.epsFallback)
        {
            spec.fallbackEventMapped = true;
            if (spec.fallbackDataPlaneMode.empty() ||
                spec.fallbackDataPlaneMode == "synthetic-residual-flow-validation")
            {
                spec.fallbackDataPlaneMode = "admission-direct-eps-fallback";
            }
            if (spec.fallbackMappingType == "none")
            {
                spec.fallbackMappingType = "admission-direct";
            }
        }
        else if (fallbackEventToMatrixFlowMapping == "synthetic-residual" &&
                 requiresEpsResidualPath(spec))
        {
            spec.fallbackEventMapped = true;
            spec.fallbackDataPlaneMode = "synthetic-residual-flow-validation";
            spec.fallbackMappingType = "synthetic-residual";
        }
        else
        {
            if (!spec.fallbackEventMapped)
            {
                spec.fallbackMappingType = "none";
            }
            if (spec.fallbackDataPlaneMode.empty())
            {
                spec.fallbackDataPlaneMode = fallbackDataPlaneMode;
            }
        }
    }

    std::cout << "[HYBRID-DCN][ADMISSION] enabled = "
              << (enableOcsAdmissionControl ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] mode = "
              << (enableOcsAdmissionControl ? "controlled" : "disabled-direct-ocs")
              << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] threshold = " << ocsAdmissionThreshold
              << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] matrixFlowDemand = " << matrixFlowDemand
              << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] fallbackDataPlaneMode = "
              << fallbackDataPlaneMode << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] fallbackEventCount = "
              << admissionFallbackFlows << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] fallbackMatrixFlowCount = "
              << fallbackMatrixFlowCount << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] directFallbackMatrixFlowCount = "
              << directFallbackMatrixFlowCount << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] syntheticFallbackMatrixFlowCount = "
              << syntheticFallbackMatrixFlowCount << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] fallbackEventToMatrixFlowMapping = "
              << fallbackEventToMatrixFlowMapping << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] epsResidualPathFlowCount = "
              << epsResidualPathFlowCount << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] plannedResidualPositiveButOcsAdmitted = "
              << plannedResidualPositiveButOcsAdmitted << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] ocsCoveredFlows = " << ocsCoveredFlows
              << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] ocsPairAvailableFlows = "
              << ocsPairAvailableFlows << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] admissionControlledAdmittedFlows = "
              << admissionControlledAdmittedFlows << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] directOcsFlows = " << directOcsFlows
              << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] admittedFlows = " << admissionAdmittedFlows
              << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] admittedFlowsSemantic = "
              << (enableOcsAdmissionControl ? "admission-controlled"
                                            : "disabled-zero-see-directOcsFlows")
              << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] fallbackFlows = " << admissionFallbackFlows
              << std::endl;
    std::cout << "[HYBRID-DCN][ADMISSION] residualFlows = " << admissionResidualFlows
              << std::endl;
    for (uint32_t eventIndex = 0; eventIndex < ocsAdmissionEvents.size(); ++eventIndex)
    {
        const auto& event = ocsAdmissionEvents[eventIndex];
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] pair = " << event.srcLeaf << "-" << event.dstLeaf << std::endl;
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] ocsPairAvailable = "
                  << (event.ocsPairAvailable ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] ocsAdmitted = " << (event.ocsAdmitted ? "true" : "false")
                  << std::endl;
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] epsFallback = " << (event.epsFallback ? "true" : "false")
                  << std::endl;
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] estimatedDemand = " << event.estimatedDemand << std::endl;
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] ocsLoadBefore = " << event.ocsLoadBefore << std::endl;
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] ocsLoadAfter = " << event.ocsLoadAfter << std::endl;
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] plannedResidualDemand = " << event.plannedResidualDemand
                  << std::endl;
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] realResidualDemand = " << event.realResidualDemand << std::endl;
        std::cout << "[HYBRID-DCN][ADMISSION] matrixFlow[" << eventIndex
                  << "] wecmpResidualDemand = " << event.wecmpResidualDemand << std::endl;
    }

    std::cout << "[HYBRID-DCN][WECMP] enabled = "
              << (enableEpsWecmp ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] source = "
              << (epsWecmpLoadSource == "control-plane"
                      ? "control-plane-estimated-residual-load"
                      : "measured-snapshot")
              << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] ns3MeasuredUtilization = false"
              << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] measuredSnapshotRequested = "
              << (epsWecmpLoadSource == "measured-snapshot" ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] measuredNoSampleFallback = "
              << measuredWecmpNoSampleFallback << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] measuredWarmupTime = "
              << measuredWecmpWarmupTime << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] observedTrafficSemantic = "
                 "residual-demand-weighted-by-current-wecmp-probability"
              << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] rho = " << epsWecmpRho << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] gamma = " << epsWecmpGamma << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] epsilon = " << epsWecmpEpsilon << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] kappa = " << epsWecmpKappa << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] maxDelta = " << epsWecmpMaxDelta << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] epochs = " << epsWecmpEpochs << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] capacity = " << epsWecmpCapacity << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] pathMetric = " << epsWecmpPathMetric << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] diagnosticLoadMode = "
              << epsWecmpDiagnosticLoadMode << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] diagnosticLoad = "
              << epsWecmpDiagnosticLoad << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] diagnosticHotSpine = "
              << epsWecmpDiagnosticHotSpine << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] diagnosticLoadApplied = "
              << (epsWecmpDiagnosticLoadApplied ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] diagnosticLoadApplyCount = "
              << epsWecmpDiagnosticLoadApplyCount << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] diagnosticTotalInjected = "
              << epsWecmpDiagnosticTotalInjected << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] diagnosticLoadScope = all-leaves-to-hot-spine"
              << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] diagnosticLoadPurpose = synthetic-diagnostics-only"
              << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] diagnosticLoadApplicationSemantics = "
                 "applied-before-each-wecmp-telemetry-update"
              << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] residualDecisions = "
              << epsWecmpDecisions.size() << std::endl;
    std::cout << "[HYBRID-DCN][WECMP] pairStates = " << epsWecmpPairStates.size()
              << std::endl;
    const uint32_t physicalLinkStateCount = numLeaves * numSpines;
    std::cout << "[HYBRID-DCN][WECMP] physicalLinkStates = "
              << physicalLinkStateCount << std::endl;
    const uint32_t physicalLinkLogLimit = 8;
    if (physicalLinkStateCount > physicalLinkLogLimit)
    {
        std::cout << "[HYBRID-DCN][WECMP] physicalLinkLogLimit = "
                  << physicalLinkLogLimit << std::endl;
    }
    uint32_t physicalLinkLogIndex = 0;
    for (uint32_t leafIndex = 0; leafIndex < numLeaves &&
                                      physicalLinkLogIndex < physicalLinkLogLimit;
         ++leafIndex)
    {
        for (uint32_t spineIndex = 0; spineIndex < numSpines &&
                                          physicalLinkLogIndex < physicalLinkLogLimit;
             ++spineIndex)
        {
            const auto& linkState = epsPhysicalLinkStates[leafIndex][spineIndex];
            std::cout << "[HYBRID-DCN][WECMP] physicalLink[" << physicalLinkLogIndex
                      << "] leaf = " << linkState.leafIndex << std::endl;
            std::cout << "[HYBRID-DCN][WECMP] physicalLink[" << physicalLinkLogIndex
                      << "] spine = " << linkState.spineIndex << std::endl;
            std::cout << "[HYBRID-DCN][WECMP] physicalLink[" << physicalLinkLogIndex
                      << "] observedTraffic = " << linkState.observedTraffic << std::endl;
            std::cout << "[HYBRID-DCN][WECMP] physicalLink[" << physicalLinkLogIndex
                      << "] utilization = " << linkState.utilization << std::endl;
            std::cout << "[HYBRID-DCN][WECMP] physicalLink[" << physicalLinkLogIndex
                      << "] smoothedUtilization = " << linkState.smoothedUtilization
                      << std::endl;
            physicalLinkLogIndex++;
        }
    }
    if (enableEpsWecmp)
    {
        for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
        {
            const auto& spec = matrixFlowSpecs[flowIndex];
            if (!requiresEpsResidualPath(spec))
            {
                std::cout << "[HYBRID-DCN][WECMP] matrixFlow[" << flowIndex
                          << "] skipped = ";
                if (spec.ocsAdmitted && spec.plannedResidualDemand > 0)
                {
                    std::cout << "ocs-admitted-planned-residual-pending-flow-splitting";
                }
                else if (spec.ocsAdmitted || spec.ocsCovered)
                {
                    std::cout << "ocs-admitted-no-eps-residual-path";
                }
                else
                {
                    std::cout << "no-eps-residual-path";
                }
                std::cout << std::endl;
                continue;
            }

            const int32_t decisionIndex = matrixFlowWecmpDecisionIndex[flowIndex];
            if (decisionIndex < 0)
            {
                continue;
            }

            const auto& decision =
                epsWecmpDecisions[static_cast<std::size_t>(decisionIndex)];
            std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                      << "] pair = " << decision.srcLeaf << "-" << decision.dstLeaf
                      << std::endl;
            std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                      << "] selectedSpine = " << decision.selectedSpine << std::endl;
            std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                      << "] residualDemand = " << decision.residualDemand << std::endl;
            std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                      << "] candidateSpines = " << decision.linkStates.size() << std::endl;
            for (const auto& state : decision.linkStates)
            {
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] observedTraffic = " << state.observedTraffic << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] utilization = " << state.utilization << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] smoothedUtilization = " << state.smoothedUtilization
                          << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] pathLoadMetric = " << state.pathLoadMetric << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] candidatePathLoad = " << state.candidatePathLoad
                          << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] attractiveness = " << state.attractiveness << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] normalizedAttractiveness = "
                          << state.normalizedAttractiveness << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] targetProbability = " << state.targetProbability << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] previousProbability = " << state.previousProbability
                          << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] updatedProbability = " << state.updatedProbability
                          << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] probabilityDelta = " << state.probabilityDelta
                          << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] boundedProbabilityDelta = "
                          << state.boundedProbabilityDelta << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] probabilityDeltaSemantic = decision-start-to-final"
                          << std::endl;
                std::cout << "[HYBRID-DCN][WECMP] decision[" << decisionIndex
                          << "] spine[" << state.spineIndex
                          << "] boundedProbabilityDeltaSemantic = per-update-step"
                          << std::endl;
            }
        }
    }
    for (uint32_t pairStateIndex = 0; pairStateIndex < epsWecmpPairStates.size();
         ++pairStateIndex)
    {
        const auto& pairState = epsWecmpPairStates[pairStateIndex];
        std::cout << "[HYBRID-DCN][WECMP] pairState[" << pairStateIndex
                  << "] pair = " << pairState.srcLeaf << "-" << pairState.dstLeaf
                  << std::endl;
        std::cout << "[HYBRID-DCN][WECMP] pairState[" << pairStateIndex
                  << "] initialized = " << (pairState.initialized ? "true" : "false")
                  << std::endl;
        for (uint32_t spineIndex = 0; spineIndex < pairState.probabilities.size();
             ++spineIndex)
        {
            std::cout << "[HYBRID-DCN][WECMP] pairState[" << pairStateIndex
                      << "] spine[" << spineIndex
                      << "] probability = " << pairState.probabilities[spineIndex]
                      << std::endl;
            std::cout << "[HYBRID-DCN][WECMP] pairState[" << pairStateIndex
                      << "] spine[" << spineIndex
                      << "] smoothedUtilization = "
                      << pairState.smoothedUtilizations[spineIndex] << std::endl;
        }
    }

    std::cout << "[HYBRID-DCN][WECMP-ROUTE] enabled = "
              << (enableEpsWecmpRouting ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][WECMP-ROUTE] bindings = "
              << epsWecmpRouteBindings.size() << std::endl;
    for (uint32_t bindingIndex = 0; bindingIndex < epsWecmpRouteBindings.size(); ++bindingIndex)
    {
        const auto& binding = epsWecmpRouteBindings[bindingIndex];
        std::cout << "[HYBRID-DCN][WECMP-ROUTE] binding[" << bindingIndex
                  << "] flowIndex = " << binding.flowIndex << std::endl;
        std::cout << "[HYBRID-DCN][WECMP-ROUTE] binding[" << bindingIndex
                  << "] pair = " << binding.srcLeaf << "-" << binding.dstLeaf << std::endl;
        std::cout << "[HYBRID-DCN][WECMP-ROUTE] binding[" << bindingIndex
                  << "] selectedSpine = " << binding.selectedSpine << std::endl;
        std::cout << "[HYBRID-DCN][WECMP-ROUTE] binding[" << bindingIndex
                  << "] installed = " << (binding.installed ? "true" : "false")
                  << std::endl;
        std::cout << "[HYBRID-DCN][WECMP-ROUTE] binding[" << bindingIndex
                  << "] pathFrozen = " << (binding.pathFrozen ? "true" : "false")
                  << std::endl;
        std::cout << "[HYBRID-DCN][WECMP-ROUTE] binding[" << bindingIndex
                  << "] residualDemand = " << binding.residualDemand << std::endl;
    }

    std::cout << "[HYBRID-DCN][ROUTE-FIX] skippedOcsRoutesForFallback = "
              << routeFixSkippedOcsRoutesForFallback << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE-FIX] epsFallbackHostRoutes = "
              << routeFixEpsResidualHostRoutes << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE-FIX] deterministicEpsRoutes = "
              << routeFixDeterministicEpsRoutes << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE-FIX] wecmpFrozenRoutes = "
              << routeFixWecmpFrozenRoutes << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE-FIX] fallbackRoutes = "
              << routeFixRecords.size() << std::endl;
    for (uint32_t recordIndex = 0; recordIndex < routeFixRecords.size(); ++recordIndex)
    {
        const auto& record = routeFixRecords[recordIndex];
        std::cout << "[HYBRID-DCN][ROUTE-FIX] fallbackRoute[" << recordIndex
                  << "] flowIndex = " << record.flowIndex << std::endl;
        std::cout << "[HYBRID-DCN][ROUTE-FIX] fallbackRoute[" << recordIndex
                  << "] pair = " << record.srcLeaf << "-" << record.dstLeaf << std::endl;
        std::cout << "[HYBRID-DCN][ROUTE-FIX] fallbackRoute[" << recordIndex
                  << "] spine = " << record.spine << std::endl;
        std::cout << "[HYBRID-DCN][ROUTE-FIX] fallbackRoute[" << recordIndex
                  << "] mode = " << record.mode << std::endl;
        std::cout << "[HYBRID-DCN][ROUTE-FIX] fallbackRoute[" << recordIndex
                  << "] reason = " << record.reason << std::endl;
        std::cout << "[HYBRID-DCN][ROUTE-FIX] fallbackRoute[" << recordIndex
                  << "] hostRoutes = " << record.hostRoutes << std::endl;
        std::cout << "[HYBRID-DCN][ROUTE-FIX] fallbackRoute[" << recordIndex
                  << "] installed = " << (record.installed ? "true" : "false") << std::endl;
    }

    uint32_t epsFrozenFlows = 0;
    uint32_t epsUnfrozenResidualFlows = 0;
    for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
    {
        const auto& spec = matrixFlowSpecs[flowIndex];
        if (!spec.ocsCovered)
        {
            if (spec.epsPathFrozen)
            {
                epsFrozenFlows++;
            }
            else
            {
                epsUnfrozenResidualFlows++;
            }
        }

        std::cout << "[HYBRID-DCN][FLOW-PATH] matrixFlow[" << flowIndex
                  << "] name = " << spec.name << std::endl;
        std::cout << "[HYBRID-DCN][FLOW-PATH] matrixFlow[" << flowIndex
                  << "] pair = " << spec.srcLeaf << "-" << spec.dstLeaf << std::endl;
        std::cout << "[HYBRID-DCN][FLOW-PATH] matrixFlow[" << flowIndex
                  << "] ocsCovered = " << (spec.ocsCovered ? "true" : "false")
                  << std::endl;
        std::cout << "[HYBRID-DCN][FLOW-PATH] matrixFlow[" << flowIndex
                  << "] epsPathFrozen = "
                  << (spec.epsPathFrozen ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][FLOW-PATH] matrixFlow[" << flowIndex
                  << "] frozenSpine = " << spec.frozenSpine << std::endl;
        std::cout << "[HYBRID-DCN][FLOW-PATH] matrixFlow[" << flowIndex
                  << "] requiresEpsResidualPath = "
                  << (requiresEpsResidualPath(spec) ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][FLOW-PATH] matrixFlow[" << flowIndex
                  << "] residualPathReason = " << spec.residualPathReason << std::endl;
    }
    std::cout << "[HYBRID-DCN][FLOW-PATH] epsFrozenFlows = " << epsFrozenFlows
              << std::endl;
    std::cout << "[HYBRID-DCN][FLOW-PATH] epsUnfrozenResidualFlows = "
              << epsUnfrozenResidualFlows << std::endl;

    std::cout << "[HYBRID-DCN][MATRIX] enableMatrixSelect = "
              << (enableMatrixSelect ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] trafficMatrixMode = " << trafficMatrixMode
              << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] trafficMatrixSource = "
              << trafficMatrixSource << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] enableEwmaSmoothing = "
              << (enableEwmaSmoothing ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] ewmaBeta = " << ewmaBeta << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] trafficGraphThreshold = "
              << trafficGraphThreshold << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] matrixUsedForControl = "
              << matrixUsedForControl << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] matrixSemantic = directed-W-to-undirected-A"
              << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] rawMatrixSemantic = synthetic-directed-W-derived-A"
              << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] controlMatrixSemantic = "
              << (enableEwmaSmoothing ? "thresholded-ewma-smoothed-Abar"
                                       : "thresholded-traffic-graph")
              << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] rawTotalTraffic = "
              << computeTotalTraffic(rawTrafficMatrix) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] controlTotalTraffic = "
              << computeTotalTraffic(controlTrafficMatrix) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] rawTraffic[0][3] = "
              << (numLeaves >= 4 ? rawTrafficMatrix[0][3] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] controlTraffic[0][3] = "
              << (numLeaves >= 4 ? controlTrafficMatrix[0][3] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] rawTraffic[1][2] = "
              << (numLeaves >= 3 ? rawTrafficMatrix[1][2] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] controlTraffic[1][2] = "
              << (numLeaves >= 3 ? controlTrafficMatrix[1][2] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectionMetric = " << selectionMetric << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] eta             = " << eta << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] totalTraffic    = " << totalTraffic << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] degree[0]       = "
              << (numLeaves >= 1 ? nodeDegree[0] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] degree[3]       = "
              << (numLeaves >= 4 ? nodeDegree[3] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] expected[0][3]  = "
              << (numLeaves >= 4 ? expectedTraffic[0][3] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] B[0][3]         = "
              << (numLeaves >= 4 ? modularityGain[0][3] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] U[0][3]         = "
              << (numLeaves >= 4 ? ocsUtility[0][3] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedOcsLeafA   = " << ocsLeafA << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedOcsLeafB   = " << ocsLeafB << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedWeight     = "
              << (enableMatrixSelect ? selectedOcsWeight : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedExpected = "
              << (enableMatrixSelect ? selectedExpectedTraffic : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedB        = "
              << (enableMatrixSelect ? selectedModularityGain : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedUtility  = "
              << (enableMatrixSelect ? selectedOcsUtility : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedBaseUtility = "
              << (enableMatrixSelect ? selectedBaseUtility : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedCommunityFactor = "
              << (enableMatrixSelect ? selectedCommunityFactor : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedCommunityUtility = "
              << (enableMatrixSelect ? selectedCommunityUtility : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedIntraCommunity = "
              << (enableMatrixSelect ? (selectedIntraCommunity ? "true" : "false") : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedWasPreviouslyInstalled = "
              << (enableMatrixSelect ? (selectedWasPreviouslyInstalled ? "true" : "false")
                                     : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedStateHoldingGain = "
              << (enableMatrixSelect ? selectedStateHoldingGain : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedSelectionScore = "
              << (enableMatrixSelect ? selectedSelectionScore : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] traffic[0][1]      = "
              << (numLeaves >= 2 ? torTrafficMatrix[0][1] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] traffic[0][3]      = "
              << (numLeaves >= 4 ? torTrafficMatrix[0][3] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] traffic[1][2]      = "
              << (numLeaves >= 3 ? torTrafficMatrix[1][2] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] traffic[2][3]      = "
              << (numLeaves >= 4 ? torTrafficMatrix[2][3] : 0.0) << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] candidateEdges     = " << candidateEdges.size()
              << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] selectedEdges      = " << selectedOcsEdges.size()
              << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] ocsPortK           = " << ocsPortK << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] maxSelectedOcsLinks = "
              << maxSelectedOcsLinks << std::endl;

    std::cout << "[HYBRID-DCN][CONFIG] enableConfigUpdateGate = "
              << (enableConfigUpdateGate ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] configUpdateThreshold = "
              << configUpdateThreshold << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] configScoreMode = "
              << configScoreMode << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] reconfigurationPenalty = "
              << reconfigurationPenalty << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] candidateConfigScore = "
              << candidateConfigScore << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] previousConfigScore = "
              << previousConfigScore << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] configScoreImprovement = "
              << configScoreImprovement << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] candidateRawUtilitySum = "
              << candidateRawUtilitySum << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] previousRawUtilitySum = "
              << previousRawUtilitySum << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] candidateSelectionScoreSum = "
              << candidateSelectionScoreSum << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] previousSelectionScoreSum = "
              << previousSelectionScoreSum << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] candidateChangedEdges = "
              << candidateChangedEdges << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] previousChangedEdges = "
              << previousChangedEdges << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] candidateEdgeSet = "
              << formatEdgeSet(candidateOcsEdges) << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] previousEdgeSet = "
              << formatEdgeSet(previousOcsEdges) << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] changedEdgeSet = "
              << formatChangedEdgeSet(candidateOcsEdges, previousOcsEdges) << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] decision = " << configGateDecision << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] candidateEdges = "
              << candidateOcsEdges.size() << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] selectedEdges = "
              << selectedOcsEdges.size() << std::endl;
    if (configGateDecision == "keep-previous")
    {
        std::cout << "[HYBRID-DCN][CONFIG] note = keeping previous OCS configuration because improvement does not exceed threshold"
                  << std::endl;
    }
    if (configGateDecision == "hold-previous")
    {
        std::cout << "[HYBRID-DCN][CONFIG] note = keeping previous OCS configuration because minHoldCycles is not satisfied"
                  << std::endl;
    }
    if (enableConfigUpdateGate && previousOcsEdges.empty())
    {
        std::cout << "[HYBRID-DCN][CONFIG] note = previous OCS configuration is empty"
                  << std::endl;
    }
    for (uint32_t edgeIndex = 0; edgeIndex < candidateOcsEdges.size(); ++edgeIndex)
    {
        const auto& edge = candidateOcsEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][CONFIG] candidateEdge[" << edgeIndex
                  << "] = " << edge.leafA << "-" << edge.leafB
                  << " selectionScore=" << edge.selectionScore << std::endl;
    }

    std::string experimentGroup = "manual";
    uint32_t ablationLevel = 0;
    if (presetScenario == "baseline-excess")
    {
        experimentGroup = "baseline";
        ablationLevel = 0;
    }
    else if (presetScenario == "community-aware")
    {
        experimentGroup = "community-ablation";
        ablationLevel = 1;
    }
    else if (presetScenario == "state-aware")
    {
        experimentGroup = "state-ablation";
        ablationLevel = 2;
    }
    else if (presetScenario == "config-gated")
    {
        experimentGroup = "config-gate-ablation";
        ablationLevel = 3;
    }
    else if (presetScenario == "hold-gated")
    {
        experimentGroup = "hold-gate-ablation";
        ablationLevel = 4;
    }
    else if (presetScenario == "full-control")
    {
        experimentGroup = "full-method";
        ablationLevel = 5;
    }
    else if (presetScenario == "full-stack-control")
    {
        experimentGroup = "full-stack-method";
        ablationLevel = 6;
    }

    if (presetScenario != "manual" && presetOverrideMode == "explicit-wins" &&
        !explicitControlArgNames.empty())
    {
        experimentGroup = "customized-preset";
        ablationLevel = 99;
    }

    const bool isMainMethod =
        presetScenario == "full-control" &&
        (presetOverrideMode == "preset-wins" ||
         (presetOverrideMode == "explicit-wins" && explicitControlArgNames.empty()));
    const bool isFullStackMethod =
        presetScenario == "full-stack-control" &&
        (presetOverrideMode == "preset-wins" ||
         (presetOverrideMode == "explicit-wins" && explicitControlArgNames.empty()));
    const bool isBaseline =
        presetScenario == "baseline-excess" &&
        !(presetOverrideMode == "explicit-wins" && !explicitControlArgNames.empty());
    const bool isFullStackPreset = presetScenario == "full-stack-control";
    const bool fullStackControlEnabled = enableOcsAdmissionControl &&
                                         enableEpsWecmp &&
                                         enableEpsWecmpRouting &&
                                         enableMultiPeriodControl &&
                                         enableMultiPeriodWecmpState;

    std::ostringstream enabledModuleSummaryStream;
    enabledModuleSummaryStream << "matrix,"
                               << (enableEwmaSmoothing ? "ewma" : "noEwma")
                               << ","
                               << (communityMode == "louvain" ? "louvain" : "preview")
                               << ","
                               << (selectionMetric == "community-excess"
                                       ? "communityUtility"
                                       : "noCommunityUtility")
                               << ","
                               << (enableStateHolding ? "stateHolding" : "noStateHolding")
                               << ","
                               << (enableConfigUpdateGate ? "configGate" : "noConfigGate")
                               << ","
                               << (enableHoldTimeGate ? "holdGate" : "noHoldGate")
                               << ","
                               << (enableOcsAdmissionControl ? "ocsAdmission"
                                                             : "noOcsAdmission")
                               << ","
                               << (enableEpsWecmp ? "epsWecmp" : "noEpsWecmp")
                               << ","
                               << (enableEpsWecmpRouting ? "epsWecmpRouting"
                                                         : "noEpsWecmpRouting")
                               << ","
                               << (enableMultiPeriodControl ? "multiPeriodControl"
                                                            : "noMultiPeriodControl")
                               << ","
                               << (enableResultValidation ? "resultValidation"
                                                          : "noResultValidation");
    const std::string enabledModuleSummary = enabledModuleSummaryStream.str();

    std::cout << "[HYBRID-DCN][MULTI] enabled = "
              << (enableMultiPeriodControl ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][MULTI] controlEpochs = " << controlEpochs << std::endl;
    std::cout << "[HYBRID-DCN][MULTI] trafficMatrixSequence = "
              << trafficMatrixSequence << std::endl;
    std::cout << "[HYBRID-DCN][MULTI] finalEpoch = "
              << (controlEpochSummaries.empty() ? 0 : controlEpochSummaries.back().epoch)
              << std::endl;
    std::cout << "[HYBRID-DCN][MULTI] finalSelectedOcsEdges = "
              << selectedOcsEdges.size() << std::endl;
    if (enableMultiPeriodControl)
    {
        for (const auto& summary : controlEpochSummaries)
        {
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] trafficMatrixMode = " << summary.trafficMatrixMode << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] rawTrafficMatrixMode = " << summary.trafficMatrixMode << std::endl;
            if (summary.epoch < matrixEpochSummaries.size())
            {
                const auto& matrixSummary = matrixEpochSummaries[summary.epoch];
                std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                          << "] rawTotalTraffic = " << matrixSummary.rawTotalTraffic
                          << std::endl;
                std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                          << "] controlTotalTraffic = "
                          << matrixSummary.controlTotalTraffic << std::endl;
                std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                          << "] rawTraffic[0][3] = " << matrixSummary.rawTraffic03
                          << std::endl;
                std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                          << "] controlTraffic[0][3] = "
                          << matrixSummary.controlTraffic03 << std::endl;
                std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                          << "] rawTraffic[1][2] = " << matrixSummary.rawTraffic12
                          << std::endl;
                std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                          << "] controlTraffic[1][2] = "
                          << matrixSummary.controlTraffic12 << std::endl;
            }
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] matrixUsedForControl = "
                      << (enableEwmaSmoothing ? "ewma" : "raw") << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] ewmaEnabled = "
                      << (enableEwmaSmoothing ? "true" : "false") << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] ewmaBeta = " << ewmaBeta << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] communityCount = " << summary.communityCount << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] candidateOcsEdges = " << summary.candidateOcsEdges << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] previousOcsEdges = " << summary.previousOcsEdges << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] selectedOcsEdges = " << summary.selectedOcsEdges << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] hardHoldEdges = " << summary.hardHoldEdges << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] candidateConfigScore = " << summary.candidateConfigScore
                      << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] previousConfigScore = " << summary.previousConfigScore << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] configScoreImprovement = " << summary.configScoreImprovement
                      << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] configScoreMode = " << summary.configScoreMode << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] candidateChangedEdges = " << summary.candidateChangedEdges
                      << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] previousChangedEdges = " << summary.previousChangedEdges
                      << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] holdTimeActive = "
                      << (summary.holdTimeActive ? "true" : "false") << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] decision = " << summary.decision << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] previousConfigAgeBefore = "
                      << summary.previousConfigAgeBefore << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] previousConfigAgeAfter = "
                      << summary.previousConfigAgeAfter << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] minSelectedEdgeAge = "
                      << summary.minSelectedEdgeAge << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] maxSelectedEdgeAge = "
                      << summary.maxSelectedEdgeAge << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] minHoldEdgeAge = " << summary.minHoldEdgeAge << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] maxHoldEdgeAge = " << summary.maxHoldEdgeAge << std::endl;
        }
    }

    std::cout << "[HYBRID-DCN][MULTI-WECMP] enabled = "
              << (multiPeriodWecmpStateEnabled ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][MULTI-WECMP] residualTelemetrySource = "
                 "control-plane-estimated-residual-load"
              << std::endl;
    std::cout << "[HYBRID-DCN][MULTI-WECMP] realResidualSemantic = "
                 "planned-residual-placeholder"
              << std::endl;
    std::cout << "[HYBRID-DCN][MULTI-WECMP] epochSummaries = "
              << epsWecmpEpochSummaries.size() << std::endl;
    for (const auto& summary : epsWecmpEpochSummaries)
    {
        std::cout << "[HYBRID-DCN][MULTI-WECMP] epoch[" << summary.epoch
                  << "] trafficMatrixMode = " << summary.trafficMatrixMode << std::endl;
        std::cout << "[HYBRID-DCN][MULTI-WECMP] epoch[" << summary.epoch
                  << "] residualPairs = " << summary.residualPairs << std::endl;
        std::cout << "[HYBRID-DCN][MULTI-WECMP] epoch[" << summary.epoch
                  << "] updatedPairs = " << summary.updatedPairs << std::endl;
        std::cout << "[HYBRID-DCN][MULTI-WECMP] epoch[" << summary.epoch
                  << "] totalPlannedResidualDemand = "
                  << summary.totalPlannedResidualDemand << std::endl;
        std::cout << "[HYBRID-DCN][MULTI-WECMP] epoch[" << summary.epoch
                  << "] totalRealResidualDemand = "
                  << summary.totalRealResidualDemand << std::endl;
    }

    std::cout << "[HYBRID-DCN][PAPER] paperStage = stage-26-experiment-group-summary"
              << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] experimentGroup = " << experimentGroup
              << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] ablationLevel = " << ablationLevel << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] isMainMethod = "
              << (isMainMethod ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] isFullStackMethod = "
              << (isFullStackMethod ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] isFullStackPreset = "
              << (isFullStackPreset ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] isBaseline = " << (isBaseline ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] presetScenario = " << presetScenario
              << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] presetOverrideMode = " << presetOverrideMode
              << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] explicitControlArgCount = "
              << explicitControlArgNames.size() << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] enabledModuleSummary = "
              << enabledModuleSummary << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] trafficMatrixSource = "
              << trafficMatrixSource << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] ewmaEnabled = "
              << (enableEwmaSmoothing ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] trafficGraphThreshold = "
              << trafficGraphThreshold << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] ocsAdmissionControlEnabled = "
              << (enableOcsAdmissionControl ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] epsWecmpEnabled = "
              << (enableEpsWecmp ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] epsWecmpRoutingEnabled = "
              << (enableEpsWecmpRouting ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] multiPeriodControlEnabled = "
              << (enableMultiPeriodControl ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] fullStackControlEnabled = "
              << (fullStackControlEnabled ? "true" : "false") << std::endl;

    std::cout << "[HYBRID-DCN][CONTROL] algorithmStage = stage-20-control-summary"
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] experimentGroup = " << experimentGroup
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] ablationLevel = " << ablationLevel
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] isMainMethod = "
              << (isMainMethod ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] isFullStackMethod = "
              << (isFullStackMethod ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] isFullStackPreset = "
              << (isFullStackPreset ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] isBaseline = "
              << (isBaseline ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] enabledModuleSummary = "
              << enabledModuleSummary << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] presetScenario = " << presetScenario
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] presetOverrideMode = " << presetOverrideMode
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] presetApplied = "
              << (presetApplied ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] trafficMatrixSource = "
              << trafficMatrixSource << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] enableEwmaSmoothing = "
              << (enableEwmaSmoothing ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] ewmaEnabled = "
              << (enableEwmaSmoothing ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] ewmaBeta = " << ewmaBeta << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] trafficGraphThreshold = "
              << trafficGraphThreshold << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] ocsAdmissionControlEnabled = "
              << (enableOcsAdmissionControl ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] epsWecmpEnabled = "
              << (enableEpsWecmp ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] epsWecmpRoutingEnabled = "
              << (enableEpsWecmpRouting ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] multiPeriodControlEnabled = "
              << (enableMultiPeriodControl ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] fullStackControlEnabled = "
              << (fullStackControlEnabled ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] enableDetailedAlgorithmTrace = "
              << (enableDetailedAlgorithmTrace ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] detailedCandidateLogLimit = "
              << detailedCandidateLogLimit << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] enableDetailedFlowTrace = "
              << (enableDetailedFlowTrace ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] detailedFlowLogLimit = "
              << detailedFlowLogLimit << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] presetApplicationOrder = after-command-line-parse"
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] explicitArgCount = " << explicitArgNames.size()
              << std::endl;
    const uint32_t explicitArgLogCount =
        static_cast<uint32_t>(std::min<size_t>(explicitArgNames.size(), 20));
    for (uint32_t argIndex = 0; argIndex < explicitArgLogCount; ++argIndex)
    {
        std::cout << "[HYBRID-DCN][CONTROL] explicitArg[" << argIndex
                  << "] = " << explicitArgNames[argIndex] << std::endl;
    }
    std::cout << "[HYBRID-DCN][CONTROL] explicitControlArgCount = "
              << explicitControlArgNames.size() << std::endl;
    const uint32_t explicitControlArgLogCount =
        static_cast<uint32_t>(std::min<size_t>(explicitControlArgNames.size(), 20));
    for (uint32_t argIndex = 0; argIndex < explicitControlArgLogCount; ++argIndex)
    {
        std::cout << "[HYBRID-DCN][CONTROL] explicitControlArg[" << argIndex
                  << "] = " << explicitControlArgNames[argIndex] << std::endl;
    }
    const bool presetIgnoresExplicitControlArgs =
        presetScenario != "manual" && presetOverrideMode == "preset-wins";
    const uint32_t presetIgnoredExplicitControlArgCount =
        presetIgnoresExplicitControlArgs
            ? static_cast<uint32_t>(explicitControlArgNames.size())
            : 0;
    std::cout << "[HYBRID-DCN][CONTROL] presetIgnoredExplicitControlArgCount = "
              << presetIgnoredExplicitControlArgCount << std::endl;
    if (presetIgnoredExplicitControlArgCount > 0)
    {
        const uint32_t ignoredExplicitControlArgLogCount =
            static_cast<uint32_t>(std::min<size_t>(explicitControlArgNames.size(), 20));
        for (uint32_t argIndex = 0; argIndex < ignoredExplicitControlArgLogCount; ++argIndex)
        {
            std::cout << "[HYBRID-DCN][CONTROL] presetIgnoredExplicitControlArg["
                      << argIndex << "] = " << explicitControlArgNames[argIndex]
                      << std::endl;
        }
    }
    std::cout << "[HYBRID-DCN][CONTROL] trafficMatrixMode = " << trafficMatrixMode
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] communityMode = " << communityMode << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] selectionMetric = " << selectionMetric
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] eta = " << eta << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] communityAlpha = " << communityAlpha
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] enableStateHolding = "
              << (enableStateHolding ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] stateHoldingLambda = " << stateHoldingLambda
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] previousOcsMode = " << previousOcsMode
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] enableConfigUpdateGate = "
              << (enableConfigUpdateGate ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] configUpdateThreshold = "
              << configUpdateThreshold << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] configScoreMode = "
              << configScoreMode << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] reconfigurationPenalty = "
              << reconfigurationPenalty << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] enableHoldTimeGate = "
              << (enableHoldTimeGate ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] minHoldCycles = " << minHoldCycles
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] previousConfigAge = " << previousConfigAge
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] ocsPortK = " << ocsPortK << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] maxSelectedOcsLinks = "
              << maxSelectedOcsLinks << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] routeMode = " << routeMode << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] totalTraffic = " << totalTraffic << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] candidateEdgeCount = "
              << candidateEdges.size() << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] candidateOcsEdges = "
              << candidateOcsEdges.size() << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] previousOcsEdges = "
              << previousOcsEdges.size() << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] finalSelectedOcsEdges = "
              << selectedOcsEdges.size() << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] configGateDecision = "
              << configGateDecision << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] holdTimeActive = "
              << (holdTimeActive ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] candidateConfigScore = "
              << candidateConfigScore << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] previousConfigScore = "
              << previousConfigScore << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] configScoreImprovement = "
              << configScoreImprovement << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] candidateRawUtilitySum = "
              << candidateRawUtilitySum << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] previousRawUtilitySum = "
              << previousRawUtilitySum << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] candidateSelectionScoreSum = "
              << candidateSelectionScoreSum << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] previousSelectionScoreSum = "
              << previousSelectionScoreSum << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] candidateChangedEdges = "
              << candidateChangedEdges << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] previousChangedEdges = "
              << previousChangedEdges << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] candidateEdgeSet = "
              << formatEdgeSet(candidateOcsEdges) << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] previousEdgeSet = "
              << formatEdgeSet(previousOcsEdges) << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] changedEdgeSet = "
              << formatChangedEdgeSet(candidateOcsEdges, previousOcsEdges) << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] hasTrafficMatrix = true" << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] hasCommunityLabels = true" << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] hasCommunityUtility = "
              << (selectionMetric == "community-excess" ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] hasStateHolding = "
              << (enableStateHolding ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] hasConfigGate = "
              << (enableConfigUpdateGate ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] hasHoldGate = "
              << (enableHoldTimeGate ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] finalConfigInstalled = "
              << (installedOcsLinks.size() == selectedOcsEdges.size() ? "true" : "false")
              << std::endl;
    for (uint32_t edgeIndex = 0; edgeIndex < selectedOcsEdges.size(); ++edgeIndex)
    {
        const auto& edge = selectedOcsEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][CONTROL] finalEdge[" << edgeIndex
                  << "] = " << edge.leafA << "-" << edge.leafB
                  << " score=" << edge.selectionScore
                  << " utility=" << edge.utility
                  << " baseUtility=" << edge.baseUtility
                  << " communityUtility=" << edge.communityUtility
                  << " stateHoldingGain=" << edge.stateHoldingGain << std::endl;
    }

    std::cout << "[HYBRID-DCN][EXPERIMENT] experimentName = " << experimentName
              << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] presetScenario = " << presetScenario
              << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] presetOverrideMode = "
              << presetOverrideMode << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] simTime = " << simTime << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] numSpines = " << numSpines << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] numLeaves = " << numLeaves << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] serversPerLeaf = " << serversPerLeaf
              << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] ocsDataRate = " << ocsDataRate
              << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] ocsDelay = " << ocsDelay << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] enableMatrixFlows = "
              << (enableMatrixFlows ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] matrixFlowMaxBytes = "
              << matrixFlowMaxBytes << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] matrixFlowStart = " << matrixFlowStart
              << std::endl;
    std::cout << "[HYBRID-DCN][EXPERIMENT] matrixFlowPortBase = "
              << matrixFlowPortBase << std::endl;

    const uint32_t candidateLogCount =
        static_cast<uint32_t>(std::min<size_t>(candidateEdges.size(), 3));
    for (uint32_t edgeIndex = 0; edgeIndex < candidateLogCount; ++edgeIndex)
    {
        const auto& edge = candidateEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][MATRIX] candidate[" << edgeIndex << "] = " << edge.leafA
                  << "-" << edge.leafB << " traffic=" << edge.traffic
                  << " expected=" << edge.expected << " B=" << edge.modularityGain
                  << " baseUtility=" << edge.baseUtility
                  << " communityFactor=" << edge.communityFactor
                  << " communityUtility=" << edge.communityUtility
                  << " U=" << edge.utility
                  << " wasPreviouslyInstalled="
                  << (edge.wasPreviouslyInstalled ? "true" : "false")
                  << " stateHoldingGain=" << edge.stateHoldingGain
                  << " selectionScore=" << edge.selectionScore << std::endl;
    }

    for (uint32_t edgeIndex = 0; edgeIndex < selectedOcsEdges.size(); ++edgeIndex)
    {
        const auto& edge = selectedOcsEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][MATRIX] selectedEdge[" << edgeIndex << "] = "
                  << edge.leafA << "-" << edge.leafB << " traffic=" << edge.traffic
                  << " expected=" << edge.expected << " B=" << edge.modularityGain
                  << " U=" << edge.utility << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX] selectedEdge[" << edgeIndex
                  << "] baseUtility = " << edge.baseUtility << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX] selectedEdge[" << edgeIndex
                  << "] communityFactor = " << edge.communityFactor << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX] selectedEdge[" << edgeIndex
                  << "] communityUtility = " << edge.communityUtility << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX] selectedEdge[" << edgeIndex
                  << "] intraCommunity = " << (edge.intraCommunity ? "true" : "false")
                  << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX] selectedEdge[" << edgeIndex
                  << "] wasPreviouslyInstalled = "
                  << (edge.wasPreviouslyInstalled ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX] selectedEdge[" << edgeIndex
                  << "] stateHoldingGain = " << edge.stateHoldingGain << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX] selectedEdge[" << edgeIndex
                  << "] selectionScore = " << edge.selectionScore << std::endl;
    }

    std::cout << "[HYBRID-DCN][STATE] enableStateHolding = "
              << (enableStateHolding ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][STATE] stateHoldingLambda = " << stateHoldingLambda
              << std::endl;
    std::cout << "[HYBRID-DCN][STATE] previousOcsMode = " << previousOcsMode << std::endl;
    std::cout << "[HYBRID-DCN][STATE] previousOcsEdges = " << previousOcsEdges.size()
              << std::endl;
    for (uint32_t edgeIndex = 0; edgeIndex < previousOcsEdges.size(); ++edgeIndex)
    {
        const auto& edge = previousOcsEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][STATE] previousEdge[" << edgeIndex
                  << "] = " << edge.leafA << "-" << edge.leafB << std::endl;
    }

    std::cout << "[HYBRID-DCN][HOLD] enableHoldTimeGate = "
              << (enableHoldTimeGate ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][HOLD] minHoldCycles = " << minHoldCycles << std::endl;
    std::cout << "[HYBRID-DCN][HOLD] previousConfigAge = " << previousConfigAge
              << std::endl;
    std::cout << "[HYBRID-DCN][HOLD] previousConfigAvailable = "
              << (previousConfigAvailable ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][HOLD] holdTimeActive = "
              << (holdTimeActive ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][HOLD] hardHoldEdges = " << holdOcsEdges.size()
              << std::endl;
    std::cout << "[HYBRID-DCN][HOLD] hardHoldUsesRemainingPorts = true" << std::endl;
    for (uint32_t edgeIndex = 0; edgeIndex < holdOcsEdges.size(); ++edgeIndex)
    {
        const auto& edge = holdOcsEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][HOLD] holdEdge[" << edgeIndex
                  << "] = " << edge.leafA << "-" << edge.leafB << std::endl;
    }
    if (holdTimeActive)
    {
        std::cout << "[HYBRID-DCN][HOLD] note = hard-hold edges occupy OCS ports before greedy candidate selection"
                  << std::endl;
    }

    std::cout << "[HYBRID-DCN][HOLD-AGE] edgeAgeMode = per-edge" << std::endl;
    std::cout << "[HYBRID-DCN][HOLD-AGE] previousConfigAgeCompatibility = "
              << previousConfigAge << std::endl;
    std::cout << "[HYBRID-DCN][HOLD-AGE] selectedEdgeAges = "
              << selectedOcsEdges.size() << std::endl;
    for (uint32_t edgeIndex = 0; edgeIndex < selectedOcsEdges.size(); ++edgeIndex)
    {
        const auto& edge = selectedOcsEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][HOLD-AGE] selectedEdge[" << edgeIndex
                  << "] = " << edge.leafA << "-" << edge.leafB
                  << " age="
                  << GetOcsEdgeAge(selectedOcsEdgeAges, edge.leafA, edge.leafB)
                  << std::endl;
    }
    for (uint32_t edgeIndex = 0; edgeIndex < holdOcsEdges.size(); ++edgeIndex)
    {
        const auto& edge = holdOcsEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][HOLD-AGE] holdEdge[" << edgeIndex
                  << "] = " << edge.leafA << "-" << edge.leafB
                  << " age=" << GetOcsEdgeAge(previousOcsEdgeAges, edge.leafA, edge.leafB)
                  << std::endl;
    }

    std::cout << "[HYBRID-DCN][COMMUNITY] previewEnabled = true" << std::endl;
    std::cout << "[HYBRID-DCN][COMMUNITY] communityMode = " << communityMode << std::endl;
    std::cout << "[HYBRID-DCN][COMMUNITY] communityAlpha = " << communityAlpha
              << std::endl;
    std::cout << "[HYBRID-DCN][COMMUNITY] intraCommunityFactor = 1" << std::endl;
    std::cout << "[HYBRID-DCN][COMMUNITY] interCommunityFactor = " << communityAlpha
              << std::endl;
    std::cout << "[HYBRID-DCN][COMMUNITY] source = "
              << (communityMode == "louvain"
                      ? (louvainMode == "multi-level" ? "louvain-multi-level"
                                                       : "louvain-single-level")
                      : "trafficMatrixMode-preview")
              << std::endl;
    std::cout << "[HYBRID-DCN][COMMUNITY] communityCount = "
              << activeCommunityCount << std::endl;
    if (communityMode == "louvain")
    {
        std::cout << "[HYBRID-DCN][LOUVAIN] mode = " << louvainMode << std::endl;
        std::cout << "[HYBRID-DCN][LOUVAIN] implementation = "
                  << (louvainMode == "multi-level"
                          ? "multi-level-local-moving-with-folding"
                          : "single-level-local-moving")
                  << std::endl;
        std::cout << "[HYBRID-DCN][LOUVAIN] foldedGraph = "
                  << (louvainResult.foldedGraph ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][LOUVAIN] maxLevels = " << louvainMaxLevels
                  << std::endl;
        std::cout << "[HYBRID-DCN][LOUVAIN] levels = " << louvainResult.levels
                  << std::endl;
        std::cout << "[HYBRID-DCN][LOUVAIN] maxPasses = " << louvainMaxPasses
                  << std::endl;
        std::cout << "[HYBRID-DCN][LOUVAIN] epsilon = " << louvainEpsilon << std::endl;
        std::cout << "[HYBRID-DCN][LOUVAIN] passes = " << louvainResult.passes
                  << std::endl;
        std::cout << "[HYBRID-DCN][LOUVAIN] moved = "
                  << (louvainResult.moved ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][LOUVAIN] modularityQ = "
                  << louvainResult.modularityQ << std::endl;
        for (const auto& levelSummary : louvainResult.levelSummaries)
        {
            std::cout << "[HYBRID-DCN][LOUVAIN] level[" << levelSummary.level
                      << "] nodeCountBefore = " << levelSummary.nodeCountBefore << std::endl;
            std::cout << "[HYBRID-DCN][LOUVAIN] level[" << levelSummary.level
                      << "] nodeCountAfter = " << levelSummary.nodeCountAfter << std::endl;
            std::cout << "[HYBRID-DCN][LOUVAIN] level[" << levelSummary.level
                      << "] communityCount = " << levelSummary.communityCount << std::endl;
            std::cout << "[HYBRID-DCN][LOUVAIN] level[" << levelSummary.level
                      << "] passes = " << levelSummary.passes << std::endl;
            std::cout << "[HYBRID-DCN][LOUVAIN] level[" << levelSummary.level
                      << "] moved = " << (levelSummary.moved ? "true" : "false")
                      << std::endl;
            std::cout << "[HYBRID-DCN][LOUVAIN] level[" << levelSummary.level
                      << "] modularityQ = " << levelSummary.modularityQ << std::endl;
        }
    }
    if (trafficMatrixMode == "uniform" && communityMode == "preview")
    {
        std::cout
            << "[HYBRID-DCN][COMMUNITY] note = uniform matrix diagnostic preview only"
            << std::endl;
    }
    for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
    {
        std::cout << "[HYBRID-DCN][COMMUNITY] leaf-" << leafIndex
                  << " community = " << activeCommunityLabels[leafIndex] << std::endl;
    }
    std::cout << "[HYBRID-DCN][COMMUNITY] intraCandidateEdges = "
              << intraCandidateEdges << std::endl;
    std::cout << "[HYBRID-DCN][COMMUNITY] interCandidateEdges = "
              << interCandidateEdges << std::endl;
    if (selectedOcsEdges.empty())
    {
        std::cout << "[HYBRID-DCN][COMMUNITY] selectedEdges = 0" << std::endl;
    }
    for (uint32_t edgeIndex = 0; edgeIndex < selectedOcsEdges.size(); ++edgeIndex)
    {
        const auto& edge = selectedOcsEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][COMMUNITY] selectedEdge[" << edgeIndex
                  << "] intraCommunity = "
                  << (isIntraCommunity(edge.leafA, edge.leafB) ? "true" : "false")
                  << std::endl;
    }

    std::vector<uint32_t> replayOcsDegree(numLeaves, 0);
    std::vector<OcsCandidateEdge> replaySelectedEdges;
    std::vector<std::string> replayRejectReasons(candidateEdges.size(),
                                                 "not-selected-greedy-order");
    for (const auto& holdEdge : holdOcsEdges)
    {
        replaySelectedEdges.push_back(holdEdge);
        if (holdEdge.leafA < numLeaves)
        {
            replayOcsDegree[holdEdge.leafA]++;
        }
        if (holdEdge.leafB < numLeaves)
        {
            replayOcsDegree[holdEdge.leafB]++;
        }
    }
    for (uint32_t candidateIndex = 0; candidateIndex < candidateEdges.size();
         ++candidateIndex)
    {
        const auto& edge = candidateEdges[candidateIndex];
        if (isEdgeInSet(holdOcsEdges, edge.leafA, edge.leafB))
        {
            replayRejectReasons[candidateIndex] = "hard-hold-preserved";
            continue;
        }
        if (edge.selectionScore <= 0)
        {
            replayRejectReasons[candidateIndex] = "non-positive-score";
            continue;
        }
        if (replaySelectedEdges.size() >= maxSelectedOcsLinks)
        {
            replayRejectReasons[candidateIndex] = "max-selected-links";
            continue;
        }
        if (replayOcsDegree[edge.leafA] >= ocsPortK ||
            replayOcsDegree[edge.leafB] >= ocsPortK)
        {
            replayRejectReasons[candidateIndex] = "ocs-port-budget";
            continue;
        }

        replaySelectedEdges.push_back(edge);
        replayOcsDegree[edge.leafA]++;
        replayOcsDegree[edge.leafB]++;
        replayRejectReasons[candidateIndex] = "selected";
    }
    const bool greedyReplayMatchesSelected =
        makeEdgeSet(replaySelectedEdges) == makeEdgeSet(selectedOcsEdges);

    if (enableDetailedAlgorithmTrace)
    {
        std::cout << "[HYBRID-DCN][TRACE] enableDetailedAlgorithmTrace = true"
                  << std::endl;
        std::cout << "[HYBRID-DCN][TRACE] detailedCandidateLogLimit = "
                  << detailedCandidateLogLimit << std::endl;
        std::cout << "[HYBRID-DCN][TRACE] louvainInputMatrixSemantic = controlMatrix"
                  << std::endl;
        std::cout << "[HYBRID-DCN][TRACE] louvainMode = " << louvainMode
                  << std::endl;
        std::cout << "[HYBRID-DCN][TRACE] louvainModularityQ = "
                  << louvainResult.modularityQ << std::endl;
        std::cout << "[HYBRID-DCN][TRACE] communityLabelVector = "
                  << FormatCommunityLabelVector(activeCommunityLabels) << std::endl;
        std::cout << "[HYBRID-DCN][TRACE] candidateSortRule = "
                     "descending-selectionScore-ascending-leafA-ascending-leafB"
                  << std::endl;
        std::cout << "[HYBRID-DCN][TRACE] traceScope = final-control-state"
                  << std::endl;
        if (enableMultiPeriodControl)
        {
            std::cout << "[HYBRID-DCN][TRACE] multiPeriodDetailedTrace = final-epoch-only"
                      << std::endl;
        }
        for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
        {
            std::cout << "[HYBRID-DCN][TRACE] nodeDegree[" << leafIndex
                      << "] = " << nodeDegree[leafIndex] << std::endl;
        }
        std::cout << "[HYBRID-DCN][TRACE] totalTraffic = " << totalTraffic
                  << std::endl;

        uint32_t matrixTraceCount = 0;
        for (uint32_t leafA = 0;
             leafA < numLeaves && matrixTraceCount < detailedCandidateLogLimit;
             ++leafA)
        {
            for (uint32_t leafB = leafA + 1;
                 leafB < numLeaves && matrixTraceCount < detailedCandidateLogLimit;
                 ++leafB)
            {
                const OcsCandidateEdge traceEdge = makeCandidateEdge(leafA, leafB);
                std::cout << "[HYBRID-DCN][TRACE] expectedTraffic[" << leafA << "]["
                          << leafB << "] = " << expectedTraffic[leafA][leafB]
                          << std::endl;
                std::cout << "[HYBRID-DCN][TRACE] modularityGain[" << leafA << "]["
                          << leafB << "] = " << modularityGain[leafA][leafB]
                          << std::endl;
                std::cout << "[HYBRID-DCN][TRACE] utility[" << leafA << "]["
                          << leafB << "] = " << traceEdge.utility << std::endl;
                std::cout << "[HYBRID-DCN][TRACE] communityFactor[" << leafA << "]["
                          << leafB << "] = " << traceEdge.communityFactor << std::endl;
                std::cout << "[HYBRID-DCN][TRACE] selectionScore[" << leafA << "]["
                          << leafB << "] = " << traceEdge.selectionScore << std::endl;
                matrixTraceCount++;
            }
        }

        std::cout << "[HYBRID-DCN][TRACE] greedyReplayMatchesSelected = "
                  << (greedyReplayMatchesSelected ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][TRACE] replaySelectedEdgeSet = "
                  << formatEdgeSet(replaySelectedEdges) << std::endl;

        const uint32_t sortedCandidateLogCount =
            static_cast<uint32_t>(std::min<std::size_t>(candidateEdges.size(),
                                                        detailedCandidateLogLimit));
        for (uint32_t candidateIndex = 0; candidateIndex < sortedCandidateLogCount;
             ++candidateIndex)
        {
            const auto& edge = candidateEdges[candidateIndex];
            const bool selected = isEdgeInSet(replaySelectedEdges, edge.leafA, edge.leafB);
            const std::string rejectReason = replayRejectReasons[candidateIndex];

            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] pair = " << edge.leafA << "-" << edge.leafB << std::endl;
            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] traffic = " << edge.traffic << std::endl;
            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] expected = " << edge.expected << std::endl;
            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] modularityGain = " << edge.modularityGain << std::endl;
            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] utility = " << edge.utility << std::endl;
            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] communityFactor = " << edge.communityFactor << std::endl;
            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] stateHoldingGain = " << edge.stateHoldingGain << std::endl;
            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] selectionScore = " << edge.selectionScore << std::endl;
            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] selected = " << (selected ? "true" : "false") << std::endl;
            std::cout << "[HYBRID-DCN][TRACE] sortedCandidate[" << candidateIndex
                      << "] rejectReason = " << rejectReason << std::endl;
        }
    }
    else
    {
        std::cout << "[HYBRID-DCN][TRACE] enableDetailedAlgorithmTrace = false"
                  << std::endl;
    }

    std::cout << "[HYBRID-DCN][MATRIX] instantiatedOcsLeafA = " << ocsLeafA << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] instantiatedOcsLeafB = " << ocsLeafB << std::endl;

    if (enableEcho && enableBulk)
    {
        std::cout << "[HYBRID-DCN][WARN] Echo and Bulk are both enabled; Stage-3 metrics are for "
                     "engineering validation only."
                  << std::endl;
    }

    std::cout << "[HYBRID-DCN][ROUTE] routeMode       = " << routeMode << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE] bulkSrcAddress  = ";
    if (enableBulk)
    {
        std::cout << bulkSrcAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE] bulkDstAddress  = ";
    if (enableBulk)
    {
        std::cout << bulkDstAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE] ocsLeafAAddress = ";
    if (enableStaticOcs)
    {
        std::cout << ocsLeafAAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE] ocsLeafBAddress = ";
    if (enableStaticOcs)
    {
        std::cout << ocsLeafBAddress;
    }
    else
    {
        std::cout << "N/A";
    }
    std::cout << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE] ocsForced       = " << (ocsForced ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE] ocsPairRule       = "
              << (ocsForced ? "true" : "false") << std::endl;
    if (ocsForced)
    {
        std::cout << "[HYBRID-DCN][ROUTE] ocsPairLeafA      = " << ocsLeafA << std::endl;
        std::cout << "[HYBRID-DCN][ROUTE] ocsPairLeafB      = " << ocsLeafB << std::endl;
    }
    std::cout << "[HYBRID-DCN][ROUTE] ocsPairRules      = "
              << (ocsForced ? installedOcsLinks.size() : 0) << std::endl;
    if (ocsForced)
    {
        for (uint32_t linkIndex = 0; linkIndex < installedOcsLinks.size(); ++linkIndex)
        {
            const auto& link = installedOcsLinks[linkIndex];
            std::cout << "[HYBRID-DCN][ROUTE] ocsPairRule[" << linkIndex << "] = "
                      << link.leafA << "-" << link.leafB << std::endl;
        }
    }
    std::cout << "[HYBRID-DCN][ROUTE] ocsPairHostRoutes = " << ocsPairHostRoutes
              << std::endl;
    std::cout << "[HYBRID-DCN][ROUTE] ocsPairHostRoutesSkippedForEpsResidual = "
              << ocsPairHostRoutesSkippedForEpsResidual << std::endl;
    if (routeMode == "ocs-forced")
    {
        std::cout << "[HYBRID-DCN][VERIFY] primaryCoveredPairExpectedPath   = "
                  << (isOcsPairInstalled(bulkSrcLeaf, bulkDstLeaf) ? "OCS" : "EPS")
                  << std::endl;
        std::cout << "[HYBRID-DCN][VERIFY] secondCoveredPairExpectedPath    = "
                  << (enableSecondBulk
                          ? (isOcsPairInstalled(secondBulkSrcLeaf, secondBulkDstLeaf) ? "OCS"
                                                                                      : "EPS")
                          : "disabled")
                  << std::endl;
        std::cout << "[HYBRID-DCN][VERIFY] residualPairExpectedPath         = EPS"
                  << std::endl;
    }
    else
    {
        std::cout << "[HYBRID-DCN][VERIFY] primaryCoveredPairExpectedPath   = global-routing"
                  << std::endl;
        std::cout << "[HYBRID-DCN][VERIFY] secondCoveredPairExpectedPath    = "
                  << (enableSecondBulk ? "global-routing" : "disabled") << std::endl;
        std::cout << "[HYBRID-DCN][VERIFY] residualPairExpectedPath         = global-routing"
                  << std::endl;
    }

    AnimationInterface anim("../sim/results/raw/hybrid-dcn-anim.xml");
    anim.EnablePacketMetadata(true);
    if (enableStaticOcs)
    {
        for (const auto& link : installedOcsLinks)
        {
            std::ostringstream leafADescription;
            leafADescription << "leaf-" << link.leafA << "-OCS";
            anim.UpdateNodeDescription(leaves.Get(link.leafA), leafADescription.str());

            std::ostringstream leafBDescription;
            leafBDescription << "leaf-" << link.leafB << "-OCS";
            anim.UpdateNodeDescription(leaves.Get(link.leafB), leafBDescription.str());
        }
    }

    const double layerXOffset = 40.0;
    const double layerSpacing = 40.0;
    const double serverSpacing = 8.0;

    auto layerX = [layerXOffset, layerSpacing](uint32_t index) {
        return layerXOffset + (static_cast<double>(index) * layerSpacing);
    };

    for (uint32_t spineIndex = 0; spineIndex < numSpines; ++spineIndex)
    {
        AnimationInterface::SetConstantPosition(spines.Get(spineIndex), layerX(spineIndex), 0.0);
    }

    for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
    {
        const double leafX = layerX(leafIndex);
        AnimationInterface::SetConstantPosition(leaves.Get(leafIndex), leafX, 50.0);

        for (uint32_t serverOffset = 0; serverOffset < serversPerLeaf; ++serverOffset)
        {
            const double serverX =
                leafX + ((static_cast<double>(serverOffset) -
                          (static_cast<double>(serversPerLeaf - 1) / 2.0)) *
                         serverSpacing);
            AnimationInterface::SetConstantPosition(
                servers.Get(serverIndex(leafIndex, serverOffset)),
                serverX,
                100.0);
        }
    }

    g_ocsTxPackets = 0;
    g_ocsTxBytes = 0;
    g_epsTxPackets = 0;
    g_epsTxBytes = 0;

    if (enableMeasuredWecmpLaterFlowProof && measuredWecmpLaterFlowIndex >= 0)
    {
        Simulator::Schedule(
            Seconds(measuredWecmpLaterDecisionTime),
            [&]() {
                if (measuredWecmpLaterFlowIndex < 0 ||
                    static_cast<std::size_t>(measuredWecmpLaterFlowIndex) >=
                        matrixFlowSpecs.size())
                {
                    return;
                }

                const uint32_t flowIndex =
                    static_cast<uint32_t>(measuredWecmpLaterFlowIndex);
                auto& spec = matrixFlowSpecs[flowIndex];
                if (!requiresEpsResidualPath(spec))
                {
                    return;
                }

                EpsWecmpDecision baselineDecision =
                    runEpsWecmpUpdateForPair(spec.srcLeaf,
                                             spec.dstLeaf,
                                             spec.wecmpResidualDemand,
                                             false,
                                             "control-plane",
                                             true,
                                             false);
                EpsWecmpDecision measuredDecision =
                    runEpsWecmpUpdateForPair(spec.srcLeaf,
                                             spec.dstLeaf,
                                             spec.wecmpResidualDemand,
                                             true,
                                             "measured-snapshot",
                                             true,
                                             false);
                measuredDecision.controlPlaneSelectedSpine = baselineDecision.selectedSpine;
                const uint32_t selectedSpine = measuredDecision.selectedSpine;

                matrixFlowWecmpDecisionIndex[flowIndex] =
                    static_cast<int32_t>(epsWecmpDecisions.size());
                spec.measuredLaterDecisionIndex = matrixFlowWecmpDecisionIndex[flowIndex];
                epsWecmpDecisions.push_back(measuredDecision);

                const Ipv4Address srcServerAddress =
                    serverIpv4[spec.srcLeaf][spec.srcServer];
                const Ipv4Address dstServerAddress =
                    serverIpv4[spec.dstLeaf][spec.dstServer];
                const auto installResult =
                    installEpsHostRoutesForMatrixFlow(spec, selectedSpine);
                const bool installed = installResult.first;
                const uint32_t installedHostRoutes = installResult.second;

                routeFixEpsResidualHostRoutes += installedHostRoutes;
                if (installed)
                {
                    spec.epsPathFrozen = true;
                    spec.frozenSpine = static_cast<int32_t>(selectedSpine);
                    spec.measuredLaterRouteInstalled = true;
                    routeFixWecmpFrozenRoutes++;
                }

                routeFixRecords.push_back({flowIndex,
                                           spec.srcLeaf,
                                           spec.dstLeaf,
                                           selectedSpine,
                                           spec.residualPathReason,
                                           "measured-later-wecmp-frozen",
                                           installedHostRoutes,
                                           installed});
                epsWecmpRouteBindings.push_back({flowIndex,
                                                 spec.srcLeaf,
                                                 spec.dstLeaf,
                                                 selectedSpine,
                                                 srcServerAddress,
                                                 dstServerAddress,
                                                 installed,
                                                 installed,
                                                 spec.wecmpResidualDemand});

                spec.measuredLaterSelectedSpine = static_cast<int32_t>(selectedSpine);
                spec.measuredLaterControlPlaneSelectedSpine =
                    static_cast<int32_t>(baselineDecision.selectedSpine);
                spec.measuredLaterSelectedSpineChanged =
                    selectedSpine != baselineDecision.selectedSpine;
                spec.measuredLaterHasMeasuredSample =
                    measuredDecision.measuredDecisionUsed &&
                    !measuredDecision.measuredDecisionFallback &&
                    !measuredDecision.measuredNoSample;
                spec.measuredLaterAppliesToLaterFlow = measuredDecision.appliesToLaterFlow;
            });
    }

    std::vector<LinkUtilizationSample> linkUtilizationSamples;
    uint32_t linkUtilizationNextSampleIndex = 0;
    const bool linkTimeseriesEnabled = linkUtilizationSampleInterval > 0.0;
    if (linkTimeseriesEnabled && !linkCounters.empty())
    {
        Simulator::Schedule(Seconds(linkUtilizationSampleInterval),
                            &SampleLinkUtilizationTimeSeries,
                            &linkCounters,
                            &linkUtilizationSamples,
                            &epsMeasuredLeafToSpineSnapshots,
                            &epsMeasuredSpineToLeafSnapshots,
                            &measuredWecmpSnapshotUpdateCount,
                            linkUtilizationSampleInterval,
                            simTime,
                            &linkUtilizationNextSampleIndex);
    }

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    if (measuredWecmpNoSampleError)
    {
        std::cerr
            << "[HYBRID-DCN][ERROR] measured WECMP requested but no measured sample was available and measuredWecmpNoSampleFallback=error."
            << std::endl;
        return 1;
    }

    if (enableMatrixFlows)
    {
        for (uint32_t flowIndex = 0; flowIndex < matrixFlowSinks.size(); ++flowIndex)
        {
            matrixFlowStats[flowIndex].rxBytes = matrixFlowSinks[flowIndex]->GetTotalRx();
            matrixFlowStats[flowIndex].seenFirstRx = matrixFlowStats[flowIndex].rxBytes > 0;
        }
    }

    std::cout << "[HYBRID-DCN][BULK] enabled          = " << (enableBulk ? "true" : "false")
              << std::endl;
    if (enableBulk)
    {
        const double activeDuration = simTime - bulkStart;
        const double observedFct = g_bulkSeenFirstRx ? (g_bulkLastRxTime - bulkStart) : 0.0;
        const double rxDuration = g_bulkSeenFirstRx ? (g_bulkLastRxTime - g_bulkFirstRxTime) : 0.0;
        const double completionRatio =
            bulkMaxBytes > 0 ? static_cast<double>(g_bulkRxBytes) / static_cast<double>(bulkMaxBytes)
                             : 0.0;
        const bool completed = (g_bulkRxBytes >= bulkMaxBytes);
        const double avgGoodputMbps =
            static_cast<double>(g_bulkRxBytes) * 8.0 / activeDuration / 1e6;

        std::cout << "[HYBRID-DCN][BULK] src              = " << bulkSrcName << std::endl;
        std::cout << "[HYBRID-DCN][BULK] dst              = " << bulkDstName << std::endl;
        std::cout << "[HYBRID-DCN][BULK] dstAddress       = " << bulkDstAddress << std::endl;
        std::cout << "[HYBRID-DCN][BULK] bulkMaxBytes     = " << bulkMaxBytes << std::endl;
        std::cout << "[HYBRID-DCN][BULK] rxBytes          = " << g_bulkRxBytes << std::endl;
        std::cout << "[HYBRID-DCN][BULK] firstRxTime      = "
                  << (g_bulkSeenFirstRx ? g_bulkFirstRxTime : 0.0) << " s" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] lastRxTime       = "
                  << (g_bulkSeenFirstRx ? g_bulkLastRxTime : 0.0) << " s" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] observedFct      = " << observedFct << " s"
                  << std::endl;
        std::cout << "[HYBRID-DCN][BULK] rxDuration       = " << rxDuration << " s"
                  << std::endl;
        std::cout << "[HYBRID-DCN][BULK] completionRatio  = " << completionRatio << std::endl;
        std::cout << "[HYBRID-DCN][BULK] completed        = "
                  << (completed ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][BULK] avgGoodputMbps   = " << avgGoodputMbps << " Mbps"
                  << std::endl;

        if (g_bulkRxBytes == 0)
        {
            std::cout << "[HYBRID-DCN][BULK][WARN] No bytes received by PacketSink." << std::endl;
        }

        if (!completed)
        {
            std::cout << "[HYBRID-DCN][BULK][WARN] Bulk flow did not complete before simTime."
                      << std::endl;
        }
    }
    else
    {
        std::cout << "[HYBRID-DCN][BULK] src              = N/A" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] dst              = N/A" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] dstAddress       = N/A" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] bulkMaxBytes     = 0" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] rxBytes          = 0" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] firstRxTime      = 0 s" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] lastRxTime       = 0 s" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] observedFct      = 0 s" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] rxDuration       = 0 s" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] completionRatio  = 0" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] completed        = false" << std::endl;
        std::cout << "[HYBRID-DCN][BULK] avgGoodputMbps   = 0 Mbps" << std::endl;
    }

    std::cout << "[HYBRID-DCN][SECOND] enabled          = "
              << (enableSecondBulk ? "true" : "false") << std::endl;
    if (enableSecondBulk)
    {
        const double secondActiveDuration = simTime - secondBulkStart;
        const double secondObservedFct =
            g_secondBulkSeenFirstRx ? (g_secondBulkLastRxTime - secondBulkStart) : 0.0;
        const double secondCompletionRatio =
            secondBulkMaxBytes > 0
                ? static_cast<double>(g_secondBulkRxBytes) /
                      static_cast<double>(secondBulkMaxBytes)
                : 0.0;
        const bool secondCompleted = (g_secondBulkRxBytes >= secondBulkMaxBytes);
        const double secondAvgGoodputMbps =
            static_cast<double>(g_secondBulkRxBytes) * 8.0 / secondActiveDuration / 1e6;

        std::cout << "[HYBRID-DCN][SECOND] src              = " << secondBulkSrcName
                  << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] dst              = " << secondBulkDstName
                  << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] completed        = "
                  << (secondCompleted ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] rxBytes          = "
                  << g_secondBulkRxBytes << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] completionRatio  = "
                  << secondCompletionRatio << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] observedFct      = " << secondObservedFct
                  << " s" << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] avgGoodputMbps   = "
                  << secondAvgGoodputMbps << " Mbps" << std::endl;
    }
    else
    {
        std::cout << "[HYBRID-DCN][SECOND] src              = N/A" << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] dst              = N/A" << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] completed        = false" << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] rxBytes          = 0" << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] completionRatio  = 0" << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] observedFct      = 0 s" << std::endl;
        std::cout << "[HYBRID-DCN][SECOND] avgGoodputMbps   = 0 Mbps" << std::endl;
    }

    std::cout << "[HYBRID-DCN][RESIDUAL] enabled          = "
              << (enableResidualBulk ? "true" : "false") << std::endl;
    if (enableResidualBulk)
    {
        const double residualActiveDuration = simTime - residualBulkStart;
        const double residualObservedFct =
            g_residualBulkSeenFirstRx ? (g_residualBulkLastRxTime - residualBulkStart) : 0.0;
        const double residualCompletionRatio =
            residualBulkMaxBytes > 0
                ? static_cast<double>(g_residualBulkRxBytes) /
                      static_cast<double>(residualBulkMaxBytes)
                : 0.0;
        const bool residualCompleted = (g_residualBulkRxBytes >= residualBulkMaxBytes);
        const double residualAvgGoodputMbps =
            static_cast<double>(g_residualBulkRxBytes) * 8.0 / residualActiveDuration / 1e6;

        std::cout << "[HYBRID-DCN][RESIDUAL] src              = " << residualBulkSrcName
                  << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] dst              = " << residualBulkDstName
                  << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] dstAddress       = " << residualBulkDstAddress
                  << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] rxBytes          = "
                  << g_residualBulkRxBytes << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] completionRatio  = "
                  << residualCompletionRatio << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] completed        = "
                  << (residualCompleted ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] observedFct      = " << residualObservedFct
                  << " s" << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] avgGoodputMbps   = "
                  << residualAvgGoodputMbps << " Mbps" << std::endl;
    }
    else
    {
        std::cout << "[HYBRID-DCN][RESIDUAL] src              = N/A" << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] dst              = N/A" << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] dstAddress       = N/A" << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] rxBytes          = 0" << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] completionRatio  = 0" << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] completed        = false" << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] observedFct      = 0 s" << std::endl;
        std::cout << "[HYBRID-DCN][RESIDUAL] avgGoodputMbps   = 0 Mbps" << std::endl;
    }

    auto isMatrixFlowCompleted =
        [](const MatrixBulkFlowSpec& spec, const MatrixBulkFlowStats& stats) {
            return spec.expectedBytes > 0 && stats.rxBytes >= spec.expectedBytes;
        };
    auto computeMatrixFlowCompletionRatio =
        [](const MatrixBulkFlowSpec& spec, const MatrixBulkFlowStats& stats) {
            if (spec.expectedBytes == 0)
            {
                return 0.0;
            }
            return static_cast<double>(stats.rxBytes) /
                   static_cast<double>(spec.expectedBytes);
        };
    auto computeMatrixFlowRxDurationSeconds = [](const MatrixBulkFlowStats& stats) {
        const double firstRx = stats.seenFirstRx ? stats.firstRxTime : 0.0;
        const double lastRx = stats.seenFirstRx ? stats.lastRxTime : 0.0;
        return lastRx > firstRx ? (lastRx - firstRx) : 0.0;
    };
    auto computeMatrixFlowFctSeconds =
        [&](const MatrixBulkFlowSpec& spec, const MatrixBulkFlowStats& stats) {
            if (!isMatrixFlowCompleted(spec, stats) || !stats.seenFirstRx)
            {
                return 0.0;
            }
            return stats.lastRxTime > spec.startTime ? (stats.lastRxTime - spec.startTime) : 0.0;
        };
    auto classifyMatrixFlowPathType = [&](const MatrixBulkFlowSpec& spec) {
        if (spec.ocsCovered)
        {
            return std::string("ocs");
        }
        if (spec.epsFallback)
        {
            return std::string("eps-fallback");
        }
        if (requiresEpsResidualPath(spec))
        {
            return std::string("eps-residual");
        }
        return std::string("unknown");
    };

    std::cout << "[HYBRID-DCN][MATRIX-FLOW] enabled          = "
              << (enableMatrixFlows ? "true" : "false") << std::endl;
    if (enableMatrixFlows)
    {
        uint32_t coveredFlows = 0;
        uint32_t residualFlows = 0;
        uint32_t completedFlows = 0;
        uint64_t totalMatrixRxBytes = 0;
        for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
        {
            const auto& spec = matrixFlowSpecs[flowIndex];
            const auto& stats = matrixFlowStats[flowIndex];
            if (spec.ocsCovered)
            {
                coveredFlows++;
            }
            else
            {
                residualFlows++;
            }
            if (isMatrixFlowCompleted(spec, stats))
            {
                completedFlows++;
            }
            totalMatrixRxBytes += stats.rxBytes;
        }

        const double matrixCompletionRatio =
            matrixFlowSpecs.empty()
                ? 0.0
                : static_cast<double>(completedFlows) / static_cast<double>(matrixFlowSpecs.size());
        const double matrixAvgGoodputMbps =
            static_cast<double>(totalMatrixRxBytes) * 8.0 / (simTime - matrixFlowStart) / 1e6;

        std::cout << "[HYBRID-DCN][MATRIX-FLOW] flowCount        = "
                  << matrixFlowSpecs.size() << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] coveredFlows     = " << coveredFlows
                  << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] residualFlows    = " << residualFlows
                  << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] completedFlows   = " << completedFlows
                  << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] completionRatio  = "
                  << matrixCompletionRatio << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] totalRxBytes     = "
                  << totalMatrixRxBytes << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] avgGoodputMbps   = "
                  << matrixAvgGoodputMbps << " Mbps" << std::endl;

        for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
        {
            const auto& spec = matrixFlowSpecs[flowIndex];
            const auto& stats = matrixFlowStats[flowIndex];
            const bool completed = isMatrixFlowCompleted(spec, stats);
            std::cout << "[HYBRID-DCN][MATRIX-FLOW] flow[" << flowIndex
                      << "] name=" << spec.name
                      << " src=" << makeServerName(spec.srcLeaf, spec.srcServer)
                      << " dst=" << makeServerName(spec.dstLeaf, spec.dstServer)
                      << " ocsCovered=" << (spec.ocsCovered ? "true" : "false")
                      << " rxBytes=" << stats.rxBytes
                      << " completed=" << (completed ? "true" : "false") << std::endl;
        }
    }
    else
    {
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] flowCount        = 0" << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] coveredFlows     = 0" << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] residualFlows    = 0" << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] completedFlows   = 0" << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] completionRatio  = 0" << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] totalRxBytes     = 0" << std::endl;
        std::cout << "[HYBRID-DCN][MATRIX-FLOW] avgGoodputMbps   = 0 Mbps" << std::endl;
    }

    std::cout << "[HYBRID-DCN][FLOW-TRACE] enabled = "
              << (enableDetailedFlowTrace ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][FLOW-TRACE] detailedFlowLogLimit = "
              << detailedFlowLogLimit << std::endl;
    if (enableDetailedFlowTrace && enableMatrixFlows)
    {
        const uint32_t flowTraceCount =
            static_cast<uint32_t>(std::min<std::size_t>(matrixFlowSpecs.size(),
                                                        detailedFlowLogLimit));
        for (uint32_t flowIndex = 0; flowIndex < flowTraceCount; ++flowIndex)
        {
            const auto& spec = matrixFlowSpecs[flowIndex];
            const auto& stats = matrixFlowStats[flowIndex];
            const bool completed = isMatrixFlowCompleted(spec, stats);
            const double completionRatio = computeMatrixFlowCompletionRatio(spec, stats);
            const double firstRx = stats.seenFirstRx ? stats.firstRxTime : 0.0;
            const double lastRx = stats.seenFirstRx ? stats.lastRxTime : 0.0;
            const double duration = lastRx > firstRx ? (lastRx - firstRx) : 0.0;
            const double goodputMbps =
                duration > 0 ? static_cast<double>(stats.rxBytes) * 8.0 / duration / 1e6 : 0.0;

            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] name = " << spec.name << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] srcLeaf = " << spec.srcLeaf << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] dstLeaf = " << spec.dstLeaf << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] rawDemand = " << rawTrafficMatrix[spec.srcLeaf][spec.dstLeaf]
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] controlDemand = "
                      << controlTrafficMatrix[spec.srcLeaf][spec.dstLeaf] << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] ocsPairInstalled = "
                      << (isOcsPairInstalled(spec.srcLeaf, spec.dstLeaf) ? "true" : "false")
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] admissionMode = "
                      << (enableOcsAdmissionControl ? "controlled" : "disabled-direct-ocs")
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] ocsAdmitted = " << (spec.ocsAdmitted ? "true" : "false")
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] ocsCovered = " << (spec.ocsCovered ? "true" : "false")
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] fallbackToEps = " << (spec.epsFallback ? "true" : "false")
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] fallbackDataPlaneMode = " << spec.fallbackDataPlaneMode
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] fallbackEventMapped = "
                      << (spec.fallbackEventMapped ? "true" : "false") << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] fallbackMappingType = " << spec.fallbackMappingType
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] plannedResidualDemand = " << spec.plannedResidualDemand
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] realResidualDemand = " << spec.realResidualDemand << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] wecmpResidualDemand = " << spec.wecmpResidualDemand << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] requiresEpsResidualPath = "
                      << (requiresEpsResidualPath(spec) ? "true" : "false") << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] residualPathReason = " << spec.residualPathReason << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] epsPathFrozen = " << (spec.epsPathFrozen ? "true" : "false")
                      << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] frozenSpine = " << spec.frozenSpine << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] packetSinkPort = " << spec.port << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] bulkStart = " << spec.startTime << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] maxBytes = " << matrixFlowMaxBytes << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] expectedBytes = " << spec.expectedBytes << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] rxBytes = " << stats.rxBytes << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] completed = " << (completed ? "true" : "false") << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] completionRatio = " << completionRatio << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] firstRx = " << firstRx << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] lastRx = " << lastRx << std::endl;
            std::cout << "[HYBRID-DCN][FLOW-TRACE] flow[" << flowIndex
                      << "] goodputMbps = " << goodputMbps << std::endl;
        }
    }

    std::cout << "[HYBRID-DCN][OCS] enabled     = " << (enableStaticOcs ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][OCS] txPackets   = " << g_ocsTxPackets << std::endl;
    std::cout << "[HYBRID-DCN][OCS] txBytes     = " << g_ocsTxBytes << std::endl;
    std::cout << "[HYBRID-DCN][OCS] observedUse = "
              << (g_ocsTxPackets > 0 ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][EPS] txPackets   = " << g_epsTxPackets << std::endl;
    std::cout << "[HYBRID-DCN][EPS] txBytes     = " << g_epsTxBytes << std::endl;
    std::cout << "[HYBRID-DCN][EPS] observedUse = "
              << (g_epsTxPackets > 0 ? "true" : "false") << std::endl;

    uint32_t resultCoveredFlows = 0;
    uint32_t resultResidualFlows = 0;
    uint32_t resultCompletedFlows = 0;
    uint64_t resultTotalMatrixRxBytes = 0;
    uint64_t resultCoveredMatrixRxBytes = 0;
    uint64_t resultResidualMatrixRxBytes = 0;
    std::vector<double> resultCompletedFctSeconds;
    resultCompletedFctSeconds.reserve(matrixFlowSpecs.size());
    for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
    {
        const auto& spec = matrixFlowSpecs[flowIndex];
        const auto& stats = matrixFlowStats[flowIndex];
        if (spec.ocsCovered)
        {
            resultCoveredFlows++;
            resultCoveredMatrixRxBytes += stats.rxBytes;
        }
        else
        {
            resultResidualFlows++;
            resultResidualMatrixRxBytes += stats.rxBytes;
        }

        if (isMatrixFlowCompleted(spec, stats))
        {
            resultCompletedFlows++;
            resultCompletedFctSeconds.push_back(computeMatrixFlowFctSeconds(spec, stats));
        }
        resultTotalMatrixRxBytes += stats.rxBytes;
    }

    double resultAvgFctSeconds = 0.0;
    for (const auto fctSeconds : resultCompletedFctSeconds)
    {
        resultAvgFctSeconds += fctSeconds;
    }
    if (!resultCompletedFctSeconds.empty())
    {
        resultAvgFctSeconds /= static_cast<double>(resultCompletedFctSeconds.size());
    }

    const double resultP99FctSeconds = ComputeNearestRankP99(resultCompletedFctSeconds);

    const double resultEffectiveDuration =
        simTime > matrixFlowStart ? (simTime - matrixFlowStart) : 0.0;
    const double resultMatrixAggregateGoodputMbps =
        resultEffectiveDuration > 0
            ? static_cast<double>(resultTotalMatrixRxBytes) * 8.0 / resultEffectiveDuration / 1e6
            : 0.0;
    const bool resultOcsObservedUse = g_ocsTxPackets > 0;
    const bool resultEpsObservedUse = g_epsTxPackets > 0;
    const double resultOcsByteShare =
        (g_ocsTxBytes + g_epsTxBytes) > 0
            ? static_cast<double>(g_ocsTxBytes) /
                  static_cast<double>(g_ocsTxBytes + g_epsTxBytes)
            : 0.0;
    const double resultOcsHitRatio =
        matrixFlowSpecs.empty()
            ? 0.0
            : static_cast<double>(resultCoveredFlows) /
                  static_cast<double>(matrixFlowSpecs.size());
    const double resultEpsFallbackRatio =
        matrixFlowSpecs.empty()
            ? 0.0
            : static_cast<double>(admissionFallbackFlows) /
                  static_cast<double>(matrixFlowSpecs.size());
    const double resultResidualFlowRatio =
        matrixFlowSpecs.empty()
            ? 0.0
            : static_cast<double>(resultResidualFlows) /
                  static_cast<double>(matrixFlowSpecs.size());
    auto computeLinkUtilizationApprox = [&](const LinkCounter& counter) {
        if (simTime <= 0 || counter.capacityGbps <= 0)
        {
            return 0.0;
        }
        return static_cast<double>(counter.txBytes) * 8.0 /
               (simTime * counter.capacityGbps * 1e9);
    };
    uint32_t resultOcsLinkCounterCount = 0;
    uint32_t resultEpsLinkCounterCount = 0;
    double resultMaxOcsLinkUtilizationApprox = 0.0;
    double resultMaxEpsLinkUtilizationApprox = 0.0;
    double resultSumEpsLinkUtilizationApprox = 0.0;
    std::vector<double> resultEpsLinkUtilizationApproxSamples;
    for (const auto& counter : linkCounters)
    {
        const double utilizationApprox = computeLinkUtilizationApprox(counter);
        if (counter.linkType == "ocs")
        {
            resultOcsLinkCounterCount++;
            resultMaxOcsLinkUtilizationApprox =
                std::max(resultMaxOcsLinkUtilizationApprox, utilizationApprox);
        }
        else if (counter.linkType == "eps-leaf-spine")
        {
            resultEpsLinkCounterCount++;
            resultMaxEpsLinkUtilizationApprox =
                std::max(resultMaxEpsLinkUtilizationApprox, utilizationApprox);
            resultSumEpsLinkUtilizationApprox += utilizationApprox;
            resultEpsLinkUtilizationApproxSamples.push_back(utilizationApprox);
        }
    }
    const double resultAvgEpsLinkUtilizationApprox =
        resultEpsLinkCounterCount > 0
            ? resultSumEpsLinkUtilizationApprox /
                  static_cast<double>(resultEpsLinkCounterCount)
            : 0.0;
    double resultEpsLinkUtilizationVarianceApprox = 0.0;
    for (const auto utilizationApprox : resultEpsLinkUtilizationApproxSamples)
    {
        const double delta = utilizationApprox - resultAvgEpsLinkUtilizationApprox;
        resultEpsLinkUtilizationVarianceApprox += delta * delta;
    }
    if (!resultEpsLinkUtilizationApproxSamples.empty())
    {
        resultEpsLinkUtilizationVarianceApprox /=
            static_cast<double>(resultEpsLinkUtilizationApproxSamples.size());
    }
    const double resultEpsLinkUtilizationStddevApprox =
        std::sqrt(resultEpsLinkUtilizationVarianceApprox);
    uint64_t linkTimeseriesNonzeroSampleRows = 0;
    double linkTimeseriesMaxUtilizationApprox = 0.0;
    double linkTimeseriesSumUtilizationApprox = 0.0;
    double linkTimeseriesMaxEpsUtilizationApprox = 0.0;
    double linkTimeseriesMaxOcsUtilizationApprox = 0.0;
    for (const auto& sample : linkUtilizationSamples)
    {
        linkTimeseriesSumUtilizationApprox += sample.utilizationApprox;
        linkTimeseriesMaxUtilizationApprox =
            std::max(linkTimeseriesMaxUtilizationApprox, sample.utilizationApprox);
        if (sample.deltaTxBytes > 0)
        {
            linkTimeseriesNonzeroSampleRows++;
        }
        if (sample.linkIndex < linkCounters.size())
        {
            const auto& counter = linkCounters[sample.linkIndex];
            if (counter.linkType == "eps-leaf-spine")
            {
                linkTimeseriesMaxEpsUtilizationApprox =
                    std::max(linkTimeseriesMaxEpsUtilizationApprox,
                             sample.utilizationApprox);
            }
            else if (counter.linkType == "ocs")
            {
                linkTimeseriesMaxOcsUtilizationApprox =
                    std::max(linkTimeseriesMaxOcsUtilizationApprox,
                             sample.utilizationApprox);
            }
        }
    }
    const double linkTimeseriesAvgUtilizationApprox =
        !linkUtilizationSamples.empty()
            ? linkTimeseriesSumUtilizationApprox /
                  static_cast<double>(linkUtilizationSamples.size())
            : 0.0;
    uint64_t measuredWecmpSnapshotCount = 0;
    auto countMeasuredSnapshotEntries = [](const auto& snapshots) {
        uint64_t count = 0;
        for (const auto& leafSnapshots : snapshots)
        {
            for (const auto& snapshot : leafSnapshots)
            {
                if (snapshot.hasSample)
                {
                    count++;
                }
            }
        }
        return count;
    };
    measuredWecmpSnapshotCount +=
        countMeasuredSnapshotEntries(epsMeasuredLeafToSpineSnapshots);
    measuredWecmpSnapshotCount +=
        countMeasuredSnapshotEntries(epsMeasuredSpineToLeafSnapshots);
    uint64_t measuredWecmpDecisionCount = 0;
    uint64_t measuredWecmpFallbackDecisionCount = 0;
    uint64_t measuredWecmpNoSampleDecisionCount = 0;
    uint64_t measuredWecmpCandidateRowsWithSample = 0;
    uint64_t measuredWecmpCandidateRowsWithoutSample = 0;
    uint64_t measuredWecmpChangedSelectedSpineCount = 0;
    for (const auto& decision : epsWecmpDecisions)
    {
        if (!decision.measuredDecisionRequested)
        {
            continue;
        }
        measuredWecmpDecisionCount++;
        if (decision.measuredDecisionFallback)
        {
            measuredWecmpFallbackDecisionCount++;
        }
        if (decision.measuredNoSample)
        {
            measuredWecmpNoSampleDecisionCount++;
        }
        if (decision.selectedSpine != decision.controlPlaneSelectedSpine)
        {
            measuredWecmpChangedSelectedSpineCount++;
        }
        for (const auto& state : decision.linkStates)
        {
            if (state.hasMeasuredSample)
            {
                measuredWecmpCandidateRowsWithSample++;
            }
            else
            {
                measuredWecmpCandidateRowsWithoutSample++;
            }
        }
    }
    uint64_t measuredWecmpLaterFlowCount = 0;
    uint64_t measuredWecmpLaterFlowCompletedCount = 0;
    uint64_t measuredWecmpLaterDecisionCount = 0;
    uint64_t measuredWecmpLaterChangedSelectedSpineCount = 0;
    bool measuredWecmpLaterRouteInstalled = false;
    bool measuredWecmpLaterOcsLeakageDetected = false;
    bool measuredWecmpLaterProofPassed = false;
    for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
    {
        const auto& spec = matrixFlowSpecs[flowIndex];
        if (!spec.isMeasuredLaterProofFlow)
        {
            continue;
        }
        measuredWecmpLaterFlowCount++;
        if (flowIndex < matrixFlowStats.size() &&
            isMatrixFlowCompleted(spec, matrixFlowStats[flowIndex]))
        {
            measuredWecmpLaterFlowCompletedCount++;
        }
        if (spec.measuredLaterDecisionIndex >= 0)
        {
            measuredWecmpLaterDecisionCount++;
        }
        if (spec.measuredLaterSelectedSpineChanged)
        {
            measuredWecmpLaterChangedSelectedSpineCount++;
        }
        if (spec.measuredLaterRouteInstalled)
        {
            measuredWecmpLaterRouteInstalled = true;
        }
        for (const auto& counter : linkCounters)
        {
            const bool ocsPairMatch =
                counter.linkType == "ocs" &&
                ((counter.endpointA == spec.srcLeaf && counter.endpointB == spec.dstLeaf) ||
                 (counter.endpointA == spec.dstLeaf && counter.endpointB == spec.srcLeaf));
            if (ocsPairMatch && counter.txBytes > 0)
            {
                measuredWecmpLaterOcsLeakageDetected = true;
            }
        }
    }
    measuredWecmpLaterProofPassed =
        enableMeasuredWecmpLaterFlowProof && measuredWecmpLaterFlowCount > 0 &&
        measuredWecmpLaterFlowCompletedCount == measuredWecmpLaterFlowCount &&
        measuredWecmpLaterDecisionCount == measuredWecmpLaterFlowCount &&
        measuredWecmpLaterChangedSelectedSpineCount > 0 &&
        measuredWecmpLaterRouteInstalled && !measuredWecmpLaterOcsLeakageDetected;
    std::string ocsToEpsByteRatio = "0";
    if (g_epsTxBytes > 0)
    {
        std::ostringstream ratioStream;
        ratioStream << (static_cast<double>(g_ocsTxBytes) / static_cast<double>(g_epsTxBytes));
        ocsToEpsByteRatio = ratioStream.str();
    }
    else if (g_ocsTxBytes > 0)
    {
        ocsToEpsByteRatio = "inf";
    }

    auto isFinalEdgeInstalled = [&installedOcsLinks](uint32_t leafA, uint32_t leafB) {
        for (const auto& link : installedOcsLinks)
        {
            if ((link.leafA == leafA && link.leafB == leafB) ||
                (link.leafA == leafB && link.leafB == leafA))
            {
                return true;
            }
        }
        return false;
    };

    const bool admissionFallbackOnlyDataPlane =
        enableOcsAdmissionControl && admissionFallbackFlows > 0 && admissionAdmittedFlows == 0 &&
        resultResidualFlows > 0;
    const bool hasCoveredAndResidualFlows =
        resultCoveredFlows > 0 && resultResidualFlows > 0;
    const bool allMatrixFlowsCompleted =
        !matrixFlowSpecs.empty() && resultCompletedFlows == matrixFlowSpecs.size();
    const bool ocsAndEpsBothObserved = resultOcsObservedUse && resultEpsObservedUse;
    const bool dataPlanePathObservationPass =
        ocsAndEpsBothObserved || (admissionFallbackOnlyDataPlane && resultEpsObservedUse);
    const bool dataPlaneValidationPass =
        allMatrixFlowsCompleted && dataPlanePathObservationPass;
    uint32_t resultWecmpFrozenFlows = 0;
    for (const auto& spec : matrixFlowSpecs)
    {
        if (spec.epsPathFrozen)
        {
            resultWecmpFrozenFlows++;
        }
    }

    std::cout << "[HYBRID-DCN][PATH] ocsCoveredFlowCount = "
              << resultCoveredFlows << std::endl;
    std::cout << "[HYBRID-DCN][PATH] epsResidualFlowCount = "
              << resultResidualFlows << std::endl;
    std::cout << "[HYBRID-DCN][PATH] fallbackFlowCount = "
              << admissionFallbackFlows << std::endl;
    std::cout << "[HYBRID-DCN][PATH] wecmpFrozenFlowCount = "
              << resultWecmpFrozenFlows << std::endl;
    std::cout << "[HYBRID-DCN][PATH] ocsTxBytes = " << g_ocsTxBytes << std::endl;
    std::cout << "[HYBRID-DCN][PATH] epsTxBytes = " << g_epsTxBytes << std::endl;
    std::cout << "[HYBRID-DCN][PATH] ocsObservedUse = "
              << (resultOcsObservedUse ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PATH] epsObservedUse = "
              << (resultEpsObservedUse ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PATH] hasCoveredAndResidualFlows = "
              << (hasCoveredAndResidualFlows ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][PATH] ocsToEpsByteRatio = "
              << ocsToEpsByteRatio << std::endl;

    std::cout << "[HYBRID-DCN][RESULT] resultStage = stage-23-result-summary"
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] experimentName = " << experimentName << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] presetScenario = " << presetScenario << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] presetOverrideMode = " << presetOverrideMode
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] experimentGroup = " << experimentGroup
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ablationLevel = " << ablationLevel
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] isMainMethod = "
              << (isMainMethod ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] isFullStackMethod = "
              << (isFullStackMethod ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] isFullStackPreset = "
              << (isFullStackPreset ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] isBaseline = "
              << (isBaseline ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] enabledModuleSummary = "
              << enabledModuleSummary << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] trafficMatrixSource = "
              << trafficMatrixSource << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] enableEwmaSmoothing = "
              << (enableEwmaSmoothing ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ewmaEnabled = "
              << (enableEwmaSmoothing ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ewmaBeta = " << ewmaBeta << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] trafficGraphThreshold = "
              << trafficGraphThreshold << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ocsAdmissionControlEnabled = "
              << (enableOcsAdmissionControl ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] epsWecmpEnabled = "
              << (enableEpsWecmp ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] epsWecmpRoutingEnabled = "
              << (enableEpsWecmpRouting ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] multiPeriodControlEnabled = "
              << (enableMultiPeriodControl ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] fullStackControlEnabled = "
              << (fullStackControlEnabled ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] trafficMatrixMode = " << trafficMatrixMode
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] communityMode = " << communityMode << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] selectionMetric = " << selectionMetric
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] configGateDecision = "
              << configGateDecision << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] holdTimeActive = "
              << (holdTimeActive ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] finalSelectedOcsEdges = "
              << selectedOcsEdges.size() << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] installedOcsLinks = "
              << installedOcsLinks.size() << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] finalConfigInstalled = "
              << (installedOcsLinks.size() == selectedOcsEdges.size() ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] matrixFlowCount = "
              << matrixFlowSpecs.size() << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] coveredFlows = " << resultCoveredFlows
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] residualFlows = " << resultResidualFlows
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] completedFlows = " << resultCompletedFlows
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] completedFlowCount = " << resultCompletedFlows
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] matrixTotalRxBytes = "
              << resultTotalMatrixRxBytes << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] totalRxBytes = "
              << resultTotalMatrixRxBytes << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] matrixCoveredRxBytes = "
              << resultCoveredMatrixRxBytes << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] matrixResidualRxBytes = "
              << resultResidualMatrixRxBytes << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] matrixAggregateGoodputMbps = "
              << resultMatrixAggregateGoodputMbps << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] aggregateGoodputMbps = "
              << resultMatrixAggregateGoodputMbps << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] avgFctSeconds = "
              << resultAvgFctSeconds << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] p99FctSeconds = "
              << resultP99FctSeconds << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ocsByteShare = "
              << resultOcsByteShare << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ocsHitRatio = "
              << resultOcsHitRatio << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] epsFallbackRatio = "
              << resultEpsFallbackRatio << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] residualFlowRatio = "
              << resultResidualFlowRatio << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] linkCounterCount = "
              << linkCounters.size() << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ocsLinkCounterCount = "
              << resultOcsLinkCounterCount << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] epsLinkCounterCount = "
              << resultEpsLinkCounterCount << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] maxOcsLinkUtilizationApprox = "
              << resultMaxOcsLinkUtilizationApprox << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] maxEpsLinkUtilizationApprox = "
              << resultMaxEpsLinkUtilizationApprox << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] avgEpsLinkUtilizationApprox = "
              << resultAvgEpsLinkUtilizationApprox << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] epsLinkUtilizationStddevApprox = "
              << resultEpsLinkUtilizationStddevApprox << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] linkUtilizationSampleIntervalSeconds = "
              << linkUtilizationSampleInterval << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] linkTimeseriesEnabled = "
              << (linkTimeseriesEnabled ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] linkTimeseriesSampleRows = "
              << linkUtilizationSamples.size() << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] linkTimeseriesNonzeroSampleRows = "
              << linkTimeseriesNonzeroSampleRows << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] linkTimeseriesMaxUtilizationApprox = "
              << linkTimeseriesMaxUtilizationApprox << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] linkTimeseriesAvgUtilizationApprox = "
              << linkTimeseriesAvgUtilizationApprox << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] linkTimeseriesMaxEpsUtilizationApprox = "
              << linkTimeseriesMaxEpsUtilizationApprox << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] linkTimeseriesMaxOcsUtilizationApprox = "
              << linkTimeseriesMaxOcsUtilizationApprox << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] epsWecmpLoadSource = "
              << epsWecmpLoadSource << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpEnabled = "
              << (epsWecmpLoadSource == "measured-snapshot" ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpNoSampleFallback = "
              << measuredWecmpNoSampleFallback << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpWarmupTime = "
              << measuredWecmpWarmupTime << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpSnapshotCount = "
              << measuredWecmpSnapshotCount << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpSnapshotUpdateCount = "
              << measuredWecmpSnapshotUpdateCount << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpDecisionCount = "
              << measuredWecmpDecisionCount << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpFallbackDecisionCount = "
              << measuredWecmpFallbackDecisionCount << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpNoSampleDecisionCount = "
              << measuredWecmpNoSampleDecisionCount << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpCandidateRowsWithSample = "
              << measuredWecmpCandidateRowsWithSample << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpCandidateRowsWithoutSample = "
              << measuredWecmpCandidateRowsWithoutSample << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] measuredWecmpChangedSelectedSpineCount = "
              << measuredWecmpChangedSelectedSpineCount << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] matrixFlowRxBytesTolerance = "
              << matrixFlowRxBytesTolerance << std::endl;

    for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
    {
        const auto& spec = matrixFlowSpecs[flowIndex];
        const auto& stats = matrixFlowStats[flowIndex];
        const bool completed = isMatrixFlowCompleted(spec, stats);
        const double completionRatio = computeMatrixFlowCompletionRatio(spec, stats);
        const double firstRx = stats.seenFirstRx ? stats.firstRxTime : 0.0;
        const double lastRx = stats.seenFirstRx ? stats.lastRxTime : 0.0;
        const double duration = computeMatrixFlowRxDurationSeconds(stats);
        const double fctSeconds = computeMatrixFlowFctSeconds(spec, stats);
        const double goodputMbps =
            duration > 0 ? static_cast<double>(stats.rxBytes) * 8.0 / duration / 1e6 : 0.0;
        std::cout << "[HYBRID-DCN][RESULT] matrixFlow[" << flowIndex
                  << "] name=" << spec.name
                  << " pair=" << spec.srcLeaf << "-" << spec.dstLeaf
                  << " pathType=" << classifyMatrixFlowPathType(spec)
                  << " ocsCovered=" << (spec.ocsCovered ? "true" : "false")
                  << " expectedBytes=" << spec.expectedBytes
                  << " rxBytes=" << stats.rxBytes
                  << " completed=" << (completed ? "true" : "false")
                  << " completionRatio=" << completionRatio
                  << " firstRx=" << firstRx
                  << " lastRx=" << lastRx
                  << " fctSeconds=" << fctSeconds
                  << " rxDurationSeconds=" << duration
                  << " duration=" << duration
                  << " goodputMbps=" << goodputMbps << std::endl;
    }

    std::cout << "[HYBRID-DCN][RESULT] ocsTxPackets = " << g_ocsTxPackets
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ocsTxBytes = " << g_ocsTxBytes << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] epsTxPackets = " << g_epsTxPackets
              << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] epsTxBytes = " << g_epsTxBytes << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ocsObservedUse = "
              << (resultOcsObservedUse ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] epsObservedUse = "
              << (resultEpsObservedUse ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ocsToEpsByteRatio = "
              << ocsToEpsByteRatio << std::endl;

    for (uint32_t edgeIndex = 0; edgeIndex < selectedOcsEdges.size(); ++edgeIndex)
    {
        const auto& edge = selectedOcsEdges[edgeIndex];
        std::cout << "[HYBRID-DCN][RESULT] finalEdge[" << edgeIndex
                  << "] = " << edge.leafA << "-" << edge.leafB
                  << " score=" << edge.selectionScore
                  << " utility=" << edge.utility
                  << " stateHoldingGain=" << edge.stateHoldingGain
                  << " installed="
                  << (isFinalEdgeInstalled(edge.leafA, edge.leafB) ? "true" : "false")
                  << std::endl;
    }

    std::cout << "[HYBRID-DCN][RESULT] hasCoveredAndResidualFlows = "
              << (hasCoveredAndResidualFlows ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] allMatrixFlowsCompleted = "
              << (allMatrixFlowsCompleted ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] ocsAndEpsBothObserved = "
              << (ocsAndEpsBothObserved ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] dataPlaneValidationPass = "
              << (dataPlaneValidationPass ? "true" : "false") << std::endl;

    std::string overallResultConsistencyStatus =
        (enableResultValidation && validationMode == "warn") ? "not-run" : "skipped";
    std::cout << "[HYBRID-DCN][VALIDATION] enabled = "
              << (enableResultValidation ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][VALIDATION] validationMode = " << validationMode
              << std::endl;
    if (enableResultValidation && validationMode == "warn")
    {
        auto passFail = [](bool passed) {
            return passed ? "pass" : "fail";
        };

        const bool finalConfigInstalledCheck =
            installedOcsLinks.size() == selectedOcsEdges.size();
        const bool matrixCompletionCheck =
            enableMatrixFlows && !matrixFlowSpecs.empty() &&
            resultCompletedFlows == matrixFlowSpecs.size();
        const bool coveredResidualMixCheck =
            enableMatrixFlows &&
            ((resultCoveredFlows > 0 && resultResidualFlows > 0) ||
             admissionFallbackOnlyDataPlane);
        const bool ocsEpsObservationCheck = dataPlanePathObservationPass;
        const bool dataPlaneValidationCheck = dataPlaneValidationPass;
        const bool presetExpectationAdjustedByExplicitArgs =
            presetOverrideMode == "explicit-wins" && !explicitControlArgNames.empty();

        bool presetExpectationKnown = false;
        bool presetExpectationPass = true;
        std::string presetExpectation = "none";
        std::string presetExpectationCheck = "skipped";
        std::string presetExpectationSkipReason = "unknown-preset";

        if (presetScenario == "manual")
        {
            presetExpectation = "manual-no-fixed-expectation";
            presetExpectationSkipReason = "manual";
        }
        else if (presetExpectationAdjustedByExplicitArgs)
        {
            presetExpectation = presetScenario;
            presetExpectationCheck = "skipped-explicit-overrides";
            presetExpectationSkipReason = "explicit-control-overrides";
        }
        else if (presetScenario == "baseline-excess")
        {
            presetExpectationKnown = true;
            presetExpectation = "baseline-excess";
            presetExpectationSkipReason = "none";
            presetExpectationPass = selectionMetric == "excess" &&
                                    configGateDecision == "disabled" &&
                                    selectedOcsEdges.size() == 2 &&
                                    installedOcsLinks.size() == 2 &&
                                    resultCompletedFlows == matrixFlowSpecs.size();
        }
        else if (presetScenario == "community-aware")
        {
            presetExpectationKnown = true;
            presetExpectation = "community-aware";
            presetExpectationSkipReason = "none";
            presetExpectationPass = selectionMetric == "community-excess" &&
                                    communityMode == "louvain" &&
                                    !enableStateHolding &&
                                    selectedOcsEdges.size() == 2 &&
                                    dataPlaneValidationPass;
        }
        else if (presetScenario == "state-aware")
        {
            presetExpectationKnown = true;
            presetExpectation = "state-aware";
            presetExpectationSkipReason = "none";
            presetExpectationPass = enableStateHolding &&
                                    !enableConfigUpdateGate &&
                                    configGateDecision == "disabled" &&
                                    selectedOcsEdges.size() == 2 &&
                                    dataPlaneValidationPass;
        }
        else if (presetScenario == "config-gated")
        {
            presetExpectationKnown = true;
            presetExpectation = "config-gated";
            presetExpectationSkipReason = "none";
            presetExpectationPass = enableConfigUpdateGate &&
                                    configGateDecision == "use-candidate" &&
                                    selectedOcsEdges.size() == 2 &&
                                    dataPlaneValidationPass;
        }
        else if (presetScenario == "hold-gated")
        {
            presetExpectationKnown = true;
            presetExpectation = "hold-gated";
            presetExpectationSkipReason = "none";
            presetExpectationPass = enableHoldTimeGate &&
                                    holdTimeActive &&
                                    !holdOcsEdges.empty() &&
                                    selectedOcsEdges.size() >= holdOcsEdges.size() &&
                                    dataPlaneValidationPass;
        }
        else if (presetScenario == "full-control")
        {
            presetExpectationKnown = true;
            presetExpectation = "full-control";
            presetExpectationSkipReason = "none";
            presetExpectationPass = enableHoldTimeGate &&
                                    previousConfigAge >= minHoldCycles &&
                                    !holdTimeActive &&
                                    configGateDecision == "use-candidate" &&
                                    selectedOcsEdges.size() == 2 &&
                                    dataPlaneValidationPass;
        }
        else if (presetScenario == "full-stack-control")
        {
            presetExpectationKnown = true;
            presetExpectation = "full-stack-control";
            presetExpectationSkipReason = "none";
            presetExpectationPass = communityMode == "louvain" &&
                                    selectionMetric == "community-excess" &&
                                    enableStateHolding &&
                                    enableConfigUpdateGate &&
                                    enableHoldTimeGate &&
                                    enableOcsAdmissionControl &&
                                    enableEpsWecmp &&
                                    enableEpsWecmpRouting &&
                                    enableMultiPeriodControl &&
                                    !selectedOcsEdges.empty() &&
                                    dataPlaneValidationPass;
        }

        if (presetExpectationKnown)
        {
            presetExpectationCheck = passFail(presetExpectationPass);
        }

        const bool includePresetExpectation =
            presetExpectationCheck != "skipped" &&
            presetExpectationCheck != "skipped-explicit-overrides";
        const bool overallResultConsistency =
            finalConfigInstalledCheck &&
            matrixCompletionCheck &&
            coveredResidualMixCheck &&
            ocsEpsObservationCheck &&
            dataPlaneValidationCheck &&
            (!includePresetExpectation || presetExpectationPass);
        const bool mainMethodConsistencyPass =
            presetScenario == "full-control" &&
            selectionMetric == "community-excess" &&
            communityMode == "louvain" &&
            enableStateHolding &&
            enableConfigUpdateGate &&
            enableHoldTimeGate &&
            selectedOcsEdges.size() == 2 &&
            dataPlaneValidationPass;
        const bool baselineConsistencyPass =
            presetScenario == "baseline-excess" &&
            selectionMetric == "excess" &&
            !enableStateHolding &&
            !enableConfigUpdateGate &&
            !enableHoldTimeGate &&
            selectedOcsEdges.size() == 2 &&
            dataPlaneValidationPass;
        const std::string mainMethodConsistency =
            isMainMethod ? passFail(mainMethodConsistencyPass) : "skipped";
        const std::string baselineConsistency =
            isBaseline ? passFail(baselineConsistencyPass) : "skipped";

        std::cout << "[HYBRID-DCN][VALIDATION] validationStage = stage-24-result-consistency"
                  << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] experimentGroup = "
                  << experimentGroup << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] ablationLevel = "
                  << ablationLevel << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] finalConfigInstalledCheck = "
                  << passFail(finalConfigInstalledCheck) << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] matrixCompletionCheck = "
                  << passFail(matrixCompletionCheck) << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] coveredResidualMixCheck = "
                  << passFail(coveredResidualMixCheck) << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] ocsEpsObservationCheck = "
                  << passFail(ocsEpsObservationCheck) << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] dataPlaneValidationCheck = "
                  << passFail(dataPlaneValidationCheck) << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] presetExpectation = "
                  << presetExpectation << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] explicitControlArgCount = "
                  << explicitControlArgNames.size() << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] presetExpectationAdjustedByExplicitArgs = "
                  << (presetExpectationAdjustedByExplicitArgs ? "true" : "false")
                  << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] presetExpectationSkipReason = "
                  << presetExpectationSkipReason << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] presetExpectationCheck = "
                  << presetExpectationCheck << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] mainMethodConsistency = "
                  << mainMethodConsistency << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] baselineConsistency = "
                  << baselineConsistency << std::endl;
        std::cout << "[HYBRID-DCN][VALIDATION] overallResultConsistency = "
                  << passFail(overallResultConsistency) << std::endl;
        overallResultConsistencyStatus = passFail(overallResultConsistency);
    }

    const StructuredResultCsvPaths structuredResultCsvPaths =
        BuildStructuredResultCsvPaths(structuredResultDir, experimentName);
    const std::string& summaryCsvPath = structuredResultCsvPaths.summaryCsvPath;
    const std::string& flowsCsvPath = structuredResultCsvPaths.flowsCsvPath;
    const std::string& wecmpCsvPath = structuredResultCsvPaths.wecmpCsvPath;
    const std::string& ocsCandidatesCsvPath =
        structuredResultCsvPaths.ocsCandidatesCsvPath;
    const std::string& linksCsvPath = structuredResultCsvPaths.linksCsvPath;
    const std::string& linkTimeseriesCsvPath =
        structuredResultCsvPaths.linkTimeseriesCsvPath;
    const std::string& measuredWecmpCsvPath =
        structuredResultCsvPaths.measuredWecmpCsvPath;
    bool structuredExportCheck = !enableStructuredResultExport;
    bool structuredExportEvaluated = false;

    auto runStructuredResultExport = [&](const std::string& overallAlgorithmInvariantStatus) {
        if (structuredExportEvaluated)
        {
            return structuredExportCheck;
        }
        structuredExportEvaluated = true;

        std::cout << "[HYBRID-DCN][EXPORT] enabled = "
                  << (enableStructuredResultExport ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][EXPORT] summaryCsv = " << summaryCsvPath
                  << std::endl;
        std::cout << "[HYBRID-DCN][EXPORT] flowsCsv = " << flowsCsvPath << std::endl;
        std::cout << "[HYBRID-DCN][EXPORT] wecmpCsv = " << wecmpCsvPath << std::endl;
        std::cout << "[HYBRID-DCN][EXPORT] ocsCandidatesCsv = "
                  << ocsCandidatesCsvPath << std::endl;
        std::cout << "[HYBRID-DCN][EXPORT] linksCsv = " << linksCsvPath << std::endl;
        std::cout << "[HYBRID-DCN][EXPORT] linkTimeseriesCsv = "
                  << linkTimeseriesCsvPath << std::endl;
        std::cout << "[HYBRID-DCN][EXPORT] measuredWecmpCsv = "
                  << measuredWecmpCsvPath << std::endl;

        if (!enableStructuredResultExport)
        {
            std::cout << "[HYBRID-DCN][EXPORT] exportSuccess = false" << std::endl;
            structuredExportCheck = true;
            return structuredExportCheck;
        }

        bool exportSuccess = true;

        {
            std::ofstream file = OpenStructuredResultCsv(summaryCsvPath, exportSuccess);
            if (file.is_open())
            {
                writeCsvRow(file, SummaryCsvHeader());
                writeCsvRow(file,
                            {experimentName,
                             presetScenario,
                             trafficMatrixMode,
                             trafficMatrixSource,
                             csvBool(enableEwmaSmoothing),
                             csvValue(trafficGraphThreshold),
                             configScoreMode,
                             selectionMetric,
                             communityMode,
                             csvValue(activeCommunityCount),
                             csvValue(louvainResult.modularityQ),
                             formatEdgeSet(selectedOcsEdges),
                             csvValue(candidateConfigScore),
                             csvValue(previousConfigScore),
                             csvValue(configScoreImprovement),
                             csvValue(resultCoveredFlows),
                             csvValue(resultResidualFlows),
                             csvValue(admissionFallbackFlows),
                             csvValue(resultWecmpFrozenFlows),
                             csvValue(g_ocsTxBytes),
                             csvValue(g_epsTxBytes),
                             csvBool(resultOcsObservedUse),
                             csvBool(resultEpsObservedUse),
                             csvValue(matrixFlowSpecs.size()),
                             csvValue(resultCompletedFlows),
                             csvValue(matrixFlowSpecs.empty()
                                          ? 0.0
                                          : static_cast<double>(resultCompletedFlows) /
                                                static_cast<double>(matrixFlowSpecs.size())),
                             csvValue(resultMatrixAggregateGoodputMbps),
                             overallResultConsistencyStatus,
                             overallAlgorithmInvariantStatus,
                             csvValue(resultCompletedFlows),
                             csvValue(resultTotalMatrixRxBytes),
                             csvValue(resultMatrixAggregateGoodputMbps),
                             csvValue(resultAvgFctSeconds),
                             csvValue(resultP99FctSeconds),
                             csvValue(resultOcsByteShare),
                             csvValue(resultOcsHitRatio),
                             csvValue(resultEpsFallbackRatio),
                             csvValue(resultResidualFlowRatio),
                             csvValue(linkCounters.size()),
                             csvValue(resultOcsLinkCounterCount),
                             csvValue(resultEpsLinkCounterCount),
                             csvValue(resultMaxOcsLinkUtilizationApprox),
                             csvValue(resultMaxEpsLinkUtilizationApprox),
                             csvValue(resultAvgEpsLinkUtilizationApprox),
                             csvValue(resultEpsLinkUtilizationStddevApprox),
                             csvValue(linkUtilizationSampleInterval),
                             csvBool(linkTimeseriesEnabled),
                             csvValue(linkUtilizationSamples.size()),
                             csvValue(linkTimeseriesNonzeroSampleRows),
                             csvValue(linkTimeseriesMaxUtilizationApprox),
                             csvValue(linkTimeseriesAvgUtilizationApprox),
                             csvValue(linkTimeseriesMaxEpsUtilizationApprox),
                             csvValue(linkTimeseriesMaxOcsUtilizationApprox),
                             epsWecmpLoadSource,
                             csvBool(epsWecmpLoadSource == "measured-snapshot"),
                             measuredWecmpNoSampleFallback,
                             csvValue(measuredWecmpWarmupTime),
                             csvValue(measuredWecmpSnapshotCount),
                             csvValue(measuredWecmpSnapshotUpdateCount),
                             csvValue(measuredWecmpDecisionCount),
                             csvValue(measuredWecmpFallbackDecisionCount),
                             csvValue(measuredWecmpNoSampleDecisionCount),
                             csvValue(measuredWecmpCandidateRowsWithSample),
                             csvValue(measuredWecmpCandidateRowsWithoutSample),
                             csvValue(measuredWecmpChangedSelectedSpineCount),
                             csvBool(enableMeasuredWecmpLaterFlowProof),
                             csvValue(measuredWecmpLaterDecisionTime),
                             csvValue(measuredWecmpLaterFlowStart),
                             csvValue(measuredWecmpLaterFlowCount),
                             csvValue(measuredWecmpLaterFlowCompletedCount),
                             csvValue(measuredWecmpLaterDecisionCount),
                             csvValue(measuredWecmpLaterChangedSelectedSpineCount),
                             csvBool(measuredWecmpLaterRouteInstalled),
                             csvBool(measuredWecmpLaterOcsLeakageDetected),
                             csvBool(measuredWecmpLaterProofPassed)});
                if (!file.good())
                {
                    exportSuccess = false;
                    std::cout << "[HYBRID-DCN][EXPORT] error = failed-to-write:"
                              << summaryCsvPath << std::endl;
                }
            }
        }

        {
            std::ofstream file = OpenStructuredResultCsv(flowsCsvPath, exportSuccess);
            if (file.is_open())
            {
                writeCsvRow(file, FlowsCsvHeader());
                for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
                {
                    const auto& spec = matrixFlowSpecs[flowIndex];
                    const auto& stats = matrixFlowStats[flowIndex];
                    const double firstRx = stats.seenFirstRx ? stats.firstRxTime : 0.0;
                    const double lastRx = stats.seenFirstRx ? stats.lastRxTime : 0.0;
                    const double rxDurationSeconds = computeMatrixFlowRxDurationSeconds(stats);
                    const double fctSeconds = computeMatrixFlowFctSeconds(spec, stats);
                    const double goodputMbps =
                        rxDurationSeconds > 0
                            ? static_cast<double>(stats.rxBytes) * 8.0 / rxDurationSeconds / 1e6
                            : 0.0;
                    writeCsvRow(file,
                                {csvValue(flowIndex),
                                 spec.name,
                                 csvValue(spec.srcLeaf),
                                 csvValue(spec.dstLeaf),
                                 csvValue(rawTrafficMatrix[spec.srcLeaf][spec.dstLeaf]),
                                 csvValue(controlTrafficMatrix[spec.srcLeaf][spec.dstLeaf]),
                                 csvBool(isOcsPairInstalled(spec.srcLeaf, spec.dstLeaf)),
                                 enableOcsAdmissionControl ? "controlled"
                                                           : "disabled-direct-ocs",
                                 csvBool(spec.ocsAdmitted),
                                 csvBool(spec.ocsCovered),
                                 csvBool(spec.epsFallback),
                                 spec.fallbackDataPlaneMode,
                                 csvBool(spec.fallbackEventMapped),
                                 spec.fallbackMappingType,
                                 csvValue(spec.plannedResidualDemand),
                                 csvValue(spec.realResidualDemand),
                                 csvValue(spec.wecmpResidualDemand),
                                 csvBool(requiresEpsResidualPath(spec)),
                                 spec.residualPathReason,
                                 csvBool(spec.epsPathFrozen),
                                 csvValue(spec.frozenSpine),
                                 csvValue(spec.port),
                                 csvValue(spec.startTime),
                                 csvValue(spec.expectedBytes),
                                 csvValue(stats.rxBytes),
                                 csvBool(isMatrixFlowCompleted(spec, stats)),
                                 csvValue(computeMatrixFlowCompletionRatio(spec, stats)),
                                 csvValue(firstRx),
                                 csvValue(lastRx),
                                 csvValue(goodputMbps),
                                 classifyMatrixFlowPathType(spec),
                                 csvValue(fctSeconds),
                                 csvValue(rxDurationSeconds),
                                 csvBool(spec.isMeasuredLaterProofFlow),
                                 csvValue(spec.measuredLaterDecisionTime),
                                 csvValue(spec.measuredLaterDecisionIndex),
                                 csvValue(spec.measuredLaterSelectedSpine),
                                 csvValue(spec.measuredLaterControlPlaneSelectedSpine),
                                 csvBool(spec.measuredLaterSelectedSpineChanged),
                                 csvBool(spec.measuredLaterHasMeasuredSample),
                                 csvBool(spec.measuredLaterAppliesToLaterFlow)});
                }
                if (!file.good())
                {
                    exportSuccess = false;
                    std::cout << "[HYBRID-DCN][EXPORT] error = failed-to-write:"
                              << flowsCsvPath << std::endl;
                }
            }
        }

        {
            std::ofstream file = OpenStructuredResultCsv(wecmpCsvPath, exportSuccess);
            if (file.is_open())
            {
                writeCsvRow(file, WecmpCsvHeader());
                for (uint32_t decisionIndex = 0; decisionIndex < epsWecmpDecisions.size();
                     ++decisionIndex)
                {
                    const auto& decision = epsWecmpDecisions[decisionIndex];
                    for (const auto& state : decision.linkStates)
                    {
                        writeCsvRow(file,
                                    {csvValue(decisionIndex),
                                     csvValue(decision.srcLeaf),
                                     csvValue(decision.dstLeaf),
                                     csvValue(decision.residualDemand),
                                     csvValue(decision.selectedSpine),
                                     csvValue(state.spineIndex),
                                     csvValue(state.pathLoadMetric),
                                     csvValue(state.candidatePathLoad),
                                     csvValue(state.attractiveness),
                                     csvValue(state.normalizedAttractiveness),
                                     csvValue(state.targetProbability),
                                     csvValue(state.previousProbability),
                                     csvValue(state.updatedProbability),
                                     csvValue(state.probabilityDelta),
                                     csvValue(state.boundedProbabilityDelta),
                                     decision.loadSource,
                                     csvBool(state.hasMeasuredSample),
                                     csvValue(state.measuredSrcToSpineUtilization),
                                     csvValue(state.measuredSpineToDstUtilization),
                                     csvValue(state.measuredPathUtilization),
                                     csvValue(state.controlPlanePathLoadMetric),
                                     csvValue(state.effectivePathLoadMetric),
                                     decision.noSampleFallbackMode,
                                     csvBool(decision.measuredDecisionRequested),
                                     csvBool(decision.measuredDecisionUsed),
                                     csvBool(decision.measuredDecisionFallback),
                                     csvBool(decision.measuredNoSample),
                                     csvValue(decision.decisionTimeSeconds),
                                     csvBool(decision.appliesToLaterFlow),
                                     csvValue(decision.controlPlaneSelectedSpine)});
                    }
                }
                if (!file.good())
                {
                    exportSuccess = false;
                    std::cout << "[HYBRID-DCN][EXPORT] error = failed-to-write:"
                              << wecmpCsvPath << std::endl;
                }
            }
        }

        {
            std::ofstream file = OpenStructuredResultCsv(ocsCandidatesCsvPath, exportSuccess);
            if (file.is_open())
            {
                writeCsvRow(file, OcsCandidatesCsvHeader());
                for (uint32_t candidateIndex = 0; candidateIndex < candidateEdges.size();
                     ++candidateIndex)
                {
                    const auto& edge = candidateEdges[candidateIndex];
                    writeCsvRow(file,
                                {csvValue(candidateIndex),
                                 csvValue(edge.leafA),
                                 csvValue(edge.leafB),
                                 csvValue(edge.traffic),
                                 csvValue(edge.expected),
                                 csvValue(edge.modularityGain),
                                 csvValue(edge.utility),
                                 csvValue(edge.communityFactor),
                                 csvValue(edge.stateHoldingGain),
                                 csvValue(edge.selectionScore),
                                 csvBool(isEdgeInSet(replaySelectedEdges,
                                                     edge.leafA,
                                                     edge.leafB)),
                                 replayRejectReasons[candidateIndex]});
                }
                if (!file.good())
                {
                    exportSuccess = false;
                    std::cout << "[HYBRID-DCN][EXPORT] error = failed-to-write:"
                              << ocsCandidatesCsvPath << std::endl;
                }
            }
        }

        {
            std::ofstream file = OpenStructuredResultCsv(linksCsvPath, exportSuccess);
            if (file.is_open())
            {
                writeCsvRow(file, LinksCsvHeader());
                for (uint32_t linkIndex = 0; linkIndex < linkCounters.size(); ++linkIndex)
                {
                    const auto& counter = linkCounters[linkIndex];
                    writeCsvRow(file,
                                {csvValue(linkIndex),
                                 counter.linkId,
                                 counter.linkType,
                                 counter.direction,
                                 counter.endpointAType,
                                 csvValue(counter.endpointA),
                                 counter.endpointBType,
                                 csvValue(counter.endpointB),
                                 csvValue(counter.capacityGbps),
                                 counter.delay,
                                 csvValue(counter.txPackets),
                                 csvValue(counter.txBytes),
                                 csvValue(computeLinkUtilizationApprox(counter)),
                                 counter.note});
                }
                if (!file.good())
                {
                    exportSuccess = false;
                    std::cout << "[HYBRID-DCN][EXPORT] error = failed-to-write:"
                              << linksCsvPath << std::endl;
                }
            }
        }

        {
            std::ofstream file = OpenStructuredResultCsv(linkTimeseriesCsvPath, exportSuccess);
            if (file.is_open())
            {
                writeCsvRow(file, LinkTimeseriesCsvHeader());
                for (const auto& sample : linkUtilizationSamples)
                {
                    if (sample.linkIndex >= linkCounters.size())
                    {
                        exportSuccess = false;
                        std::cout << "[HYBRID-DCN][EXPORT] error = invalid-link-sample-index:"
                                  << sample.linkIndex << std::endl;
                        continue;
                    }
                    const auto& counter = linkCounters[sample.linkIndex];
                    std::ostringstream srcNode;
                    srcNode << counter.endpointAType << counter.endpointA;
                    std::ostringstream dstNode;
                    dstNode << counter.endpointBType << counter.endpointB;
                    writeCsvRow(file,
                                {experimentName,
                                 csvValue(sample.sampleIndex),
                                 csvValue(sample.sampleTimeSeconds),
                                 csvValue(sample.intervalSeconds),
                                 counter.linkType,
                                 counter.linkId,
                                 counter.direction,
                                 srcNode.str(),
                                 dstNode.str(),
                                 csvValue(counter.capacityGbps * 1000.0),
                                 csvValue(sample.deltaTxPackets),
                                 csvValue(sample.deltaTxBytes),
                                 csvValue(sample.cumulativeTxPackets),
                                 csvValue(sample.cumulativeTxBytes),
                                 csvValue(sample.sampleThroughputMbps),
                                 csvValue(sample.utilizationApprox)});
                }
                if (!file.good())
                {
                    exportSuccess = false;
                    std::cout << "[HYBRID-DCN][EXPORT] error = failed-to-write:"
                              << linkTimeseriesCsvPath << std::endl;
                }
            }
        }

        {
            std::ofstream file = OpenStructuredResultCsv(measuredWecmpCsvPath, exportSuccess);
            if (file.is_open())
            {
                writeCsvRow(file, MeasuredWecmpCsvHeader());
                for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
                {
                    for (uint32_t spineIndex = 0; spineIndex < numSpines; ++spineIndex)
                    {
                        const auto& leafToSpine =
                            epsMeasuredLeafToSpineSnapshots[leafIndex][spineIndex];
                        const auto& spineToLeaf =
                            epsMeasuredSpineToLeafSnapshots[leafIndex][spineIndex];
                        writeCsvRow(file,
                                    {experimentName,
                                     csvValue(leafToSpine.lastSampleTime),
                                     csvValue(leafIndex),
                                     csvValue(spineIndex),
                                     "leaf-to-spine",
                                     csvValue(leafToSpine.capacityMbps > 0.0
                                                  ? leafToSpine.capacityMbps
                                                  : leafSpineCapacityGbps * 1000.0),
                                     csvValue(leafToSpine.deltaTxBytes),
                                     csvValue(leafToSpine.cumulativeTxBytes),
                                     csvValue(leafToSpine.sampleThroughputMbps),
                                     csvValue(leafToSpine.utilizationApprox),
                                     csvBool(leafToSpine.hasSample)});
                        writeCsvRow(file,
                                    {experimentName,
                                     csvValue(spineToLeaf.lastSampleTime),
                                     csvValue(leafIndex),
                                     csvValue(spineIndex),
                                     "spine-to-leaf",
                                     csvValue(spineToLeaf.capacityMbps > 0.0
                                                  ? spineToLeaf.capacityMbps
                                                  : leafSpineCapacityGbps * 1000.0),
                                     csvValue(spineToLeaf.deltaTxBytes),
                                     csvValue(spineToLeaf.cumulativeTxBytes),
                                     csvValue(spineToLeaf.sampleThroughputMbps),
                                     csvValue(spineToLeaf.utilizationApprox),
                                     csvBool(spineToLeaf.hasSample)});
                    }
                }
                if (!file.good())
                {
                    exportSuccess = false;
                    std::cout << "[HYBRID-DCN][EXPORT] error = failed-to-write:"
                              << measuredWecmpCsvPath << std::endl;
                }
            }
        }

        structuredExportCheck = exportSuccess;
        std::cout << "[HYBRID-DCN][EXPORT] exportSuccess = "
                  << (exportSuccess ? "true" : "false") << std::endl;
        return structuredExportCheck;
    };

    std::cout << "[HYBRID-DCN][INVARIANT] enabled = "
              << (enableAlgorithmInvariantCheck ? "true" : "false") << std::endl;
    if (enableAlgorithmInvariantCheck)
    {
        auto invariantStatus = [](bool passed) {
            return passed ? "pass" : "fail";
        };
        auto updateOverallInvariant = [](bool& overall, bool passed) {
            overall = overall && passed;
        };

        bool overallAlgorithmInvariant = true;

        std::cout << "[HYBRID-DCN][INVARIANT] strict = "
                  << (strictAlgorithmInvariantCheck ? "true" : "false") << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] stage = stage-40-core-algorithm-invariants"
                  << std::endl;

        struct MatrixInvariantResult
        {
            bool symmetric;
            bool diagonalZero;
            bool nonNegative;
        };
        auto checkTrafficMatrixInvariant = [](const WeightedMatrix& matrix) {
            MatrixInvariantResult result{true, true, true};
            for (uint32_t i = 0; i < matrix.size(); ++i)
            {
                if (i >= matrix[i].size() || std::abs(matrix[i][i]) > 1e-9)
                {
                    result.diagonalZero = false;
                }
                for (uint32_t j = 0; j < matrix[i].size(); ++j)
                {
                    if (matrix[i][j] < -1e-9)
                    {
                        result.nonNegative = false;
                    }
                    if (j < matrix.size() &&
                        i < matrix[j].size() &&
                        std::abs(matrix[i][j] - matrix[j][i]) > 1e-9)
                    {
                        result.symmetric = false;
                    }
                }
            }
            return result;
        };

        const MatrixInvariantResult rawMatrixInvariant =
            checkTrafficMatrixInvariant(rawTrafficMatrix);
        const MatrixInvariantResult controlMatrixInvariant =
            checkTrafficMatrixInvariant(controlTrafficMatrix);
        const bool trafficMatrixSymmetryCheck =
            rawMatrixInvariant.symmetric && controlMatrixInvariant.symmetric;
        const bool trafficMatrixDiagonalZeroCheck =
            rawMatrixInvariant.diagonalZero && controlMatrixInvariant.diagonalZero;
        const bool trafficMatrixNonNegativeCheck =
            rawMatrixInvariant.nonNegative && controlMatrixInvariant.nonNegative;
        updateOverallInvariant(overallAlgorithmInvariant, rawMatrixInvariant.symmetric);
        updateOverallInvariant(overallAlgorithmInvariant, rawMatrixInvariant.diagonalZero);
        updateOverallInvariant(overallAlgorithmInvariant, rawMatrixInvariant.nonNegative);
        updateOverallInvariant(overallAlgorithmInvariant, controlMatrixInvariant.symmetric);
        updateOverallInvariant(overallAlgorithmInvariant, controlMatrixInvariant.diagonalZero);
        updateOverallInvariant(overallAlgorithmInvariant, controlMatrixInvariant.nonNegative);
        std::cout << "[HYBRID-DCN][INVARIANT] rawTrafficMatrixSymmetryCheck = "
                  << invariantStatus(rawMatrixInvariant.symmetric) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] rawTrafficMatrixDiagonalZeroCheck = "
                  << invariantStatus(rawMatrixInvariant.diagonalZero) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] rawTrafficMatrixNonNegativeCheck = "
                  << invariantStatus(rawMatrixInvariant.nonNegative) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] controlTrafficMatrixSymmetryCheck = "
                  << invariantStatus(controlMatrixInvariant.symmetric) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] controlTrafficMatrixDiagonalZeroCheck = "
                  << invariantStatus(controlMatrixInvariant.diagonalZero) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] controlTrafficMatrixNonNegativeCheck = "
                  << invariantStatus(controlMatrixInvariant.nonNegative) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] trafficMatrixSymmetryCheck = "
                  << invariantStatus(trafficMatrixSymmetryCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] trafficMatrixDiagonalZeroCheck = "
                  << invariantStatus(trafficMatrixDiagonalZeroCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] trafficMatrixNonNegativeCheck = "
                  << invariantStatus(trafficMatrixNonNegativeCheck) << std::endl;

        if (enableEwmaSmoothing)
        {
            const bool ewmaMatrixCheck = controlMatrixInvariant.symmetric &&
                                         controlMatrixInvariant.diagonalZero &&
                                         controlMatrixInvariant.nonNegative;
            updateOverallInvariant(overallAlgorithmInvariant, ewmaMatrixCheck);
            std::cout << "[HYBRID-DCN][INVARIANT] ewmaMatrixCheck = "
                      << invariantStatus(ewmaMatrixCheck) << std::endl;
        }
        else
        {
            std::cout << "[HYBRID-DCN][INVARIANT] ewmaMatrixCheck = skipped"
                      << std::endl;
        }

        bool nodeDegreeNonNegativeCheck = true;
        for (const auto degree : nodeDegree)
        {
            if (degree < -1e-9)
            {
                nodeDegreeNonNegativeCheck = false;
                break;
            }
        }

        bool expectedTrafficSymmetryCheck = true;
        bool modularityGainSymmetryCheck = true;
        bool utilityNonNegativeCheck = true;
        for (uint32_t i = 0; i < numLeaves; ++i)
        {
            for (uint32_t j = 0; j < numLeaves; ++j)
            {
                if (std::abs(expectedTraffic[i][j] - expectedTraffic[j][i]) > 1e-9)
                {
                    expectedTrafficSymmetryCheck = false;
                }
                if (std::abs(modularityGain[i][j] - modularityGain[j][i]) > 1e-9)
                {
                    modularityGainSymmetryCheck = false;
                }
                if (ocsUtility[i][j] < -1e-9)
                {
                    utilityNonNegativeCheck = false;
                }
            }
        }

        bool communityLabelRangeCheck = activeCommunityLabels.size() == numLeaves;
        if (communityLabelRangeCheck && activeCommunityCount > 0)
        {
            for (const auto label : activeCommunityLabels)
            {
                if (label >= activeCommunityCount)
                {
                    communityLabelRangeCheck = false;
                    break;
                }
            }
        }

        bool selectedEdgesPositiveScoreCheck = true;
        for (const auto& edge : selectedOcsEdges)
        {
            if (edge.selectionScore <= 0 &&
                !isEdgeInSet(holdOcsEdges, edge.leafA, edge.leafB))
            {
                selectedEdgesPositiveScoreCheck = false;
                break;
            }
        }

        updateOverallInvariant(overallAlgorithmInvariant, nodeDegreeNonNegativeCheck);
        updateOverallInvariant(overallAlgorithmInvariant, expectedTrafficSymmetryCheck);
        updateOverallInvariant(overallAlgorithmInvariant, modularityGainSymmetryCheck);
        updateOverallInvariant(overallAlgorithmInvariant, utilityNonNegativeCheck);
        updateOverallInvariant(overallAlgorithmInvariant, communityLabelRangeCheck);
        updateOverallInvariant(overallAlgorithmInvariant, selectedEdgesPositiveScoreCheck);
        std::cout << "[HYBRID-DCN][INVARIANT] nodeDegreeNonNegativeCheck = "
                  << invariantStatus(nodeDegreeNonNegativeCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] expectedTrafficSymmetryCheck = "
                  << invariantStatus(expectedTrafficSymmetryCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] modularityGainSymmetryCheck = "
                  << invariantStatus(modularityGainSymmetryCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] utilityNonNegativeCheck = "
                  << invariantStatus(utilityNonNegativeCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] communityLabelRangeCheck = "
                  << invariantStatus(communityLabelRangeCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] selectedEdgesPositiveScoreCheck = "
                  << invariantStatus(selectedEdgesPositiveScoreCheck) << std::endl;

        std::vector<uint32_t> invariantOcsDegree(numLeaves, 0);
        bool ocsPortConstraintCheck = true;
        for (const auto& edge : selectedOcsEdges)
        {
            if (edge.leafA >= numLeaves || edge.leafB >= numLeaves)
            {
                ocsPortConstraintCheck = false;
                continue;
            }
            invariantOcsDegree[edge.leafA]++;
            invariantOcsDegree[edge.leafB]++;
        }
        for (const auto degree : invariantOcsDegree)
        {
            if (degree > ocsPortK)
            {
                ocsPortConstraintCheck = false;
                break;
            }
        }
        updateOverallInvariant(overallAlgorithmInvariant, ocsPortConstraintCheck);
        std::cout << "[HYBRID-DCN][INVARIANT] ocsPortConstraintCheck = "
                  << invariantStatus(ocsPortConstraintCheck) << std::endl;

        const bool maxSelectedOcsLinksCheck =
            selectedOcsEdges.size() <= maxSelectedOcsLinks;
        updateOverallInvariant(overallAlgorithmInvariant, maxSelectedOcsLinksCheck);
        std::cout << "[HYBRID-DCN][INVARIANT] maxSelectedOcsLinksCheck = "
                  << invariantStatus(maxSelectedOcsLinksCheck) << std::endl;

        const bool previousChangedEdgesZeroCheck = previousChangedEdges == 0;
        const bool changedEdgesNonNegativeCheck =
            candidateChangedEdges <= candidateOcsEdges.size() + previousOcsEdges.size();
        const bool configScoreModeCheck =
            configScoreMode == "selection-score-sum" || configScoreMode == "paper-objective";
        updateOverallInvariant(overallAlgorithmInvariant, previousChangedEdgesZeroCheck);
        updateOverallInvariant(overallAlgorithmInvariant, changedEdgesNonNegativeCheck);
        updateOverallInvariant(overallAlgorithmInvariant, configScoreModeCheck);
        std::cout << "[HYBRID-DCN][INVARIANT] previousChangedEdgesZeroCheck = "
                  << invariantStatus(previousChangedEdgesZeroCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] changedEdgesNonNegativeCheck = "
                  << invariantStatus(changedEdgesNonNegativeCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] configScoreModeCheck = "
                  << invariantStatus(configScoreModeCheck) << std::endl;

        bool hardHoldPreservedInCandidateCheck = true;
        if (holdTimeActive || !holdOcsEdges.empty())
        {
            for (const auto& edge : holdOcsEdges)
            {
                if (!isEdgeInSet(candidateOcsEdges, edge.leafA, edge.leafB))
                {
                    hardHoldPreservedInCandidateCheck = false;
                    break;
                }
            }
        }
        updateOverallInvariant(overallAlgorithmInvariant, hardHoldPreservedInCandidateCheck);
        std::cout << "[HYBRID-DCN][INVARIANT] hardHoldPreservedInCandidateCheck = "
                  << invariantStatus(hardHoldPreservedInCandidateCheck) << std::endl;

        if (enableHoldTimeGate)
        {
            bool hardHoldAgeCheck = true;
            for (const auto& edge : holdOcsEdges)
            {
                const uint32_t edgeAge =
                    GetOcsEdgeAge(previousOcsEdgeAges, edge.leafA, edge.leafB);
                if (edgeAge >= minHoldCycles)
                {
                    hardHoldAgeCheck = false;
                    break;
                }
            }
            if (hardHoldAgeCheck)
            {
                for (const auto& edge : previousOcsEdges)
                {
                    const uint32_t edgeAge =
                        GetOcsEdgeAge(previousOcsEdgeAges, edge.leafA, edge.leafB);
                    const bool hardHeld =
                        isEdgeInSet(holdOcsEdges, edge.leafA, edge.leafB);
                    if ((edgeAge < minHoldCycles && !hardHeld) ||
                        (edgeAge >= minHoldCycles && hardHeld))
                    {
                        hardHoldAgeCheck = false;
                        break;
                    }
                }
            }
            updateOverallInvariant(overallAlgorithmInvariant, hardHoldAgeCheck);
            std::cout << "[HYBRID-DCN][INVARIANT] hardHoldAgeCheck = "
                      << invariantStatus(hardHoldAgeCheck) << std::endl;
        }
        else
        {
            std::cout << "[HYBRID-DCN][INVARIANT] hardHoldAgeCheck = skipped"
                      << std::endl;
        }

        if (enableOcsAdmissionControl)
        {
            bool ocsAdmissionThresholdCheck = true;
            for (const auto& spec : matrixFlowSpecs)
            {
                if (spec.ocsAdmitted &&
                    spec.ocsLoadAfter > ocsAdmissionThreshold + 1e-9)
                {
                    ocsAdmissionThresholdCheck = false;
                    break;
                }
            }
            updateOverallInvariant(overallAlgorithmInvariant, ocsAdmissionThresholdCheck);
            std::cout << "[HYBRID-DCN][INVARIANT] ocsAdmissionThresholdCheck = "
                      << invariantStatus(ocsAdmissionThresholdCheck) << std::endl;
        }
        else
        {
            std::cout << "[HYBRID-DCN][INVARIANT] ocsAdmissionThresholdCheck = skipped"
                      << std::endl;
        }

        bool residualDemandNonNegativeCheck = true;
        for (const auto& spec : matrixFlowSpecs)
        {
            if (spec.plannedResidualDemand < -1e-9 ||
                spec.realResidualDemand < -1e-9 ||
                spec.wecmpResidualDemand < -1e-9)
            {
                residualDemandNonNegativeCheck = false;
                break;
            }
        }
        updateOverallInvariant(overallAlgorithmInvariant, residualDemandNonNegativeCheck);
        std::cout << "[HYBRID-DCN][INVARIANT] residualDemandNonNegativeCheck = "
                  << invariantStatus(residualDemandNonNegativeCheck) << std::endl;

        if (enableEpsWecmp)
        {
            bool wecmpProbabilitySumCheck = true;
            bool wecmpProbabilityRangeCheck = true;
            bool wecmpProbabilityDeltaBoundCheck = true;
            for (const auto& state : epsWecmpPairStates)
            {
                double probabilitySum = 0.0;
                for (const auto probability : state.probabilities)
                {
                    probabilitySum += probability;
                    if (probability < -1e-9 || probability > 1.0 + 1e-9)
                    {
                        wecmpProbabilityRangeCheck = false;
                    }
                }
                if (!state.probabilities.empty() &&
                    std::abs(probabilitySum - 1.0) > 1e-6)
                {
                    wecmpProbabilitySumCheck = false;
                }
            }
            for (const auto& decision : epsWecmpDecisions)
            {
                for (const auto& linkState : decision.linkStates)
                {
                    if (std::abs(linkState.boundedProbabilityDelta) >
                        epsWecmpMaxDelta + 1e-6)
                    {
                        wecmpProbabilityDeltaBoundCheck = false;
                        break;
                    }
                }
                if (!wecmpProbabilityDeltaBoundCheck)
                {
                    break;
                }
            }
            updateOverallInvariant(overallAlgorithmInvariant, wecmpProbabilitySumCheck);
            updateOverallInvariant(overallAlgorithmInvariant, wecmpProbabilityRangeCheck);
            updateOverallInvariant(overallAlgorithmInvariant, wecmpProbabilityDeltaBoundCheck);
            std::cout << "[HYBRID-DCN][INVARIANT] wecmpProbabilitySumCheck = "
                      << invariantStatus(wecmpProbabilitySumCheck) << std::endl;
            std::cout << "[HYBRID-DCN][INVARIANT] wecmpProbabilityRangeCheck = "
                      << invariantStatus(wecmpProbabilityRangeCheck) << std::endl;
            std::cout << "[HYBRID-DCN][INVARIANT] wecmpProbabilityDeltaBoundCheck = "
                      << invariantStatus(wecmpProbabilityDeltaBoundCheck) << std::endl;

            bool epsPhysicalLinkNonNegativeCheck = true;
            for (const auto& leafStates : epsPhysicalLinkStates)
            {
                for (const auto& linkState : leafStates)
                {
                    if (linkState.observedTraffic < -1e-9 ||
                        linkState.utilization < -1e-9 ||
                        linkState.smoothedUtilization < -1e-9)
                    {
                        epsPhysicalLinkNonNegativeCheck = false;
                        break;
                    }
                }
                if (!epsPhysicalLinkNonNegativeCheck)
                {
                    break;
                }
            }
            updateOverallInvariant(overallAlgorithmInvariant,
                                   epsPhysicalLinkNonNegativeCheck);
            std::cout << "[HYBRID-DCN][INVARIANT] epsPhysicalLinkNonNegativeCheck = "
                      << invariantStatus(epsPhysicalLinkNonNegativeCheck) << std::endl;
        }
        else
        {
            std::cout << "[HYBRID-DCN][INVARIANT] wecmpProbabilitySumCheck = skipped"
                      << std::endl;
            std::cout << "[HYBRID-DCN][INVARIANT] wecmpProbabilityRangeCheck = skipped"
                      << std::endl;
            std::cout << "[HYBRID-DCN][INVARIANT] wecmpProbabilityDeltaBoundCheck = skipped"
                      << std::endl;
            std::cout << "[HYBRID-DCN][INVARIANT] epsPhysicalLinkNonNegativeCheck = skipped"
                      << std::endl;
        }

        if (enableEpsWecmpRouting)
        {
            bool routeBindingFreezeCheck = true;
            for (const auto& binding : epsWecmpRouteBindings)
            {
                if (binding.flowIndex >= matrixFlowSpecs.size())
                {
                    routeBindingFreezeCheck = false;
                    break;
                }

                const auto& spec = matrixFlowSpecs[binding.flowIndex];
                if ((binding.installed && !binding.pathFrozen) ||
                    (binding.pathFrozen && !spec.epsPathFrozen) ||
                    (binding.pathFrozen &&
                     spec.frozenSpine != static_cast<int32_t>(binding.selectedSpine)) ||
                    !requiresEpsResidualPath(spec))
                {
                    routeBindingFreezeCheck = false;
                    break;
                }
            }
            updateOverallInvariant(overallAlgorithmInvariant, routeBindingFreezeCheck);
            std::cout << "[HYBRID-DCN][INVARIANT] routeBindingFreezeCheck = "
                      << invariantStatus(routeBindingFreezeCheck) << std::endl;
        }
        else
        {
            std::cout << "[HYBRID-DCN][INVARIANT] routeBindingFreezeCheck = skipped"
                      << std::endl;
        }

        bool ocsAdmittedNotEpsFrozenCheck = true;
        for (const auto& spec : matrixFlowSpecs)
        {
            if (spec.ocsAdmitted && (spec.epsPathFrozen || spec.frozenSpine != -1))
            {
                ocsAdmittedNotEpsFrozenCheck = false;
                break;
            }
        }
        updateOverallInvariant(overallAlgorithmInvariant, ocsAdmittedNotEpsFrozenCheck);
        std::cout << "[HYBRID-DCN][INVARIANT] ocsAdmittedNotEpsFrozenCheck = "
                  << invariantStatus(ocsAdmittedNotEpsFrozenCheck) << std::endl;

        bool matrixFlowCompletionCountCheck = true;
        if (enableMatrixFlows && !matrixFlowSpecs.empty())
        {
            matrixFlowCompletionCountCheck =
                resultCompletedFlows <= matrixFlowSpecs.size();
        }
        updateOverallInvariant(overallAlgorithmInvariant, matrixFlowCompletionCountCheck);
        std::cout << "[HYBRID-DCN][INVARIANT] matrixFlowCompletionCountCheck = "
                  << invariantStatus(matrixFlowCompletionCountCheck) << std::endl;

        bool matrixFlowPortUniquenessCheck = true;
        bool matrixFlowStartBeforeStopCheck = true;
        bool matrixFlowRxBytesBoundCheck = true;
        bool ocsCoveredFlowsHaveInstalledPairCheck = true;
        bool fallbackFlowsAreResidualCheck = true;
        bool wecmpFrozenFlowsAreResidualCheck = true;
        bool resultGoodputFiniteCheck = std::isfinite(resultMatrixAggregateGoodputMbps);
        std::set<uint16_t> matrixFlowPorts;
        if (enableMatrixFlows)
        {
            for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
            {
                const auto& spec = matrixFlowSpecs[flowIndex];
                const auto& stats = matrixFlowStats[flowIndex];
                if (!matrixFlowPorts.insert(spec.port).second)
                {
                    matrixFlowPortUniquenessCheck = false;
                }
                if (spec.startTime >= simTime)
                {
                    matrixFlowStartBeforeStopCheck = false;
                }
                if (stats.rxBytes > spec.expectedBytes + matrixFlowRxBytesTolerance)
                {
                    matrixFlowRxBytesBoundCheck = false;
                }
                if (spec.ocsCovered && !isFinalEdgeInstalled(spec.srcLeaf, spec.dstLeaf))
                {
                    ocsCoveredFlowsHaveInstalledPairCheck = false;
                }
                if (spec.epsFallback &&
                    (spec.ocsAdmitted || spec.ocsCovered || !requiresEpsResidualPath(spec) ||
                     spec.wecmpResidualDemand <= 0 ||
                     spec.realResidualDemand <= 0))
                {
                    fallbackFlowsAreResidualCheck = false;
                }
                if (spec.epsPathFrozen && !requiresEpsResidualPath(spec))
                {
                    wecmpFrozenFlowsAreResidualCheck = false;
                }

                const double firstRx = stats.seenFirstRx ? stats.firstRxTime : 0.0;
                const double lastRx = stats.seenFirstRx ? stats.lastRxTime : 0.0;
                const double duration = lastRx > firstRx ? (lastRx - firstRx) : 0.0;
                const double goodputMbps =
                    duration > 0
                        ? static_cast<double>(stats.rxBytes) * 8.0 / duration / 1e6
                        : 0.0;
                if (!std::isfinite(goodputMbps))
                {
                    resultGoodputFiniteCheck = false;
                }
            }
        }
        for (const auto& event : ocsAdmissionEvents)
        {
            if (event.epsFallback &&
                (event.ocsAdmitted || event.realResidualDemand <= 0 ||
                 event.wecmpResidualDemand <= 0))
            {
                fallbackFlowsAreResidualCheck = false;
            }
        }

        bool routeBindingFlowIndexCheck = true;
        for (const auto& binding : epsWecmpRouteBindings)
        {
            if (binding.flowIndex >= matrixFlowSpecs.size())
            {
                routeBindingFlowIndexCheck = false;
                break;
            }
        }

        updateOverallInvariant(overallAlgorithmInvariant, matrixFlowPortUniquenessCheck);
        updateOverallInvariant(overallAlgorithmInvariant, matrixFlowStartBeforeStopCheck);
        updateOverallInvariant(overallAlgorithmInvariant, matrixFlowRxBytesBoundCheck);
        updateOverallInvariant(overallAlgorithmInvariant,
                               ocsCoveredFlowsHaveInstalledPairCheck);
        updateOverallInvariant(overallAlgorithmInvariant, fallbackFlowsAreResidualCheck);
        updateOverallInvariant(overallAlgorithmInvariant, wecmpFrozenFlowsAreResidualCheck);
        updateOverallInvariant(overallAlgorithmInvariant, routeBindingFlowIndexCheck);
        updateOverallInvariant(overallAlgorithmInvariant, resultGoodputFiniteCheck);
        std::cout << "[HYBRID-DCN][INVARIANT] matrixFlowPortUniquenessCheck = "
                  << invariantStatus(matrixFlowPortUniquenessCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] matrixFlowStartBeforeStopCheck = "
                  << invariantStatus(matrixFlowStartBeforeStopCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] matrixFlowRxBytesBoundCheck = "
                  << invariantStatus(matrixFlowRxBytesBoundCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] matrixFlowRxBytesTolerance = "
                  << matrixFlowRxBytesTolerance << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] ocsCoveredFlowsHaveInstalledPairCheck = "
                  << invariantStatus(ocsCoveredFlowsHaveInstalledPairCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] fallbackFlowsAreResidualCheck = "
                  << invariantStatus(fallbackFlowsAreResidualCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] wecmpFrozenFlowsAreResidualCheck = "
                  << invariantStatus(wecmpFrozenFlowsAreResidualCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] routeBindingFlowIndexCheck = "
                  << invariantStatus(routeBindingFlowIndexCheck) << std::endl;
        std::cout << "[HYBRID-DCN][INVARIANT] resultGoodputFiniteCheck = "
                  << invariantStatus(resultGoodputFiniteCheck) << std::endl;

        structuredExportCheck = runStructuredResultExport(invariantStatus(overallAlgorithmInvariant));
        if (enableStructuredResultExport)
        {
            updateOverallInvariant(overallAlgorithmInvariant, structuredExportCheck);
        }
        std::cout << "[HYBRID-DCN][INVARIANT] structuredExportCheck = "
                  << (enableStructuredResultExport ? invariantStatus(structuredExportCheck)
                                                   : "skipped")
                  << std::endl;

        std::cout << "[HYBRID-DCN][INVARIANT] overallAlgorithmInvariant = "
                  << invariantStatus(overallAlgorithmInvariant) << std::endl;

        if (strictAlgorithmInvariantCheck && !overallAlgorithmInvariant)
        {
            std::cerr << "[HYBRID-DCN][ERROR] algorithm invariant check failed."
                      << std::endl;
            return 1;
        }
    }
    else
    {
        structuredExportCheck = runStructuredResultExport("skipped");
        std::cout << "[HYBRID-DCN][INVARIANT] structuredExportCheck = "
                  << (enableStructuredResultExport
                          ? (structuredExportCheck ? "pass" : "fail")
                          : "skipped")
                  << std::endl;
    }

    std::cout << "[OK] Hybrid Core DCN topology build completed." << std::endl;

    return 0;
}

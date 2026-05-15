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

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace ns3;

struct OcsCandidateEdge
{
    uint32_t leafA;
    uint32_t leafB;
    double traffic;
    double expected;
    double modularityGain;
    double utility;
    double baseUtility;
    double communityFactor;
    double communityUtility;
    bool intraCommunity;
    bool wasPreviouslyInstalled;
    double stateHoldingGain;
    double selectionScore;
};

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
    std::string name;
};

struct MatrixBulkFlowStats
{
    uint64_t rxBytes;
    bool seenFirstRx;
    double firstRxTime;
    double lastRxTime;
};

void
MatrixBulkSinkRxTrace(std::vector<MatrixBulkFlowStats>* stats,
                      uint32_t flowIndex,
                      Ptr<const Packet> packet,
                      const Address& from)
{
    (void)from;
    if (stats == nullptr || flowIndex >= stats->size())
    {
        return;
    }

    MatrixBulkFlowStats& flowStats = stats->at(flowIndex);
    flowStats.rxBytes += packet->GetSize();

    const double now = Simulator::Now().GetSeconds();
    if (!flowStats.seenFirstRx)
    {
        flowStats.seenFirstRx = true;
        flowStats.firstRxTime = now;
    }
    flowStats.lastRxTime = now;
}

struct CommunityPreview
{
    std::vector<uint32_t> labels;
    uint32_t communityCount;
};

struct LouvainLevelSummary
{
    uint32_t level;
    uint32_t nodeCountBefore;
    uint32_t nodeCountAfter;
    uint32_t communityCount;
    uint32_t passes;
    bool moved;
    double modularityQ;
};

struct LouvainResult
{
    std::vector<uint32_t> labels;
    uint32_t communityCount;
    uint32_t passes;
    bool moved;
    double modularityQ;
    uint32_t levels;
    bool foldedGraph;
    std::vector<LouvainLevelSummary> levelSummaries;
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
    double candidateConfigScore;
    double previousConfigScore;
    double configScoreImprovement;
    bool holdTimeActive;
    std::string decision;
    uint32_t previousConfigAgeBefore;
    uint32_t previousConfigAgeAfter;
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
    bool enableHoldTimeGate = false;
    uint32_t minHoldCycles = 1;
    uint32_t previousConfigAge = 1;
    uint32_t ocsPortK = 1;
    uint32_t maxSelectedOcsLinks = 1;
    bool enableMatrixFlows = true;
    uint64_t matrixFlowMaxBytes = 524288;
    double matrixFlowStart = 0.35;
    uint16_t matrixFlowPortBase = 11000;
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
                 "Experiment preset: manual, baseline-excess, community-aware, state-aware, config-gated, hold-gated, or full-control.",
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
    cmd.AddValue("enableHoldTimeGate", "Enable minimum OCS configuration hold-time gating.", enableHoldTimeGate);
    cmd.AddValue("minHoldCycles", "Minimum OCS configuration hold cycles.", minHoldCycles);
    cmd.AddValue("previousConfigAge", "Number of cycles that the previous OCS configuration has been held.", previousConfigAge);
    cmd.AddValue("ocsPortK", "Per-Leaf OCS port budget for greedy candidate selection.", ocsPortK);
    cmd.AddValue("maxSelectedOcsLinks", "Maximum number of OCS candidate links selected by the controller.", maxSelectedOcsLinks);
    cmd.AddValue("enableMatrixFlows", "Enable Stage-12 matrix-driven multi-pair BulkSend flows.", enableMatrixFlows);
    cmd.AddValue("matrixFlowMaxBytes", "MaxBytes for each matrix-generated BulkSend flow.", matrixFlowMaxBytes);
    cmd.AddValue("matrixFlowStart", "Start time for matrix-generated BulkSend flows in seconds.", matrixFlowStart);
    cmd.AddValue("matrixFlowPortBase", "Base TCP port for matrix-generated PacketSink applications.", matrixFlowPortBase);
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
        presetScenario != "full-control")
    {
        std::cerr << "[HYBRID-DCN][ERROR] presetScenario must be manual, baseline-excess, community-aware, state-aware, config-gated, hold-gated, or full-control."
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

    auto buildTrafficMatrix = [](const std::string& mode, uint32_t leafCount) {
        std::vector<std::vector<double>> matrix(leafCount,
                                                std::vector<double>(leafCount, 0.0));

        if (mode == "skewed")
        {
            for (uint32_t i = 0; i < leafCount; ++i)
            {
                for (uint32_t j = i + 1; j < leafCount; ++j)
                {
                    matrix[i][j] = 10.0;
                    matrix[j][i] = 10.0;
                }
            }

            if (leafCount >= 4)
            {
                matrix[0][3] = 100.0;
                matrix[3][0] = 100.0;
            }

            if (leafCount >= 3)
            {
                matrix[1][2] = 30.0;
                matrix[2][1] = 30.0;
            }
        }
        else if (mode == "clustered")
        {
            const uint32_t split = leafCount / 2;
            for (uint32_t i = 0; i < leafCount; ++i)
            {
                for (uint32_t j = i + 1; j < leafCount; ++j)
                {
                    const bool sameCluster = (i < split && j < split) || (i >= split && j >= split);
                    matrix[i][j] = sameCluster ? 80.0 : 10.0;
                    matrix[j][i] = matrix[i][j];
                }
            }
        }
        else
        {
            for (uint32_t i = 0; i < leafCount; ++i)
            {
                for (uint32_t j = i + 1; j < leafCount; ++j)
                {
                    matrix[i][j] = 20.0;
                    matrix[j][i] = 20.0;
                }
            }
        }

        return matrix;
    };

    std::vector<std::vector<double>> torTrafficMatrix =
        buildTrafficMatrix(trafficMatrixMode, numLeaves);

    auto buildCommunityPreview = [](const std::string& mode, uint32_t leafCount) {
        CommunityPreview preview;
        preview.labels.assign(leafCount, 0);
        preview.communityCount = 0;

        if (mode == "clustered")
        {
            const uint32_t split = leafCount / 2;
            for (uint32_t leafIndex = 0; leafIndex < leafCount; ++leafIndex)
            {
                preview.labels[leafIndex] = leafIndex < split ? 0 : 1;
            }
            preview.communityCount = leafCount == 0 ? 0 : 2;
        }
        else if (mode == "skewed")
        {
            if (leafCount >= 4)
            {
                preview.labels[0] = 0;
                preview.labels[3] = 0;
                preview.labels[1] = 1;
                preview.labels[2] = 1;
                uint32_t nextCommunity = 2;
                for (uint32_t leafIndex = 4; leafIndex < leafCount; ++leafIndex)
                {
                    preview.labels[leafIndex] = nextCommunity++;
                }
                preview.communityCount = nextCommunity;
            }
            else
            {
                for (uint32_t leafIndex = 0; leafIndex < leafCount; ++leafIndex)
                {
                    preview.labels[leafIndex] = leafIndex;
                }
                preview.communityCount = leafCount;
            }
        }
        else
        {
            for (uint32_t leafIndex = 0; leafIndex < leafCount; ++leafIndex)
            {
                preview.labels[leafIndex] = 0;
            }
            preview.communityCount = leafCount == 0 ? 0 : 1;
        }

        return preview;
    };

    const CommunityPreview communityPreview =
        buildCommunityPreview(trafficMatrixMode, numLeaves);

    std::vector<double> nodeDegree(numLeaves, 0.0);
    for (uint32_t i = 0; i < numLeaves; ++i)
    {
        for (uint32_t j = 0; j < numLeaves; ++j)
        {
            nodeDegree[i] += torTrafficMatrix[i][j];
        }
    }

    double totalTraffic = 0.0;
    for (uint32_t i = 0; i < numLeaves; ++i)
    {
        for (uint32_t j = 0; j < numLeaves; ++j)
        {
            totalTraffic += torTrafficMatrix[i][j];
        }
    }
    totalTraffic *= 0.5;

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

    using WeightedMatrix = std::vector<std::vector<double>>;

    auto normalizeCommunityLabels = [](std::vector<uint32_t>& labels) {
        std::vector<uint32_t> oldLabels;
        for (uint32_t& label : labels)
        {
            uint32_t normalized = 0;
            bool found = false;
            for (uint32_t index = 0; index < oldLabels.size(); ++index)
            {
                if (oldLabels[index] == label)
                {
                    normalized = index;
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                normalized = static_cast<uint32_t>(oldLabels.size());
                oldLabels.push_back(label);
            }
            label = normalized;
        }
        return static_cast<uint32_t>(oldLabels.size());
    };

    auto computeNodeDegree = [](const WeightedMatrix& matrix) {
        std::vector<double> degree(matrix.size(), 0.0);
        for (uint32_t i = 0; i < matrix.size(); ++i)
        {
            for (uint32_t j = 0; j < matrix[i].size(); ++j)
            {
                degree[i] += matrix[i][j];
            }
        }
        return degree;
    };

    auto computeTotalTraffic = [](const WeightedMatrix& matrix) {
        double traffic = 0.0;
        for (uint32_t i = 0; i < matrix.size(); ++i)
        {
            for (uint32_t j = 0; j < matrix[i].size(); ++j)
            {
                traffic += matrix[i][j];
            }
        }
        return traffic * 0.5;
    };

    auto computeModularityQ = [&](const WeightedMatrix& matrix,
                                  const std::vector<uint32_t>& labels) {
        const double graphTraffic = computeTotalTraffic(matrix);
        if (graphTraffic <= 0 || matrix.empty())
        {
            return 0.0;
        }

        const std::vector<double> degree = computeNodeDegree(matrix);
        const double twoM = 2.0 * graphTraffic;
        double modularitySum = 0.0;
        for (uint32_t i = 0; i < matrix.size(); ++i)
        {
            for (uint32_t j = 0; j < matrix[i].size(); ++j)
            {
                if (labels[i] != labels[j])
                {
                    continue;
                }
                const double expected = degree[i] * degree[j] / twoM;
                modularitySum += matrix[i][j] - (eta * expected);
            }
        }
        return modularitySum / twoM;
    };

    auto runLocalMoving = [&](const WeightedMatrix& matrix) {
        LouvainResult result;
        const uint32_t nodeCount = static_cast<uint32_t>(matrix.size());
        result.labels.resize(nodeCount);
        for (uint32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
        {
            result.labels[nodeIndex] = nodeIndex;
        }
        result.communityCount = nodeCount;
        result.passes = 0;
        result.moved = false;
        result.modularityQ = computeModularityQ(matrix, result.labels);
        result.levels = 1;
        result.foldedGraph = false;

        for (uint32_t pass = 0; pass < louvainMaxPasses; ++pass)
        {
            bool passMoved = false;
            for (uint32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                const uint32_t oldLabel = result.labels[nodeIndex];
                std::vector<uint32_t> candidateLabels;
                candidateLabels.push_back(oldLabel);
                for (uint32_t neighbor = 0; neighbor < nodeCount; ++neighbor)
                {
                    if (matrix[nodeIndex][neighbor] <= 0)
                    {
                        continue;
                    }

                    const uint32_t neighborLabel = result.labels[neighbor];
                    if (std::find(candidateLabels.begin(),
                                  candidateLabels.end(),
                                  neighborLabel) == candidateLabels.end())
                    {
                        candidateLabels.push_back(neighborLabel);
                    }
                }
                std::sort(candidateLabels.begin(), candidateLabels.end());

                const double currentQ = computeModularityQ(matrix, result.labels);
                double bestQ = currentQ;
                uint32_t bestLabel = oldLabel;
                std::vector<uint32_t> bestLabels = result.labels;

                for (uint32_t candidateLabel : candidateLabels)
                {
                    std::vector<uint32_t> trialLabels = result.labels;
                    trialLabels[nodeIndex] = candidateLabel;
                    normalizeCommunityLabels(trialLabels);
                    const double trialQ = computeModularityQ(matrix, trialLabels);
                    const uint32_t trialLabel = trialLabels[nodeIndex];

                    if (trialQ > bestQ + louvainEpsilon ||
                        (std::abs(trialQ - bestQ) <= louvainEpsilon &&
                         trialLabel < bestLabel))
                    {
                        bestQ = trialQ;
                        bestLabel = trialLabel;
                        bestLabels = trialLabels;
                    }
                }

                if (bestLabels != result.labels)
                {
                    result.labels = bestLabels;
                    result.communityCount = normalizeCommunityLabels(result.labels);
                    passMoved = true;
                    result.moved = true;
                }
            }

            result.passes = pass + 1;
            if (!passMoved)
            {
                break;
            }
        }

        result.communityCount = normalizeCommunityLabels(result.labels);
        result.modularityQ = computeModularityQ(matrix, result.labels);
        result.levelSummaries.push_back(LouvainLevelSummary{0,
                                                            nodeCount,
                                                            result.communityCount,
                                                            result.communityCount,
                                                            result.passes,
                                                            result.moved,
                                                            result.modularityQ});
        return result;
    };

    auto buildCoarsenedGraph = [](const WeightedMatrix& matrix,
                                  const std::vector<uint32_t>& labels,
                                  uint32_t communityCount) {
        WeightedMatrix coarsened(communityCount, std::vector<double>(communityCount, 0.0));
        for (uint32_t i = 0; i < matrix.size(); ++i)
        {
            for (uint32_t j = 0; j < matrix[i].size(); ++j)
            {
                coarsened[labels[i]][labels[j]] += matrix[i][j];
            }
        }
        return coarsened;
    };

    auto runSingleLevelLouvain = [&]() {
        LouvainResult result = runLocalMoving(torTrafficMatrix);
        result.modularityQ = computeModularityQ(torTrafficMatrix, result.labels);
        result.levels = 1;
        result.foldedGraph = false;
        if (!result.levelSummaries.empty())
        {
            result.levelSummaries[0].level = 0;
            result.levelSummaries[0].nodeCountBefore = numLeaves;
            result.levelSummaries[0].nodeCountAfter = result.communityCount;
            result.levelSummaries[0].communityCount = result.communityCount;
            result.levelSummaries[0].modularityQ = result.modularityQ;
        }
        return result;
    };

    auto runMultiLevelLouvain = [&]() {
        WeightedMatrix currentMatrix = torTrafficMatrix;
        std::vector<uint32_t> originalToCurrent(numLeaves);
        for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
        {
            originalToCurrent[leafIndex] = leafIndex;
        }

        LouvainResult result;
        result.labels = originalToCurrent;
        result.communityCount = numLeaves;
        result.passes = 0;
        result.moved = false;
        result.modularityQ = computeModularityQ(torTrafficMatrix, result.labels);
        result.levels = 0;
        result.foldedGraph = false;

        double previousLevelQ = 0.0;
        bool havePreviousLevelQ = false;
        for (uint32_t level = 0; level < louvainMaxLevels; ++level)
        {
            LouvainResult local = runLocalMoving(currentMatrix);
            result.passes += local.passes;
            result.moved = result.moved || local.moved;

            for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
            {
                originalToCurrent[leafIndex] = local.labels[originalToCurrent[leafIndex]];
            }
            result.communityCount = normalizeCommunityLabels(originalToCurrent);

            const uint32_t nodeCountBefore = static_cast<uint32_t>(currentMatrix.size());
            result.levelSummaries.push_back(LouvainLevelSummary{level,
                                                                nodeCountBefore,
                                                                local.communityCount,
                                                                local.communityCount,
                                                                local.passes,
                                                                local.moved,
                                                                local.modularityQ});
            result.levels = static_cast<uint32_t>(result.levelSummaries.size());

            if (local.communityCount == nodeCountBefore)
            {
                break;
            }

            if (havePreviousLevelQ &&
                std::abs(local.modularityQ - previousLevelQ) <= louvainEpsilon)
            {
                break;
            }

            WeightedMatrix coarsened =
                buildCoarsenedGraph(currentMatrix, local.labels, local.communityCount);
            if (coarsened.size() == currentMatrix.size())
            {
                break;
            }

            result.foldedGraph = true;
            previousLevelQ = local.modularityQ;
            havePreviousLevelQ = true;
            currentMatrix = coarsened;
        }

        result.labels = originalToCurrent;
        result.communityCount = normalizeCommunityLabels(result.labels);
        result.modularityQ = computeModularityQ(torTrafficMatrix, result.labels);
        return result;
    };

    LouvainResult louvainResult{communityPreview.labels,
                                communityPreview.communityCount,
                                0,
                                false,
                                computeModularityQ(torTrafficMatrix, communityPreview.labels),
                                0,
                                false,
                                {}};
    if (communityMode == "louvain")
    {
        louvainResult =
            louvainMode == "multi-level" ? runMultiLevelLouvain() : runSingleLevelLouvain();
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
    for (const auto& edge : candidateEdges)
    {
        if (candidateOcsEdges.size() >= maxSelectedOcsLinks)
        {
            break;
        }

        if (ocsDegree[edge.leafA] < ocsPortK && ocsDegree[edge.leafB] < ocsPortK)
        {
            candidateOcsEdges.push_back(edge);
            ocsDegree[edge.leafA]++;
            ocsDegree[edge.leafB]++;
        }
    }

    auto configScore = [](const std::vector<OcsCandidateEdge>& edges) {
        double score = 0.0;
        for (const auto& edge : edges)
        {
            score += edge.selectionScore;
        }
        return score;
    };

    double candidateConfigScore = configScore(candidateOcsEdges);
    double previousConfigScore = configScore(previousOcsEdges);
    double configScoreImprovement = candidateConfigScore - previousConfigScore;
    bool previousConfigAvailable = !previousOcsEdges.empty();
    bool holdTimeActive =
        enableHoldTimeGate && previousConfigAvailable && previousConfigAge < minHoldCycles;
    std::string configGateDecision = "disabled";
    std::vector<OcsCandidateEdge> selectedOcsEdges;
    if (holdTimeActive)
    {
        selectedOcsEdges = previousOcsEdges;
        configGateDecision = "hold-previous";
    }
    else if (!enableConfigUpdateGate)
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
    std::string finalEpochMatrixMode = trafficMatrixMode;

    auto areEdgeSetsEqual = [](const std::vector<OcsCandidateEdge>& lhs,
                               const std::vector<OcsCandidateEdge>& rhs) {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        std::vector<std::pair<uint32_t, uint32_t>> lhsPairs;
        std::vector<std::pair<uint32_t, uint32_t>> rhsPairs;
        for (const auto& edge : lhs)
        {
            lhsPairs.push_back({std::min(edge.leafA, edge.leafB),
                                std::max(edge.leafA, edge.leafB)});
        }
        for (const auto& edge : rhs)
        {
            rhsPairs.push_back({std::min(edge.leafA, edge.leafB),
                                std::max(edge.leafA, edge.leafB)});
        }
        std::sort(lhsPairs.begin(), lhsPairs.end());
        std::sort(rhsPairs.begin(), rhsPairs.end());
        return lhsPairs == rhsPairs;
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
            const WeightedMatrix savedMatrix = torTrafficMatrix;
            torTrafficMatrix = matrix;
            LouvainResult epochLouvain{epochPreview.labels,
                                       epochPreview.communityCount,
                                       0,
                                       false,
                                       computeModularityQ(matrix, epochPreview.labels),
                                       0,
                                       false,
                                       {}};
            if (communityMode == "louvain")
            {
                epochLouvain = louvainMode == "multi-level" ? runMultiLevelLouvain()
                                                            : runSingleLevelLouvain();
            }
            torTrafficMatrix = savedMatrix;

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
            for (const auto& edge : epochCandidateEdges)
            {
                if (epochCandidateOcsEdges.size() >= maxSelectedOcsLinks)
                {
                    break;
                }
                if (epochOcsDegree[edge.leafA] < ocsPortK &&
                    epochOcsDegree[edge.leafB] < ocsPortK)
                {
                    epochCandidateOcsEdges.push_back(edge);
                    epochOcsDegree[edge.leafA]++;
                    epochOcsDegree[edge.leafB]++;
                }
            }

            const double epochCandidateConfigScore = configScore(epochCandidateOcsEdges);
            const double epochPreviousConfigScore = configScore(epochPreviousEdges);
            const double epochConfigScoreImprovement =
                epochCandidateConfigScore - epochPreviousConfigScore;
            const bool epochPreviousConfigAvailable = !epochPreviousEdges.empty();
            const bool epochHoldTimeActive = enableHoldTimeGate &&
                                             epochPreviousConfigAvailable &&
                                             epochPreviousAge < minHoldCycles;
            std::string epochDecision = "disabled";
            std::vector<OcsCandidateEdge> epochSelectedEdges;
            if (epochHoldTimeActive)
            {
                epochSelectedEdges = epochPreviousEdges;
                epochDecision = "hold-previous";
            }
            else if (!enableConfigUpdateGate)
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
                epochSelectedEdges = epochPreviousEdges;
                epochDecision = "keep-previous";
            }

            const uint32_t epochAgeAfter =
                areEdgeSetsEqual(epochSelectedEdges, epochPreviousEdges) ? epochPreviousAge + 1 : 1;

            ControlEpochSummary summary{epoch,
                                        matrixModeName,
                                        epochCommunityCount,
                                        static_cast<uint32_t>(epochCandidateEdges.size()),
                                        static_cast<uint32_t>(epochCandidateOcsEdges.size()),
                                        static_cast<uint32_t>(epochPreviousEdges.size()),
                                        static_cast<uint32_t>(epochSelectedEdges.size()),
                                        epochCandidateConfigScore,
                                        epochPreviousConfigScore,
                                        epochConfigScoreImprovement,
                                        epochHoldTimeActive,
                                        epochDecision,
                                        epochPreviousAge,
                                        epochAgeAfter};

            return std::make_tuple(epochCandidateEdges,
                                   epochCandidateOcsEdges,
                                   epochPreviousEdges,
                                   epochSelectedEdges,
                                   epochLouvain,
                                   epochPreview,
                                   epochLabels,
                                   epochCommunityCount,
                                   epochCandidateConfigScore,
                                   epochPreviousConfigScore,
                                   epochConfigScoreImprovement,
                                   epochHoldTimeActive,
                                   epochDecision,
                                   epochAgeAfter,
                                   summary);
        };

    if (enableMultiPeriodControl)
    {
        std::vector<OcsCandidateEdge> epochPreviousEdges = previousOcsEdges;
        uint32_t epochPreviousAge = previousConfigAge;
        for (uint32_t epoch = 0; epoch < controlEpochs; ++epoch)
        {
            const std::string epochMatrixMode = getEpochMatrixMode(epoch);
            const WeightedMatrix epochMatrix = buildTrafficMatrix(epochMatrixMode, numLeaves);
            auto epochResult = runControlDecisionForMatrix(epochMatrix,
                                                           epochMatrixMode,
                                                           epochPreviousEdges,
                                                           epochPreviousAge,
                                                           epoch);

            candidateEdges = std::get<0>(epochResult);
            candidateOcsEdges = std::get<1>(epochResult);
            previousOcsEdges = std::get<2>(epochResult);
            selectedOcsEdges = std::get<3>(epochResult);
            louvainResult = std::get<4>(epochResult);
            activeCommunityLabels = std::get<6>(epochResult);
            activeCommunityCount = std::get<7>(epochResult);
            candidateConfigScore = std::get<8>(epochResult);
            previousConfigScore = std::get<9>(epochResult);
            configScoreImprovement = std::get<10>(epochResult);
            holdTimeActive = std::get<11>(epochResult);
            configGateDecision = std::get<12>(epochResult);
            epochPreviousAge = std::get<13>(epochResult);
            controlEpochSummaries.push_back(std::get<14>(epochResult));

            epochPreviousEdges = selectedOcsEdges;
            finalEpochMatrixMode = epochMatrixMode;
            torTrafficMatrix = epochMatrix;
        }

        trafficMatrixMode = finalEpochMatrixMode;
        previousConfigAge =
            controlEpochSummaries.empty() ? previousConfigAge
                                          : controlEpochSummaries.back().previousConfigAgeBefore;
        recomputeTrafficMetrics(torTrafficMatrix);
        previousConfigAvailable = !previousOcsEdges.empty();

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
            ipv4.Assign(devices);
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
    if (routeMode == "ocs-forced")
    {
        Ipv4StaticRoutingHelper staticRoutingHelper;

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
                }
            }

            ocsPairHostRoutes += serversPerLeaf * serversPerLeaf * 4;
        };

        for (const auto& link : installedOcsLinks)
        {
            applyOcsPairHostRoutes(link);
        }

        ocsForced = true;
    }

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
    if (enableMatrixFlows)
    {
        std::vector<std::vector<bool>> matrixFlowPairUsed(numLeaves,
                                                          std::vector<bool>(numLeaves, false));
        for (const auto& link : installedOcsLinks)
        {
            std::ostringstream flowName;
            flowName << "matrix-flow-" << matrixFlowSpecs.size();
            matrixFlowSpecs.push_back({link.leafA,
                                       link.leafB,
                                       0,
                                       0,
                                       static_cast<uint16_t>(matrixFlowPortBase +
                                                             matrixFlowSpecs.size()),
                                       true,
                                       flowName.str()});
            matrixFlowPairUsed[link.leafA][link.leafB] = true;
            matrixFlowPairUsed[link.leafB][link.leafA] = true;
        }

        const size_t minMatrixFlowCount = 3;
        uint32_t residualMatrixFlowsAdded = 0;
        for (uint32_t srcLeaf = 0;
             srcLeaf < numLeaves && matrixFlowSpecs.size() < minMatrixFlowCount;
             ++srcLeaf)
        {
            for (uint32_t dstLeaf = srcLeaf + 1; dstLeaf < numLeaves; ++dstLeaf)
            {
                if (torTrafficMatrix[srcLeaf][dstLeaf] <= 0 ||
                    isOcsPairInstalled(srcLeaf, dstLeaf) ||
                    matrixFlowPairUsed[srcLeaf][dstLeaf])
                {
                    continue;
                }

                std::ostringstream flowName;
                flowName << "matrix-flow-" << matrixFlowSpecs.size();
                matrixFlowSpecs.push_back({srcLeaf,
                                           dstLeaf,
                                           1,
                                           0,
                                           static_cast<uint16_t>(matrixFlowPortBase +
                                                                 matrixFlowSpecs.size()),
                                           false,
                                           flowName.str()});
                matrixFlowPairUsed[srcLeaf][dstLeaf] = true;
                matrixFlowPairUsed[dstLeaf][srcLeaf] = true;
                residualMatrixFlowsAdded++;
                if (matrixFlowSpecs.size() >= minMatrixFlowCount)
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

        matrixFlowStats.resize(matrixFlowSpecs.size(), {0, false, 0.0, 0.0});
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
            const auto& spec = matrixFlowSpecs[flowIndex];
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
            matrixBulkSender.SetAttribute("MaxBytes", UintegerValue(matrixFlowMaxBytes));
            ApplicationContainer bulkApps =
                matrixBulkSender.Install(servers.Get(serverIndex(spec.srcLeaf, spec.srcServer)));
            bulkApps.Start(Seconds(matrixFlowStart + (0.02 * static_cast<double>(flowIndex))));
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

    std::cout << "[HYBRID-DCN][MATRIX] enableMatrixSelect = "
              << (enableMatrixSelect ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][MATRIX] trafficMatrixMode = " << trafficMatrixMode
              << std::endl;
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
    std::cout << "[HYBRID-DCN][CONFIG] candidateConfigScore = "
              << candidateConfigScore << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] previousConfigScore = "
              << previousConfigScore << std::endl;
    std::cout << "[HYBRID-DCN][CONFIG] configScoreImprovement = "
              << configScoreImprovement << std::endl;
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
    const bool isBaseline =
        presetScenario == "baseline-excess" &&
        !(presetOverrideMode == "explicit-wins" && !explicitControlArgNames.empty());

    std::ostringstream enabledModuleSummaryStream;
    enabledModuleSummaryStream << "matrix,"
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
                      << "] communityCount = " << summary.communityCount << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] candidateOcsEdges = " << summary.candidateOcsEdges << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] previousOcsEdges = " << summary.previousOcsEdges << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] selectedOcsEdges = " << summary.selectedOcsEdges << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] candidateConfigScore = " << summary.candidateConfigScore
                      << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] previousConfigScore = " << summary.previousConfigScore << std::endl;
            std::cout << "[HYBRID-DCN][MULTI] epoch[" << summary.epoch
                      << "] configScoreImprovement = " << summary.configScoreImprovement
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
        }
    }

    std::cout << "[HYBRID-DCN][PAPER] paperStage = stage-26-experiment-group-summary"
              << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] experimentGroup = " << experimentGroup
              << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] ablationLevel = " << ablationLevel << std::endl;
    std::cout << "[HYBRID-DCN][PAPER] isMainMethod = "
              << (isMainMethod ? "true" : "false") << std::endl;
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

    std::cout << "[HYBRID-DCN][CONTROL] algorithmStage = stage-20-control-summary"
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] experimentGroup = " << experimentGroup
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] ablationLevel = " << ablationLevel
              << std::endl;
    std::cout << "[HYBRID-DCN][CONTROL] isMainMethod = "
              << (isMainMethod ? "true" : "false") << std::endl;
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
    if (holdTimeActive)
    {
        std::cout << "[HYBRID-DCN][HOLD] note = keeping previous OCS configuration because minHoldCycles is not satisfied"
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

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

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
            if (stats.rxBytes >= matrixFlowMaxBytes)
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
            const bool completed = stats.rxBytes >= matrixFlowMaxBytes;
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

        if (stats.rxBytes >= matrixFlowMaxBytes)
        {
            resultCompletedFlows++;
        }
        resultTotalMatrixRxBytes += stats.rxBytes;
    }

    const double resultEffectiveDuration =
        simTime > matrixFlowStart ? (simTime - matrixFlowStart) : 0.0;
    const double resultMatrixAggregateGoodputMbps =
        resultEffectiveDuration > 0
            ? static_cast<double>(resultTotalMatrixRxBytes) * 8.0 / resultEffectiveDuration / 1e6
            : 0.0;
    const bool resultOcsObservedUse = g_ocsTxPackets > 0;
    const bool resultEpsObservedUse = g_epsTxPackets > 0;
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

    const bool hasCoveredAndResidualFlows =
        resultCoveredFlows > 0 && resultResidualFlows > 0;
    const bool allMatrixFlowsCompleted =
        !matrixFlowSpecs.empty() && resultCompletedFlows == matrixFlowSpecs.size();
    const bool ocsAndEpsBothObserved = resultOcsObservedUse && resultEpsObservedUse;
    const bool dataPlaneValidationPass = allMatrixFlowsCompleted && ocsAndEpsBothObserved;

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
    std::cout << "[HYBRID-DCN][RESULT] isBaseline = "
              << (isBaseline ? "true" : "false") << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] enabledModuleSummary = "
              << enabledModuleSummary << std::endl;
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
    std::cout << "[HYBRID-DCN][RESULT] matrixTotalRxBytes = "
              << resultTotalMatrixRxBytes << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] matrixCoveredRxBytes = "
              << resultCoveredMatrixRxBytes << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] matrixResidualRxBytes = "
              << resultResidualMatrixRxBytes << std::endl;
    std::cout << "[HYBRID-DCN][RESULT] matrixAggregateGoodputMbps = "
              << resultMatrixAggregateGoodputMbps << std::endl;

    for (uint32_t flowIndex = 0; flowIndex < matrixFlowSpecs.size(); ++flowIndex)
    {
        const auto& spec = matrixFlowSpecs[flowIndex];
        const auto& stats = matrixFlowStats[flowIndex];
        const bool completed = stats.rxBytes >= matrixFlowMaxBytes;
        const double firstRx = stats.seenFirstRx ? stats.firstRxTime : 0.0;
        const double lastRx = stats.seenFirstRx ? stats.lastRxTime : 0.0;
        const double duration = lastRx > firstRx ? (lastRx - firstRx) : 0.0;
        const double goodputMbps =
            duration > 0 ? static_cast<double>(stats.rxBytes) * 8.0 / duration / 1e6 : 0.0;
        std::cout << "[HYBRID-DCN][RESULT] matrixFlow[" << flowIndex
                  << "] name=" << spec.name
                  << " pair=" << spec.srcLeaf << "-" << spec.dstLeaf
                  << " ocsCovered=" << (spec.ocsCovered ? "true" : "false")
                  << " rxBytes=" << stats.rxBytes
                  << " completed=" << (completed ? "true" : "false")
                  << " firstRx=" << firstRx
                  << " lastRx=" << lastRx
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
            enableMatrixFlows && resultCoveredFlows > 0 && resultResidualFlows > 0;
        const bool ocsEpsObservationCheck = resultOcsObservedUse && resultEpsObservedUse;
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
                                    configGateDecision == "hold-previous" &&
                                    selectedOcsEdges.size() == 1 &&
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
    }

    std::cout << "[OK] Hybrid Core DCN topology build completed." << std::endl;

    return 0;
}

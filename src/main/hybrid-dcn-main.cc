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

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

uint64_t g_bulkRxBytes = 0;
bool g_bulkSeenFirstRx = false;
double g_bulkFirstRxTime = 0.0;
double g_bulkLastRxTime = 0.0;
uint64_t g_ocsTxPackets = 0;
uint64_t g_ocsTxBytes = 0;
uint64_t g_residualBulkRxBytes = 0;
bool g_residualBulkSeenFirstRx = false;
double g_residualBulkFirstRxTime = 0.0;
double g_residualBulkLastRxTime = 0.0;

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

    const uint32_t bulkSrcLeaf = 0;
    const uint32_t bulkSrcServer = 0;
    const uint32_t bulkDstLeaf = 3;
    const uint32_t bulkDstServer = 0;
    const uint32_t residualBulkSrcLeaf = 1;
    const uint32_t residualBulkSrcServer = 0;
    const uint32_t residualBulkDstLeaf = 2;
    const uint32_t residualBulkDstServer = 0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation time in seconds.", simTime);
    cmd.AddValue("experimentName", "Experiment name.", experimentName);
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
    cmd.Parse(argc, argv);

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

    if (routeMode != "global" && routeMode != "ocs-forced")
    {
        std::cerr << "[HYBRID-DCN][ERROR] routeMode must be global or ocs-forced." << std::endl;
        return 1;
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

    if (enableResidualBulk)
    {
        if (numLeaves < 3)
        {
            std::cerr
                << "[HYBRID-DCN][ERROR] numLeaves must be at least 3 when enableResidualBulk is true."
                << std::endl;
            return 1;
        }

        if (serversPerLeaf < 1)
        {
            std::cerr << "[HYBRID-DCN][ERROR] serversPerLeaf must be at least 1 when "
                         "enableResidualBulk is true."
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
            ipv4.Assign(devices);
            ipv4.NewNetwork();
        }
    }

    Ipv4Address ocsLeafAAddress("0.0.0.0");
    Ipv4Address ocsLeafBAddress("0.0.0.0");
    uint32_t ocsLeafAIfIndex = 0;
    uint32_t ocsLeafBIfIndex = 0;
    NetDeviceContainer ocsDevices;
    if (enableStaticOcs)
    {
        Ipv4InterfaceContainer ocsInterfaces =
            AddOcsLink(leaves.Get(ocsLeafA),
                       leaves.Get(ocsLeafB),
                       ocsDataRate,
                       ocsDelay,
                       ipv4,
                       ocsDevices);
        ocsLeafAAddress = ocsInterfaces.GetAddress(0);
        ocsLeafBAddress = ocsInterfaces.GetAddress(1);
        ocsLeafAIfIndex = ocsInterfaces.Get(0).second;
        ocsLeafBIfIndex = ocsInterfaces.Get(1).second;
        ocsDevices.Get(0)->TraceConnectWithoutContext("MacTx", MakeCallback(&OcsTxTrace));
        ocsDevices.Get(1)->TraceConnectWithoutContext("MacTx", MakeCallback(&OcsTxTrace));
        staticOcsLinks = 1;
        reservedOcsLinks = 1;
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    const Ipv4Address bulkSrcAddress = enableBulk ? serverIpv4[bulkSrcLeaf][bulkSrcServer]
                                                   : Ipv4Address("0.0.0.0");
    const Ipv4Address bulkDstAddress = enableBulk ? serverIpv4[bulkDstLeaf][bulkDstServer]
                                                   : Ipv4Address("0.0.0.0");
    const Ipv4Address residualBulkDstAddress =
        enableResidualBulk ? serverIpv4[residualBulkDstLeaf][residualBulkDstServer]
                           : Ipv4Address("0.0.0.0");
    bool ocsForced = false;
    uint32_t ocsPairHostRoutes = 0;
    if (routeMode == "ocs-forced")
    {
        Ipv4StaticRoutingHelper staticRoutingHelper;

        Ptr<Ipv4StaticRouting> leafARouting =
            staticRoutingHelper.GetStaticRouting(leaves.Get(ocsLeafA)->GetObject<Ipv4>());
        Ptr<Ipv4StaticRouting> leafBRouting =
            staticRoutingHelper.GetStaticRouting(leaves.Get(ocsLeafB)->GetObject<Ipv4>());

        for (uint32_t serverOffsetA = 0; serverOffsetA < serversPerLeaf; ++serverOffsetA)
        {
            Ptr<Ipv4StaticRouting> serverARouting = staticRoutingHelper.GetStaticRouting(
                servers.Get(serverIndex(ocsLeafA, serverOffsetA))->GetObject<Ipv4>());

            for (uint32_t serverOffsetB = 0; serverOffsetB < serversPerLeaf; ++serverOffsetB)
            {
                const Ipv4Address dstB = serverIpv4[ocsLeafB][serverOffsetB];
                const Ipv4Address dstA = serverIpv4[ocsLeafA][serverOffsetA];

                serverARouting->AddHostRouteTo(dstB,
                                               leafServerIpv4[ocsLeafA][serverOffsetA],
                                               serverIfIndex[ocsLeafA][serverOffsetA]);
                leafARouting->AddHostRouteTo(dstB, ocsLeafBAddress, ocsLeafAIfIndex);

                leafBRouting->AddHostRouteTo(dstA, ocsLeafAAddress, ocsLeafBIfIndex);

                Ptr<Ipv4StaticRouting> serverBRouting = staticRoutingHelper.GetStaticRouting(
                    servers.Get(serverIndex(ocsLeafB, serverOffsetB))->GetObject<Ipv4>());
                serverBRouting->AddHostRouteTo(dstA,
                                               leafServerIpv4[ocsLeafB][serverOffsetB],
                                               serverIfIndex[ocsLeafB][serverOffsetB]);
            }
        }

        ocsForced = true;
        ocsPairHostRoutes = serversPerLeaf * serversPerLeaf * 4;
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
    const std::string residualBulkSrcName = "server-l1-s0";
    const std::string residualBulkDstName = "server-l2-s0";

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
    std::cout << "[HYBRID-DCN][ROUTE] ocsPairHostRoutes = " << ocsPairHostRoutes
              << std::endl;

    AnimationInterface anim("../sim/results/raw/hybrid-dcn-anim.xml");
    anim.EnablePacketMetadata(true);
    if (enableStaticOcs)
    {
        std::ostringstream leafADescription;
        leafADescription << "leaf-" << ocsLeafA << "-OCS";
        anim.UpdateNodeDescription(leaves.Get(ocsLeafA), leafADescription.str());

        std::ostringstream leafBDescription;
        leafBDescription << "leaf-" << ocsLeafB << "-OCS";
        anim.UpdateNodeDescription(leaves.Get(ocsLeafB), leafBDescription.str());
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

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

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

    std::cout << "[HYBRID-DCN][OCS] enabled     = " << (enableStaticOcs ? "true" : "false")
              << std::endl;
    std::cout << "[HYBRID-DCN][OCS] txPackets   = " << g_ocsTxPackets << std::endl;
    std::cout << "[HYBRID-DCN][OCS] txBytes     = " << g_ocsTxBytes << std::endl;
    std::cout << "[HYBRID-DCN][OCS] observedUse = "
              << (g_ocsTxPackets > 0 ? "true" : "false") << std::endl;

    std::cout << "[OK] Hybrid Core DCN topology build completed." << std::endl;

    return 0;
}

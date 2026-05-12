#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/netanim-module.h"
#include "ns3/point-to-point-module.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

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

void
AddOcsLink(Ptr<Node> leafA, Ptr<Node> leafB)
{
    (void)leafA;
    (void)leafB;

    // Stage-1.5 does not instantiate OCS links.
    // Later stages will use this interface to install Leaf-To-Leaf direct L1
    // lightpaths with DataRate = 100Gbps and Delay = 5us.
    // The graph clustering and cross-scale cooperative scheduling modules will
    // call this interface when dynamic optical circuit setup is introduced.
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

    const uint32_t totalServers = numLeaves * serversPerLeaf;
    const uint32_t totalNodes = totalServers + numLeaves + numSpines;
    const uint32_t serverLeafLinks = totalServers;
    const uint32_t leafSpineLinks = numLeaves * numSpines;
    const uint32_t epsLinksCount = serverLeafLinks + leafSpineLinks;
    const uint32_t reservedOcsLinks = 0;

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

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    const uint32_t echoPort = 9000;
    const uint32_t echoSrcLeaf = 0;
    const uint32_t echoSrcServer = 0;
    const uint32_t echoDstLeaf = 3;
    const uint32_t echoDstServer = 0;
    const std::string echoSrcName = "server-l0-s0";
    const std::string echoDstName = "server-l3-s0";
    const Ipv4Address echoDstAddress = enableEcho ? serverIpv4[echoDstLeaf][echoDstServer]
                                                   : Ipv4Address("0.0.0.0");

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

    AnimationInterface anim("../sim/results/raw/hybrid-dcn-anim.xml");
    anim.EnablePacketMetadata(true);

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

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "[OK] Hybrid Core DCN topology build completed." << std::endl;

    return 0;
}

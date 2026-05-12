#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <iostream>
#include <sstream>
#include <string>

using namespace ns3;

void
AddOcsLink(Ptr<Node> leafA, Ptr<Node> leafB)
{
    (void)leafA;
    (void)leafB;

    // Reserved OCS L1 lightpath interface between two Leaf/ToR switches.
    // Future implementation should install a direct P2P link with:
    // DataRate = 100Gbps, Delay = 5us.
}

int
main(int argc, char* argv[])
{
    double simTime = 1.0;
    std::string experimentName = "stage-1-topology";
    uint32_t numSpines = 2;
    uint32_t numLeaves = 4;
    uint32_t serversPerLeaf = 4;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation time in seconds.", simTime);
    cmd.AddValue("experimentName", "Experiment name.", experimentName);
    cmd.AddValue("numSpines", "Number of EPS electrical core spine switches.", numSpines);
    cmd.AddValue("numLeaves", "Number of Leaf/ToR switches.", numLeaves);
    cmd.AddValue("serversPerLeaf", "Number of servers attached to each Leaf/ToR.", serversPerLeaf);
    cmd.Parse(argc, argv);

    const uint32_t totalServers = numLeaves * serversPerLeaf;
    const uint32_t totalNodes = totalServers + numLeaves + numSpines;
    const uint32_t epsLinksCount = totalServers + (numLeaves * numSpines);

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

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.0.0.0", "255.255.255.252");

    for (uint32_t leafIndex = 0; leafIndex < numLeaves; ++leafIndex)
    {
        for (uint32_t serverOffset = 0; serverOffset < serversPerLeaf; ++serverOffset)
        {
            NodeContainer pair(servers.Get(serverIndex(leafIndex, serverOffset)),
                               leaves.Get(leafIndex));
            NetDeviceContainer devices = serverLeafP2p.Install(pair);
            ipv4.Assign(devices);
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

    std::cout << "[HYBRID-DCN] experimentName=" << experimentName << std::endl;
    std::cout << "[HYBRID-DCN] simTime=" << simTime << "s" << std::endl;
    std::cout << "[HYBRID-DCN] numSpines=" << numSpines << std::endl;
    std::cout << "[HYBRID-DCN] numLeaves=" << numLeaves << std::endl;
    std::cout << "[HYBRID-DCN] serversPerLeaf=" << serversPerLeaf << std::endl;
    std::cout << "[HYBRID-DCN] totalServers=" << totalServers << std::endl;
    std::cout << "[HYBRID-DCN] totalNodes=" << totalNodes << std::endl;
    std::cout << "[HYBRID-DCN] epsLinksCount=" << epsLinksCount << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "[OK] Hybrid Core DCN topology build completed." << std::endl;

    return 0;
}

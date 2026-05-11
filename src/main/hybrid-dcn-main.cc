#include "ns3/core-module.h"

#include <iostream>
#include <string>

using namespace ns3;

int
main(int argc, char* argv[])
{
    double simTime = 1.0;
    std::string experimentName = "stage-0c-external-project";

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation time in seconds.", simTime);
    cmd.AddValue("experimentName", "Experiment name.", experimentName);
    cmd.Parse(argc, argv);

    std::cout << "[HYBRID-DCN] Experiment: " << experimentName << std::endl;
    std::cout << "[HYBRID-DCN] Project root: ~/sim" << std::endl;
    std::cout << "[HYBRID-DCN] ns-3 engine: ~/ns-3.47" << std::endl;
    std::cout << "[HYBRID-DCN] Stage: 0C external project skeleton" << std::endl;
    std::cout << "[HYBRID-DCN] Simulation time: " << simTime << " s" << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "[HYBRID-DCN] Simulation completed." << std::endl;

    return 0;
}

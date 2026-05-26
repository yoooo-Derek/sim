#ifndef SIM_SRC_METRICS_TRACE_METRICS_H
#define SIM_SRC_METRICS_TRACE_METRICS_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"

#include <cstdint>
#include <string>
#include <vector>

struct MatrixBulkFlowStats
{
    uint64_t rxBytes;
    bool seenFirstRx;
    double firstRxTime;
    double lastRxTime;
};

struct LinkCounter
{
    std::string linkId;
    std::string linkType;
    std::string direction;
    std::string endpointAType;
    uint32_t endpointA;
    std::string endpointBType;
    uint32_t endpointB;
    double capacityGbps;
    std::string delay;
    uint64_t txPackets;
    uint64_t txBytes;
    uint64_t lastSampleTxPackets;
    uint64_t lastSampleTxBytes;
    double lastSampleTime;
    std::string note;
};

struct LinkUtilizationSample
{
    uint32_t sampleIndex;
    uint32_t linkIndex;
    double sampleTimeSeconds;
    double intervalSeconds;
    uint64_t deltaTxPackets;
    uint64_t deltaTxBytes;
    uint64_t cumulativeTxPackets;
    uint64_t cumulativeTxBytes;
    double sampleThroughputMbps;
    double utilizationApprox;
};

struct MeasuredEpsDirectedLinkSnapshot
{
    bool hasSample;
    double lastSampleTime;
    double utilizationApprox;
    double sampleThroughputMbps;
    uint64_t deltaTxBytes;
    uint64_t cumulativeTxBytes;
    double capacityMbps;
    uint64_t updateCount;
};

inline void
MatrixBulkSinkRxTrace(std::vector<MatrixBulkFlowStats>* stats,
                      uint32_t flowIndex,
                      ns3::Ptr<const ns3::Packet> packet,
                      const ns3::Address& from)
{
    (void)from;
    if (stats == nullptr || flowIndex >= stats->size())
    {
        return;
    }

    MatrixBulkFlowStats& flowStats = stats->at(flowIndex);
    flowStats.rxBytes += packet->GetSize();

    const double now = ns3::Simulator::Now().GetSeconds();
    if (!flowStats.seenFirstRx)
    {
        flowStats.seenFirstRx = true;
        flowStats.firstRxTime = now;
    }
    flowStats.lastRxTime = now;
}

inline void
LinkTxTrace(std::vector<LinkCounter>* counters,
            uint32_t counterIndex,
            ns3::Ptr<const ns3::Packet> packet)
{
    if (counters == nullptr || counterIndex >= counters->size())
    {
        return;
    }

    LinkCounter& counter = counters->at(counterIndex);
    counter.txPackets += 1;
    counter.txBytes += packet->GetSize();
}

inline void
SampleLinkUtilizationTimeSeries(std::vector<LinkCounter>* counters,
                                std::vector<LinkUtilizationSample>* samples,
                                std::vector<std::vector<MeasuredEpsDirectedLinkSnapshot>>*
                                    epsLeafToSpineSnapshots,
                                std::vector<std::vector<MeasuredEpsDirectedLinkSnapshot>>*
                                    epsSpineToLeafSnapshots,
                                uint64_t* measuredSnapshotUpdateCount,
                                double sampleIntervalSeconds,
                                double stopTimeSeconds,
                                uint32_t* nextSampleIndex)
{
    if (counters == nullptr || samples == nullptr || nextSampleIndex == nullptr ||
        sampleIntervalSeconds <= 0.0)
    {
        return;
    }

    const double now = ns3::Simulator::Now().GetSeconds();
    const uint32_t sampleIndex = *nextSampleIndex;
    *nextSampleIndex += 1;

    for (uint32_t linkIndex = 0; linkIndex < counters->size(); ++linkIndex)
    {
        LinkCounter& counter = counters->at(linkIndex);
        const double intervalSeconds = now - counter.lastSampleTime;
        const uint64_t deltaTxPackets =
            counter.txPackets >= counter.lastSampleTxPackets
                ? counter.txPackets - counter.lastSampleTxPackets
                : 0;
        const uint64_t deltaTxBytes =
            counter.txBytes >= counter.lastSampleTxBytes
                ? counter.txBytes - counter.lastSampleTxBytes
                : 0;
        const double sampleThroughputMbps =
            intervalSeconds > 0.0
                ? static_cast<double>(deltaTxBytes) * 8.0 / intervalSeconds / 1e6
                : 0.0;
        const double capacityMbps = counter.capacityGbps * 1000.0;
        const double utilizationApprox =
            capacityMbps > 0.0 ? sampleThroughputMbps / capacityMbps : 0.0;

        samples->push_back({sampleIndex,
                            linkIndex,
                            now,
                            intervalSeconds,
                            deltaTxPackets,
                            deltaTxBytes,
                            counter.txPackets,
                            counter.txBytes,
                            sampleThroughputMbps,
                            utilizationApprox});

        if (counter.linkType == "eps-leaf-spine" &&
            epsLeafToSpineSnapshots != nullptr &&
            epsSpineToLeafSnapshots != nullptr)
        {
            MeasuredEpsDirectedLinkSnapshot* snapshot = nullptr;
            if (counter.endpointAType == "leaf" && counter.endpointBType == "spine" &&
                counter.endpointA < epsLeafToSpineSnapshots->size() &&
                counter.endpointB < epsLeafToSpineSnapshots->at(counter.endpointA).size())
            {
                snapshot = &epsLeafToSpineSnapshots->at(counter.endpointA).at(counter.endpointB);
            }
            else if (counter.endpointAType == "spine" && counter.endpointBType == "leaf" &&
                     counter.endpointB < epsSpineToLeafSnapshots->size() &&
                     counter.endpointA < epsSpineToLeafSnapshots->at(counter.endpointB).size())
            {
                snapshot = &epsSpineToLeafSnapshots->at(counter.endpointB).at(counter.endpointA);
            }

            if (snapshot != nullptr)
            {
                snapshot->hasSample = true;
                snapshot->lastSampleTime = now;
                snapshot->utilizationApprox = utilizationApprox;
                snapshot->sampleThroughputMbps = sampleThroughputMbps;
                snapshot->deltaTxBytes = deltaTxBytes;
                snapshot->cumulativeTxBytes = counter.txBytes;
                snapshot->capacityMbps = capacityMbps;
                snapshot->updateCount++;
                if (measuredSnapshotUpdateCount != nullptr)
                {
                    *measuredSnapshotUpdateCount += 1;
                }
            }
        }

        counter.lastSampleTxPackets = counter.txPackets;
        counter.lastSampleTxBytes = counter.txBytes;
        counter.lastSampleTime = now;
    }

    if (now + sampleIntervalSeconds <= stopTimeSeconds + 1e-9)
    {
        ns3::Simulator::Schedule(ns3::Seconds(sampleIntervalSeconds),
                                 &SampleLinkUtilizationTimeSeries,
                                 counters,
                                 samples,
                                 epsLeafToSpineSnapshots,
                                 epsSpineToLeafSnapshots,
                                 measuredSnapshotUpdateCount,
                                 sampleIntervalSeconds,
                                 stopTimeSeconds,
                                 nextSampleIndex);
    }
}

#endif

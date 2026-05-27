#ifndef SIM_SRC_RESULT_STRUCTURED_RESULT_SCHEMA_H
#define SIM_SRC_RESULT_STRUCTURED_RESULT_SCHEMA_H

#include <string>
#include <vector>

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

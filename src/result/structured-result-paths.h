#ifndef SIM_SRC_RESULT_STRUCTURED_RESULT_PATHS_H
#define SIM_SRC_RESULT_STRUCTURED_RESULT_PATHS_H

#include "csv-utils.h"

#include <string>

struct StructuredResultCsvPaths
{
    std::string summaryCsvPath;
    std::string flowsCsvPath;
    std::string wecmpCsvPath;
    std::string ocsCandidatesCsvPath;
    std::string linksCsvPath;
    std::string linkTimeseriesCsvPath;
    std::string measuredWecmpCsvPath;
};

inline StructuredResultCsvPaths
BuildStructuredResultCsvPaths(const std::string& structuredResultDir,
                              const std::string& experimentName)
{
    return {joinCsvPath(structuredResultDir, experimentName + "-summary.csv"),
            joinCsvPath(structuredResultDir, experimentName + "-flows.csv"),
            joinCsvPath(structuredResultDir, experimentName + "-wecmp.csv"),
            joinCsvPath(structuredResultDir, experimentName + "-ocs-candidates.csv"),
            joinCsvPath(structuredResultDir, experimentName + "-links.csv"),
            joinCsvPath(structuredResultDir, experimentName + "-link-timeseries.csv"),
            joinCsvPath(structuredResultDir, experimentName + "-measured-wecmp.csv")};
}

#endif

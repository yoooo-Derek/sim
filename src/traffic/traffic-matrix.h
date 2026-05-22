#ifndef HYBRID_DCN_TRAFFIC_MATRIX_H
#define HYBRID_DCN_TRAFFIC_MATRIX_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using WeightedMatrix = std::vector<std::vector<double>>;
using DirectedTrafficMatrix = WeightedMatrix;

inline WeightedMatrix
buildSyntheticUndirectedTrafficMatrix(const std::string& mode, uint32_t leafCount)
{
    WeightedMatrix matrix(leafCount, std::vector<double>(leafCount, 0.0));

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
}

inline DirectedTrafficMatrix
buildSyntheticDirectedTrafficMatrix(const std::string& mode, uint32_t leafCount)
{
    const WeightedMatrix undirected = buildSyntheticUndirectedTrafficMatrix(mode, leafCount);
    DirectedTrafficMatrix directed(leafCount, std::vector<double>(leafCount, 0.0));

    for (uint32_t i = 0; i < leafCount; ++i)
    {
        for (uint32_t j = i + 1; j < leafCount; ++j)
        {
            const double directionalDemand = std::max(undirected[i][j], 0.0) * 0.5;
            directed[i][j] = directionalDemand;
            directed[j][i] = directionalDemand;
        }
    }

    return directed;
}

inline WeightedMatrix
buildUndirectedCommunicationIntensityMatrix(const DirectedTrafficMatrix& directed)
{
    const uint32_t size = static_cast<uint32_t>(directed.size());
    WeightedMatrix undirected(size, std::vector<double>(size, 0.0));

    for (uint32_t i = 0; i < size; ++i)
    {
        for (uint32_t j = i + 1; j < size; ++j)
        {
            double reverse = 0.0;
            if (j < directed.size() && i < directed[j].size())
            {
                reverse = directed[j][i];
            }
            const double forward = j < directed[i].size() ? directed[i][j] : 0.0;
            const double communicationIntensity = std::max(forward + reverse, 0.0);
            undirected[i][j] = communicationIntensity;
            undirected[j][i] = communicationIntensity;
        }
    }

    return undirected;
}

inline WeightedMatrix
applyTrafficGraphThreshold(const WeightedMatrix& matrix, double threshold)
{
    WeightedMatrix filtered(matrix.size(), std::vector<double>(matrix.size(), 0.0));
    const double effectiveThreshold = std::max(threshold, 0.0);

    for (uint32_t i = 0; i < matrix.size(); ++i)
    {
        for (uint32_t j = i + 1; j < matrix[i].size(); ++j)
        {
            const double value = matrix[i][j] >= effectiveThreshold ? matrix[i][j] : 0.0;
            filtered[i][j] = value;
            if (j < filtered.size() && i < filtered[j].size())
            {
                filtered[j][i] = value;
            }
        }
    }

    return filtered;
}

inline WeightedMatrix
updateEwmaMatrix(const WeightedMatrix& previousAbar,
                 const WeightedMatrix& currentA,
                 double beta,
                 bool hasPreviousAbar)
{
    if (!hasPreviousAbar)
    {
        return currentA;
    }

    WeightedMatrix abar(currentA.size(), std::vector<double>(currentA.size(), 0.0));
    for (uint32_t i = 0; i < currentA.size(); ++i)
    {
        for (uint32_t j = i + 1; j < currentA[i].size(); ++j)
        {
            double previous = 0.0;
            if (i < previousAbar.size() && j < previousAbar[i].size())
            {
                previous = previousAbar[i][j];
            }
            const double value = std::max(beta * previous + (1.0 - beta) * currentA[i][j], 0.0);
            abar[i][j] = value;
            abar[j][i] = value;
        }
    }
    return abar;
}

inline std::vector<double>
computeNodeDegree(const WeightedMatrix& matrix)
{
    std::vector<double> degree(matrix.size(), 0.0);
    for (uint32_t i = 0; i < matrix.size(); ++i)
    {
        for (uint32_t j = 0; j < matrix[i].size(); ++j)
        {
            degree[i] += matrix[i][j];
        }
    }
    return degree;
}

inline double
computeTotalTraffic(const WeightedMatrix& matrix)
{
    double traffic = 0.0;
    for (uint32_t i = 0; i < matrix.size(); ++i)
    {
        for (uint32_t j = 0; j < matrix[i].size(); ++j)
        {
            traffic += matrix[i][j];
        }
    }
    return traffic * 0.5;
}

#endif // HYBRID_DCN_TRAFFIC_MATRIX_H

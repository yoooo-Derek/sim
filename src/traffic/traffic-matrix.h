#ifndef HYBRID_DCN_TRAFFIC_MATRIX_H
#define HYBRID_DCN_TRAFFIC_MATRIX_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using WeightedMatrix = std::vector<std::vector<double>>;

// Synthetic source currently emits the undirected ToR-level communication
// intensity matrix A(t), not a directed packet/byte matrix W(t).
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

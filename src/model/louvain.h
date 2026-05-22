#ifndef HYBRID_DCN_LOUVAIN_H
#define HYBRID_DCN_LOUVAIN_H

#include "../traffic/traffic-matrix.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

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

inline CommunityPreview
buildCommunityPreview(const std::string& mode, uint32_t leafCount)
{
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
}

inline uint32_t
normalizeCommunityLabels(std::vector<uint32_t>& labels)
{
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
}

inline double
computeModularityQ(const WeightedMatrix& matrix, const std::vector<uint32_t>& labels, double eta)
{
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
}

inline LouvainResult
runLocalMoving(const WeightedMatrix& matrix,
               uint32_t louvainMaxPasses,
               double louvainEpsilon,
               double eta)
{
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
    result.modularityQ = computeModularityQ(matrix, result.labels, eta);
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

            const double currentQ = computeModularityQ(matrix, result.labels, eta);
            double bestQ = currentQ;
            uint32_t bestLabel = oldLabel;
            std::vector<uint32_t> bestLabels = result.labels;

            for (uint32_t candidateLabel : candidateLabels)
            {
                std::vector<uint32_t> trialLabels = result.labels;
                trialLabels[nodeIndex] = candidateLabel;
                normalizeCommunityLabels(trialLabels);
                const double trialQ = computeModularityQ(matrix, trialLabels, eta);
                const uint32_t trialLabel = trialLabels[nodeIndex];

                if (trialQ > bestQ + louvainEpsilon ||
                    (std::abs(trialQ - bestQ) <= louvainEpsilon && trialLabel < bestLabel))
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
    result.modularityQ = computeModularityQ(matrix, result.labels, eta);
    result.levelSummaries.push_back(LouvainLevelSummary{0,
                                                        nodeCount,
                                                        result.communityCount,
                                                        result.communityCount,
                                                        result.passes,
                                                        result.moved,
                                                        result.modularityQ});
    return result;
}

inline WeightedMatrix
buildCoarsenedGraph(const WeightedMatrix& matrix,
                    const std::vector<uint32_t>& labels,
                    uint32_t communityCount)
{
    WeightedMatrix coarsened(communityCount, std::vector<double>(communityCount, 0.0));
    for (uint32_t i = 0; i < matrix.size(); ++i)
    {
        for (uint32_t j = 0; j < matrix[i].size(); ++j)
        {
            coarsened[labels[i]][labels[j]] += matrix[i][j];
        }
    }
    return coarsened;
}

inline LouvainResult
runSingleLevelLouvain(const WeightedMatrix& matrix,
                      uint32_t numLeaves,
                      uint32_t louvainMaxPasses,
                      double louvainEpsilon,
                      double eta)
{
    LouvainResult result = runLocalMoving(matrix, louvainMaxPasses, louvainEpsilon, eta);
    result.modularityQ = computeModularityQ(matrix, result.labels, eta);
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
}

inline LouvainResult
runMultiLevelLouvain(const WeightedMatrix& matrix,
                     uint32_t numLeaves,
                     uint32_t louvainMaxPasses,
                     uint32_t louvainMaxLevels,
                     double louvainEpsilon,
                     double eta)
{
    WeightedMatrix currentMatrix = matrix;
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
    result.modularityQ = computeModularityQ(matrix, result.labels, eta);
    result.levels = 0;
    result.foldedGraph = false;

    double previousLevelQ = 0.0;
    bool havePreviousLevelQ = false;
    for (uint32_t level = 0; level < louvainMaxLevels; ++level)
    {
        LouvainResult local = runLocalMoving(currentMatrix, louvainMaxPasses, louvainEpsilon, eta);
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

        if (havePreviousLevelQ && std::abs(local.modularityQ - previousLevelQ) <= louvainEpsilon)
        {
            break;
        }

        WeightedMatrix coarsened = buildCoarsenedGraph(currentMatrix,
                                                       local.labels,
                                                       local.communityCount);
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
    result.modularityQ = computeModularityQ(matrix, result.labels, eta);
    return result;
}

#endif // HYBRID_DCN_LOUVAIN_H

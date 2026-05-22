#ifndef HYBRID_DCN_OCS_STATE_H
#define HYBRID_DCN_OCS_STATE_H

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

using OcsEdgeAgeMatrix = std::vector<std::vector<uint32_t>>;

struct OcsCandidateEdge
{
    uint32_t leafA;
    uint32_t leafB;
    double traffic;
    double expected;
    double modularityGain;
    double utility;
    double baseUtility;
    double communityFactor;
    double communityUtility;
    bool intraCommunity;
    bool wasPreviouslyInstalled;
    double stateHoldingGain;
    double selectionScore;
};

inline std::pair<uint32_t, uint32_t>
NormalizeEdgePair(uint32_t leafA, uint32_t leafB)
{
    return {std::min(leafA, leafB), std::max(leafA, leafB)};
}

inline OcsEdgeAgeMatrix
MakeZeroOcsEdgeAgeMatrix(uint32_t numLeaves)
{
    return OcsEdgeAgeMatrix(numLeaves, std::vector<uint32_t>(numLeaves, 0));
}

inline uint32_t
GetOcsEdgeAge(const OcsEdgeAgeMatrix& ageMatrix, uint32_t leafA, uint32_t leafB)
{
    if (ageMatrix.empty() || leafA >= ageMatrix.size() || leafB >= ageMatrix.size())
    {
        return 0;
    }
    const auto pair = NormalizeEdgePair(leafA, leafB);
    return ageMatrix[pair.first][pair.second];
}

inline void
SetOcsEdgeAge(OcsEdgeAgeMatrix& ageMatrix, uint32_t leafA, uint32_t leafB, uint32_t age)
{
    if (ageMatrix.empty() || leafA >= ageMatrix.size() || leafB >= ageMatrix.size())
    {
        return;
    }
    const auto pair = NormalizeEdgePair(leafA, leafB);
    ageMatrix[pair.first][pair.second] = age;
    ageMatrix[pair.second][pair.first] = age;
}

inline bool
IsOcsEdgeInSet(const std::vector<OcsCandidateEdge>& edges, uint32_t leafA, uint32_t leafB)
{
    for (const auto& edge : edges)
    {
        if ((edge.leafA == leafA && edge.leafB == leafB) ||
            (edge.leafA == leafB && edge.leafB == leafA))
        {
            return true;
        }
    }
    return false;
}

inline OcsEdgeAgeMatrix
UpdateOcsEdgeAges(const OcsEdgeAgeMatrix& previousAgeMatrix,
                  const std::vector<OcsCandidateEdge>& previousEdges,
                  const std::vector<OcsCandidateEdge>& selectedEdges)
{
    OcsEdgeAgeMatrix nextAgeMatrix = MakeZeroOcsEdgeAgeMatrix(previousAgeMatrix.size());
    for (const auto& edge : selectedEdges)
    {
        const bool wasSelectedPreviously =
            IsOcsEdgeInSet(previousEdges, edge.leafA, edge.leafB);
        const uint32_t previousAge =
            wasSelectedPreviously ? GetOcsEdgeAge(previousAgeMatrix, edge.leafA, edge.leafB) : 0;
        SetOcsEdgeAge(nextAgeMatrix,
                      edge.leafA,
                      edge.leafB,
                      wasSelectedPreviously ? previousAge + 1 : 1);
    }
    return nextAgeMatrix;
}

inline std::pair<uint32_t, uint32_t>
GetOcsEdgeAgeRange(const OcsEdgeAgeMatrix& ageMatrix,
                   const std::vector<OcsCandidateEdge>& edges)
{
    if (edges.empty())
    {
        return {0, 0};
    }

    uint32_t minAge = std::numeric_limits<uint32_t>::max();
    uint32_t maxAge = 0;
    for (const auto& edge : edges)
    {
        const uint32_t age = GetOcsEdgeAge(ageMatrix, edge.leafA, edge.leafB);
        minAge = std::min(minAge, age);
        maxAge = std::max(maxAge, age);
    }
    return {minAge, maxAge};
}

#endif // HYBRID_DCN_OCS_STATE_H

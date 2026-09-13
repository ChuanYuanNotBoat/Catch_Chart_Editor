#include "BpmMeasureUtils.h"

#include "utils/MathUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
    bool isPositiveFinite(double value)
    {
        return std::isfinite(value) && value > 0.0;
    }
}

BpmMeasureUtils::Recommendation BpmMeasureUtils::selectRecommendation(
    const BpmDetector::DetectionResult &result)
{
    Recommendation recommendation;
    if (!result.hasAnalysis())
        return recommendation;

    double bestScore = -std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i < result.analysis.tempoCandidates.size(); ++i)
    {
        const AutoTiming2Candidate &candidate = result.analysis.tempoCandidates[i];
        if (!isPositiveFinite(candidate.bpm))
            continue;
        const double comparableScore = std::isfinite(candidate.score)
                                           ? candidate.score
                                           : -std::numeric_limits<double>::infinity();
        if (!recommendation.available || comparableScore > bestScore)
        {
            recommendation.available = true;
            recommendation.bpm = candidate.bpm;
            recommendation.candidateScore = std::isfinite(candidate.score) ? candidate.score : 0.0;
            recommendation.candidateIndex = i;
            recommendation.familyId = candidate.harmonicFamilyId;
            bestScore = comparableScore;
        }
    }

    if (!recommendation.available)
        return recommendation;

    for (const AutoTiming2Family &family : result.analysis.tempoFamilies)
    {
        if (family.id != recommendation.familyId)
            continue;

        recommendation.familyReferenceBpm = family.referenceBpm;
        recommendation.familyScore = family.relativeScore;
        for (qsizetype candidateIndex : family.memberCandidateIndices)
        {
            if (candidateIndex < 0 || candidateIndex >= result.analysis.tempoCandidates.size())
                continue;
            const double familyBpm = result.analysis.tempoCandidates[candidateIndex].bpm;
            if (!isPositiveFinite(familyBpm))
                continue;
            const bool alreadyPresent = std::any_of(
                recommendation.familyBpms.cbegin(),
                recommendation.familyBpms.cend(),
                [familyBpm](double existing)
                {
                    return std::fabs(existing - familyBpm) <= 1e-6;
                });
            if (!alreadyPresent)
                recommendation.familyBpms.append(familyBpm);
        }
        std::sort(recommendation.familyBpms.begin(), recommendation.familyBpms.end());
        break;
    }

    const AutoTiming2Confidence &confidence = result.analysis.confidence;
    recommendation.quality = confidence.overall >= kSupportedOverallConfidence &&
                                     confidence.tempo >= kSupportedTempoConfidence &&
                                     confidence.reliableCoverage >= kMinimumReliableCoverage
                                 ? EvidenceQuality::Supported
                                 : EvidenceQuality::Uncertain;
    return recommendation;
}

BpmMeasureUtils::MultiplierHint BpmMeasureUtils::findMultiplierHint(double legacyBpm,
                                                                    double suggestedBpm)
{
    MultiplierHint hint;
    if (!isPositiveFinite(legacyBpm) || !isPositiveFinite(suggestedBpm))
        return hint;

    static constexpr std::array<int, 5> kAvailableFactors = {2, 3, 4, 6, 8};
    for (int factor : kAvailableFactors)
    {
        const double multiplied = legacyBpm * static_cast<double>(factor);
        const double relativeError = std::fabs(multiplied - suggestedBpm) / suggestedBpm;
        if (relativeError > kMultiplierRelativeTolerance)
            continue;

        hint.factor = factor;
        hint.relation = factor == 2 || factor == 4 || factor == 8
                            ? MultiplierRelation::PowerOfTwo
                            : MultiplierRelation::NumericalOnly;
        return hint;
    }
    return hint;
}

BpmEntry BpmMeasureUtils::makeTargetEntry(bool fromStart,
                                          double chartTimeMs,
                                          const QVector<BpmEntry> &bpmList,
                                          int offsetMs,
                                          double bpm)
{
    if (fromStart)
        return BpmEntry(0, 0, 1, bpm);

    int beat = 0;
    int numerator = 0;
    int denominator = 1;
    MathUtils::msToBeat(chartTimeMs, bpmList, offsetMs, beat, numerator, denominator);
    return BpmEntry(beat, numerator, denominator, bpm);
}

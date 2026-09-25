#include "BpmMeasureUtils.h"

#include "utils/MathUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
    constexpr double kTwoPi = 6.28318530717958647692;

    bool isPositiveFinite(double value)
    {
        return std::isfinite(value) && value > 0.0;
    }

    struct ProjectedEntry
    {
        double beat = 0.0;
        double bpm = 0.0;
    };

    double beatPosition(const BpmEntry &entry)
    {
        return entry.beatNum + static_cast<double>(entry.numerator) / entry.denominator;
    }

    bool samePosition(double left, double right)
    {
        return std::fabs(left - right) <= 1.0e-8;
    }

    double wrapPositive(double value, double period)
    {
        double wrapped = std::fmod(value, period);
        if (wrapped < 0.0)
            wrapped += period;
        return wrapped;
    }

    void appendProjectedEntry(QVector<ProjectedEntry> &entries, double beat, double bpm)
    {
        if (!std::isfinite(beat) || !isPositiveFinite(bpm))
            return;
        if (!entries.isEmpty() && samePosition(entries.last().beat, beat))
        {
            entries.last().bpm = bpm;
            return;
        }
        if (!entries.isEmpty())
        {
            const double relative = std::fabs(entries.last().bpm / bpm - 1.0);
            if (relative <= 1.0e-7)
                return;
        }
        entries.append({beat, bpm});
    }

    BpmEntry makeBpmEntry(double beat, double bpm, int maximumDenominator)
    {
        int beatNum = 0;
        int numerator = 0;
        int denominator = 1;
        MathUtils::floatToBeat(
            beat,
            beatNum,
            numerator,
            denominator,
            maximumDenominator);
        return BpmEntry(beatNum, numerator, denominator, bpm);
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

BpmMeasureUtils::TimingMapProposal BpmMeasureUtils::buildTimingMapProposal(
    const AutoTiming2Summary &summary,
    const QVector<BpmEntry> &existingBpmList,
    int offsetMs,
    const TimingMapOptions &options)
{
    TimingMapProposal proposal;
    if (existingBpmList.isEmpty())
    {
        proposal.unavailableReason = QStringLiteral("The chart has no BPM entry to anchor the detected map.");
        return proposal;
    }
    if (!(options.maximumModelErrorMs > 0.0) ||
        !(options.maximumAcceptedAnchorResidualMs > 0.0) ||
        options.maximumEntries < 1 || options.maximumBeatDenominator < 1)
    {
        proposal.unavailableReason = QStringLiteral("Invalid timing-map projection options.");
        return proposal;
    }

    const AutoTiming2TempoMap &map = summary.tempoMap;
    proposal.sourceAnchorCount = map.anchors.size();
    proposal.sourceStartSeconds = map.startSeconds;
    proposal.sourceEndSeconds = map.endSeconds;
    proposal.maximumModelErrorMs = map.maximumBpmListModelErrorMilliseconds;
    proposal.hasTempoChange = map.hasTempoChange;
    proposal.hasContinuousChange = map.hasContinuousChange;
    proposal.hasAbruptChange = map.hasAbruptChange;
    if (!map.available)
    {
        proposal.unavailableReason = QStringLiteral(
            "AutoTimingCore tempo map is unavailable (%1).").arg(map.failureReason);
        return proposal;
    }
    if (!map.bpmListAvailable || map.bpmList.isEmpty())
    {
        proposal.unavailableReason = QStringLiteral(
            "AutoTimingCore BPM list is unavailable (%1).").arg(map.bpmListFailureReason);
        return proposal;
    }
    if (map.bpmList.size() > options.maximumEntries)
    {
        proposal.unavailableReason = QStringLiteral(
            "The core BPM list exceeded the CCE safety entry limit.");
        return proposal;
    }
    if (map.maximumBpmListModelErrorMilliseconds > options.maximumModelErrorMs + 1.0e-9)
    {
        proposal.unavailableReason = QStringLiteral(
            "The core BPM list model error (%1 ms) exceeded the CCE acceptance target.")
                                         .arg(map.maximumBpmListModelErrorMilliseconds, 0, 'f', 3);
        return proposal;
    }

    int firstBeatNum = 0;
    int firstBeatNumerator = 0;
    int firstBeatDenominator = 1;
    MathUtils::msToBeat(
        map.startSeconds * 1000.0 - offsetMs,
        existingBpmList,
        offsetMs,
        firstBeatNum,
        firstBeatNumerator,
        firstBeatDenominator);
    proposal.firstAnchorBeat = firstBeatNum +
                               static_cast<double>(firstBeatNumerator) / firstBeatDenominator;

    QVector<ProjectedEntry> generated;
    generated.reserve(map.bpmList.size());
    for (const AutoTiming2BpmPoint &point : map.bpmList)
    {
        appendProjectedEntry(
            generated,
            proposal.firstAnchorBeat + point.beat - map.startBeat,
            point.bpm);
    }
    if (generated.isEmpty())
    {
        proposal.unavailableReason = QStringLiteral(
            "AutoTimingCore BPM points could not be mapped to chart coordinates.");
        return proposal;
    }

    QVector<BpmEntry> merged;
    merged.reserve(existingBpmList.size() + generated.size());
    for (const BpmEntry &entry : existingBpmList)
    {
        if (beatPosition(entry) < proposal.firstAnchorBeat - 1.0e-8)
            merged.append(entry);
    }
    for (const ProjectedEntry &entry : generated)
        merged.append(makeBpmEntry(entry.beat, entry.bpm, options.maximumBeatDenominator));
    std::sort(merged.begin(), merged.end(), [](const BpmEntry &left, const BpmEntry &right)
    {
        return beatPosition(left) < beatPosition(right);
    });

    QVector<BpmEntry> normalized;
    normalized.reserve(merged.size());
    for (const BpmEntry &entry : merged)
    {
        if (!normalized.isEmpty() &&
            samePosition(beatPosition(normalized.last()), beatPosition(entry)))
        {
            normalized.last() = entry;
        }
        else
        {
            normalized.append(entry);
        }
    }

    double maximumAnchorResidualMs = map.maximumAnchorResidualMilliseconds;
    for (const AutoTiming2TempoMapAnchor &anchor : map.anchors)
    {
        const double beat = proposal.firstAnchorBeat +
                            anchor.phaseBeat - map.startBeat;
        const BpmEntry position = makeBpmEntry(beat, anchor.modelBpm, options.maximumBeatDenominator);
        const double projectedAudioMs = MathUtils::beatToMs(
            position.beatNum,
            position.numerator,
            position.denominator,
            normalized,
            offsetMs) + offsetMs;
        maximumAnchorResidualMs = std::max(
            maximumAnchorResidualMs,
            std::fabs(projectedAudioMs - anchor.timeSeconds * 1000.0));
    }

    proposal.maximumAnchorResidualMs = maximumAnchorResidualMs;
    proposal.generatedEntryCount = generated.size();
    proposal.bpmList = std::move(normalized);
    if (maximumAnchorResidualMs > options.maximumAcceptedAnchorResidualMs)
    {
        proposal.bpmList.clear();
        proposal.unavailableReason = QStringLiteral(
            "The projected BPM list missed a detected pulse anchor by %1 ms.")
                                         .arg(maximumAnchorResidualMs, 0, 'f', 3);
        return proposal;
    }

    proposal.available = true;
    return proposal;
}

BpmMeasureUtils::PhaseOffsetProposal BpmMeasureUtils::buildPhaseOffsetProposal(
    const AutoTiming2Summary &summary,
    double bpm,
    const PhaseOffsetOptions &options)
{
    PhaseOffsetProposal proposal;
    const AutoTiming2TempoMap &map = summary.tempoMap;
    proposal.sourceAnchorCount = map.anchors.size();
    if (!isPositiveFinite(bpm) || !(options.maximumAcceptedResidualMs > 0.0))
    {
        proposal.unavailableReason = QStringLiteral("Invalid phase-offset projection options.");
        return proposal;
    }
    if (!map.available || map.anchors.isEmpty())
    {
        proposal.unavailableReason = QStringLiteral("AutoTimingCore has no usable phase anchors.");
        return proposal;
    }
    if (map.hasTempoChange)
    {
        proposal.unavailableReason = QStringLiteral(
            "A single Malody offset cannot represent a variable-tempo phase map.");
        return proposal;
    }

    const double periodMs = 60000.0 / bpm;
    double weightedCosine = 0.0;
    double weightedSine = 0.0;
    double totalWeight = 0.0;
    QVector<double> anchorOffsets;
    anchorOffsets.reserve(map.anchors.size());
    for (const AutoTiming2TempoMapAnchor &anchor : map.anchors)
    {
        if (!std::isfinite(anchor.timeSeconds))
            continue;
        const double offset = wrapPositive(-anchor.timeSeconds * 1000.0, periodMs);
        const double weight = std::max(
            1.0e-6,
            std::max(0.0, anchor.confidence) *
                std::max(0.0, anchor.phaseConfidence));
        const double angle = kTwoPi * offset / periodMs;
        weightedCosine += weight * std::cos(angle);
        weightedSine += weight * std::sin(angle);
        totalWeight += weight;
        anchorOffsets.append(offset);
    }
    if (anchorOffsets.isEmpty() || !(totalWeight > 0.0) ||
        std::hypot(weightedCosine, weightedSine) <= totalWeight * 1.0e-6)
    {
        proposal.unavailableReason = QStringLiteral("Core phase anchors do not define a stable cyclic offset.");
        return proposal;
    }

    double meanAngle = std::atan2(weightedSine, weightedCosine);
    if (meanAngle < 0.0)
        meanAngle += kTwoPi;
    proposal.offsetMs = meanAngle * periodMs / kTwoPi;
    for (double offset : anchorOffsets)
    {
        const double direct = std::fabs(offset - proposal.offsetMs);
        proposal.maximumAnchorResidualMs = std::max(
            proposal.maximumAnchorResidualMs,
            std::min(direct, periodMs - direct));
    }
    if (proposal.maximumAnchorResidualMs > options.maximumAcceptedResidualMs)
    {
        proposal.unavailableReason = QStringLiteral(
            "The stable-grid phase spread (%1 ms) exceeded the CCE acceptance target.")
                                         .arg(proposal.maximumAnchorResidualMs, 0, 'f', 3);
        return proposal;
    }

    proposal.available = true;
    return proposal;
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

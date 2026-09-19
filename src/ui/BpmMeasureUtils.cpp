#include "BpmMeasureUtils.h"

#include "utils/MathUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace
{
    bool isPositiveFinite(double value)
    {
        return std::isfinite(value) && value > 0.0;
    }

    struct TrackAnchor
    {
        double timeSeconds = 0.0;
        double bpm = 0.0;
        double confidence = 0.0;
        double phaseConfidence = 0.0;
        qint64 pulseIndex = 0;
    };

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

    QVector<TrackAnchor> collectTrackAnchors(
        const AutoTiming2Summary &summary,
        const BpmMeasureUtils::TimingMapOptions &options)
    {
        QVector<TrackAnchor> anchors;
        anchors.reserve(summary.tempoTrack.size());
        for (const AutoTiming2TrackPoint &point : summary.tempoTrack)
        {
            if (point.state.compare(QStringLiteral("uncertain"), Qt::CaseInsensitive) == 0 ||
                !std::isfinite(point.pulseTimeSeconds) ||
                !isPositiveFinite(point.bpm) ||
                point.confidence < options.minimumPointConfidence ||
                point.phaseConfidence < options.minimumPhaseConfidence)
            {
                continue;
            }
            anchors.append({point.pulseTimeSeconds,
                            point.bpm,
                            point.confidence,
                            point.phaseConfidence,
                            0});
        }
        std::sort(anchors.begin(), anchors.end(), [](const TrackAnchor &left, const TrackAnchor &right)
        {
            return left.timeSeconds < right.timeSeconds;
        });

        QVector<TrackAnchor> unique;
        unique.reserve(anchors.size());
        for (const TrackAnchor &anchor : anchors)
        {
            if (!unique.isEmpty() &&
                std::fabs(anchor.timeSeconds - unique.last().timeSeconds) <= 0.001)
            {
                const double oldStrength = unique.last().confidence + unique.last().phaseConfidence;
                const double newStrength = anchor.confidence + anchor.phaseConfidence;
                if (newStrength > oldStrength)
                    unique.last() = anchor;
                continue;
            }
            unique.append(anchor);
        }

        QVector<TrackAnchor> indexed;
        indexed.reserve(unique.size());
        for (const TrackAnchor &anchor : unique)
        {
            if (indexed.isEmpty())
            {
                indexed.append(anchor);
                continue;
            }

            const TrackAnchor &previous = indexed.last();
            const double elapsed = anchor.timeSeconds - previous.timeSeconds;
            if (!(elapsed > 0.001))
                continue;
            const double meanBpm = 0.5 * (previous.bpm + anchor.bpm);
            const double estimatedPulses = elapsed * meanBpm / 60.0;
            const qint64 pulseCount = qRound64(estimatedPulses);
            if (pulseCount < 1)
                continue;

            TrackAnchor indexedAnchor = anchor;
            indexedAnchor.pulseIndex = previous.pulseIndex + pulseCount;
            indexed.append(indexedAnchor);
        }
        return indexed;
    }

    double linearTempoTimeAtBeat(
        double beatOffset,
        double beatSpan,
        double durationSeconds,
        double startBpm,
        double endBpm)
    {
        if (!(beatSpan > 0.0) || !(durationSeconds > 0.0) ||
            !isPositiveFinite(startBpm) || !isPositiveFinite(endBpm))
        {
            return 0.0;
        }
        const double clampedBeat = std::clamp(beatOffset, 0.0, beatSpan);
        const double startRate = startBpm / 60.0;
        const double rateSlope = (endBpm - startBpm) / (60.0 * durationSeconds);
        if (std::fabs(rateSlope) <= 1.0e-12)
            return clampedBeat / startRate;

        const double discriminant = std::max(
            0.0,
            startRate * startRate + 2.0 * rateSlope * clampedBeat);
        const double root = std::sqrt(discriminant);
        const double denominator = startRate + root;
        return denominator > 1.0e-12
                   ? 2.0 * clampedBeat / denominator
                   : clampedBeat / std::max(1.0e-12, startRate);
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
        options.maximumEntries < 2 || options.maximumBeatDenominator < 1)
    {
        proposal.unavailableReason = QStringLiteral("Invalid timing-map projection options.");
        return proposal;
    }

    const QVector<TrackAnchor> anchors = collectTrackAnchors(summary, options);
    proposal.sourceAnchorCount = anchors.size();
    if (anchors.size() < 2)
    {
        proposal.unavailableReason = QStringLiteral("AutoTiming 2 produced fewer than two phase anchors.");
        return proposal;
    }

    proposal.sourceStartSeconds = anchors.first().timeSeconds;
    proposal.sourceEndSeconds = anchors.last().timeSeconds;

    int firstBeatNum = 0;
    int firstBeatNumerator = 0;
    int firstBeatDenominator = 1;
    MathUtils::msToBeat(
        anchors.first().timeSeconds * 1000.0,
        existingBpmList,
        offsetMs,
        firstBeatNum,
        firstBeatNumerator,
        firstBeatDenominator);
    proposal.firstAnchorBeat = firstBeatNum +
                               static_cast<double>(firstBeatNumerator) / firstBeatDenominator;

    QVector<ProjectedEntry> generated;
    generated.reserve(std::min<qsizetype>(
        static_cast<qsizetype>(options.maximumEntries),
        anchors.size() * 4));
    double maximumModelErrorMs = 0.0;

    for (qsizetype i = 0; i + 1 < anchors.size(); ++i)
    {
        const TrackAnchor &left = anchors[i];
        const TrackAnchor &right = anchors[i + 1];
        const double startBeat = proposal.firstAnchorBeat +
                                 static_cast<double>(left.pulseIndex - anchors.first().pulseIndex);
        const double endBeat = proposal.firstAnchorBeat +
                               static_cast<double>(right.pulseIndex - anchors.first().pulseIndex);
        const double beatSpan = endBeat - startBeat;
        const double duration = right.timeSeconds - left.timeSeconds;
        if (!(beatSpan > 0.0) || !(duration > 0.0))
            continue;

        const double exactAverageBpm = beatSpan * 60.0 / duration;
        const double relativeTempoChange = std::fabs(right.bpm / left.bpm - 1.0);
        proposal.hasTempoChange = proposal.hasTempoChange ||
                                  relativeTempoChange > options.tempoChangeRelativeTolerance;
        const double slope = relativeTempoChange > 0.0
                                 ? std::fabs(std::log2(right.bpm / left.bpm)) / duration
                                 : 0.0;

        if (relativeTempoChange <= options.tempoChangeRelativeTolerance)
        {
            appendProjectedEntry(generated, startBeat, exactAverageBpm);
            continue;
        }

        if (slope > options.maximumContinuousSlopeOctavesPerSecond)
        {
            proposal.hasAbruptChange = true;
            const double denominator = left.bpm - right.bpm;
            const double changeAfter = std::fabs(denominator) > 1.0e-9
                                           ? (beatSpan * 60.0 - right.bpm * duration) / denominator
                                           : -1.0;
            if (changeAfter >= -1.0e-6 && changeAfter <= duration + 1.0e-6)
            {
                const double clampedChange = std::clamp(changeAfter, 0.0, duration);
                const double changeBeat = startBeat + clampedChange * left.bpm / 60.0;
                if (clampedChange > 1.0e-6)
                    appendProjectedEntry(generated, startBeat, left.bpm);
                appendProjectedEntry(generated, changeBeat, right.bpm);
            }
            else
            {
                appendProjectedEntry(generated, startBeat, exactAverageBpm);
            }
            continue;
        }

        proposal.hasContinuousChange = true;
        const double rawIntegral = duration * (left.bpm + right.bpm) / 120.0;
        const double scale = rawIntegral > 0.0 ? beatSpan / rawIntegral : 1.0;
        const double startBpm = left.bpm * scale;
        const double endBpm = right.bpm * scale;

        std::function<void(double, double, double, double, int)> appendApproximation;
        appendApproximation = [&](double localStartBeat,
                                  double localEndBeat,
                                  double localStartTime,
                                  double localEndTime,
                                  int depth)
        {
            if (generated.size() >= options.maximumEntries)
                return;
            const double localBeatSpan = localEndBeat - localStartBeat;
            const double midpointBeat = 0.5 * (localStartBeat + localEndBeat);
            const double exactMidpointTime = linearTempoTimeAtBeat(
                midpointBeat - startBeat,
                beatSpan,
                duration,
                startBpm,
                endBpm);
            const double linearMidpointTime = 0.5 * (localStartTime + localEndTime);
            const double errorMs = std::fabs(exactMidpointTime - linearMidpointTime) * 1000.0;

            if (errorMs <= options.maximumModelErrorMs ||
                localBeatSpan <= 1.0 / 64.0 || depth >= 24)
            {
                maximumModelErrorMs = std::max(maximumModelErrorMs, errorMs);
                const double segmentDuration = localEndTime - localStartTime;
                if (segmentDuration > 0.0)
                {
                    appendProjectedEntry(
                        generated,
                        localStartBeat,
                        localBeatSpan * 60.0 / segmentDuration);
                }
                return;
            }

            appendApproximation(
                localStartBeat,
                midpointBeat,
                localStartTime,
                exactMidpointTime,
                depth + 1);
            appendApproximation(
                midpointBeat,
                localEndBeat,
                exactMidpointTime,
                localEndTime,
                depth + 1);
        };

        appendApproximation(startBeat, endBeat, 0.0, duration, 0);
        if (generated.size() >= options.maximumEntries)
        {
            proposal.unavailableReason = QStringLiteral("The projected BPM list exceeded the safety entry limit.");
            return proposal;
        }
    }

    const double lastBeat = proposal.firstAnchorBeat +
                            static_cast<double>(anchors.last().pulseIndex - anchors.first().pulseIndex);
    appendProjectedEntry(generated, lastBeat, anchors.last().bpm);
    if (generated.isEmpty())
    {
        proposal.unavailableReason = QStringLiteral("AutoTiming 2 anchors could not form a monotonic timing map.");
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

    double maximumAnchorResidualMs = 0.0;
    for (const TrackAnchor &anchor : anchors)
    {
        const double beat = proposal.firstAnchorBeat +
                            static_cast<double>(anchor.pulseIndex - anchors.first().pulseIndex);
        const BpmEntry position = makeBpmEntry(beat, anchor.bpm, options.maximumBeatDenominator);
        const double projectedMs = MathUtils::beatToMs(
            position.beatNum,
            position.numerator,
            position.denominator,
            normalized,
            offsetMs);
        maximumAnchorResidualMs = std::max(
            maximumAnchorResidualMs,
            std::fabs(projectedMs - anchor.timeSeconds * 1000.0));
    }

    proposal.maximumAnchorResidualMs = maximumAnchorResidualMs;
    proposal.maximumModelErrorMs = maximumModelErrorMs;
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

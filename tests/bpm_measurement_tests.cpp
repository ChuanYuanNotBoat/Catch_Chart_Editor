#include "audio/BpmDetector.h"
#include "model/BpmEntry.h"
#include "ui/BpmMeasureUtils.h"
#include "ui/dialogs/BpmMeasureDialog.h"
#include "utils/MathUtils.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

namespace
{
    int g_failures = 0;

    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", message.c_str());
            ++g_failures;
        }
    }

    bool nearlyEqual(double left, double right, double tolerance = 1e-9)
    {
        return std::fabs(left - right) <= tolerance;
    }

    AutoTiming2TempoMapAnchor tempoMapAnchor(double timeSeconds,
                                              double phaseBeat,
                                              double bpm)
    {
        AutoTiming2TempoMapAnchor anchor;
        anchor.timeSeconds = timeSeconds;
        anchor.observedBpm = bpm;
        anchor.modelBpm = bpm;
        anchor.confidence = 0.9;
        anchor.phaseConfidence = 0.9;
        anchor.phaseBeat = phaseBeat;
        anchor.pulseIndex = qRound64(phaseBeat);
        return anchor;
    }

    void setCoreTempoMap(AutoTiming2Summary &summary,
                         double startSeconds,
                         double endSeconds,
                         double endBeat,
                         const QVector<AutoTiming2TempoMapAnchor> &anchors,
                         const QVector<AutoTiming2BpmPoint> &bpmList,
                         bool continuousChange = false,
                         bool abruptChange = false,
                         const QVector<AutoTiming2TempoCurveSegment> &segments = {})
    {
        AutoTiming2TempoMap &map = summary.tempoMap;
        map.available = true;
        map.failureReason = QStringLiteral("none");
        map.startSeconds = startSeconds;
        map.endSeconds = endSeconds;
        map.startBeat = 0.0;
        map.endBeat = endBeat;
        map.anchors = anchors;
        map.hasContinuousChange = continuousChange;
        map.hasAbruptChange = abruptChange;
        map.hasTempoChange = continuousChange || abruptChange;
        map.segments = segments;
        if (map.segments.isEmpty() && !bpmList.isEmpty())
        {
            if (abruptChange && bpmList.size() >= 2)
            {
                AutoTiming2TempoCurveSegment before;
                before.startBeat = bpmList.first().beat;
                before.endBeat = bpmList[1].beat;
                before.startBpm = bpmList.first().bpm;
                before.endBpm = before.startBpm;
                before.confidence = 0.9;
                before.kind = QStringLiteral("constant");
                map.segments.append(before);

                AutoTiming2TempoCurveSegment after;
                after.startBeat = bpmList[1].beat;
                after.endBeat = endBeat;
                after.startBpm = bpmList.last().bpm;
                after.endBpm = after.startBpm;
                after.confidence = 0.9;
                after.kind = QStringLiteral("constant");
                map.segments.append(after);
            }
            else
            {
                AutoTiming2TempoCurveSegment segment;
                segment.startBeat = bpmList.first().beat;
                segment.endBeat = endBeat;
                segment.startBpm = bpmList.first().bpm;
                segment.endBpm = bpmList.last().bpm;
                segment.confidence = 0.9;
                segment.kind = continuousChange
                    ? QStringLiteral("continuous")
                    : QStringLiteral("constant");
                map.segments.append(segment);
            }
        }
        map.bpmListAvailable = true;
        map.bpmListFailureReason = QStringLiteral("none");
        map.bpmList = bpmList;
    }

    double linearRampPulseTime(double beats, double startBpm, double slopeBpmPerSecond)
    {
        const double startRate = startBpm / 60.0;
        const double rateSlope = slopeBpmPerSecond / 60.0;
        return 2.0 * beats /
               (startRate + std::sqrt(startRate * startRate + 2.0 * rateSlope * beats));
    }

    void testRecommendationSelectionAndUncertainty()
    {
        BpmDetector::DetectionResult result;
        result.analysisStatus = BpmDetector::AnalysisStatus::Succeeded;

        AutoTiming2Candidate weaker;
        weaker.bpm = 87.0;
        weaker.score = 0.6;
        weaker.harmonicFamilyId = 4;
        AutoTiming2Candidate strongest;
        strongest.bpm = 174.0;
        strongest.score = 1.0;
        strongest.harmonicFamilyId = 4;
        AutoTiming2Candidate malformedScore;
        malformedScore.bpm = 130.0;
        malformedScore.score = std::numeric_limits<double>::quiet_NaN();
        result.analysis.tempoCandidates = {malformedScore, weaker, strongest};

        AutoTiming2Family family;
        family.id = 4;
        family.referenceBpm = 87.0;
        family.relativeScore = 0.9;
        family.memberCandidateIndices = {1, 2};
        result.analysis.tempoFamilies = {family};
        result.analysis.confidence.overall = 0.0;
        result.analysis.confidence.tempo = 0.0;
        result.analysis.confidence.reliableCoverage = 0.0;

        const BpmMeasureUtils::Recommendation recommendation =
            BpmMeasureUtils::selectRecommendation(result);
        require(recommendation.available, "completed V2 result with a candidate must expose a recommendation");
        require(nearlyEqual(recommendation.bpm, 174.0), "highest-scoring candidate must be recommended");
        require(std::isfinite(recommendation.candidateScore),
                "a malformed candidate score must not poison recommendation ranking");
        require(recommendation.quality == BpmMeasureUtils::EvidenceQuality::Uncertain,
                "zero confidence must classify the candidate as uncertain, not failed");
        require(recommendation.familyId == 4 && recommendation.familyBpms.size() == 2,
                "recommendation must retain its core-provided tempo family");
        require(nearlyEqual(recommendation.familyBpms[0], 87.0) &&
                    nearlyEqual(recommendation.familyBpms[1], 174.0),
                "tempo family members must be sorted for presentation");

        result.analysis.confidence.overall = BpmMeasureUtils::kSupportedOverallConfidence;
        result.analysis.confidence.tempo = BpmMeasureUtils::kSupportedTempoConfidence;
        result.analysis.confidence.reliableCoverage = BpmMeasureUtils::kMinimumReliableCoverage;
        require(BpmMeasureUtils::selectRecommendation(result).quality ==
                    BpmMeasureUtils::EvidenceQuality::Supported,
                "named confidence thresholds must classify supported evidence consistently");

        result.analysis.tempoCandidates.clear();
        const BpmMeasureUtils::Recommendation empty = BpmMeasureUtils::selectRecommendation(result);
        require(!empty.available && empty.quality == BpmMeasureUtils::EvidenceQuality::Unavailable,
                "successful analysis without candidates must remain a normal unavailable recommendation");
        require(result.hasAnalysis(), "candidate absence must not change analysis completion status");
    }

    void testMultiplierHintsMatchOnlyExistingActions()
    {
        const auto x2 = BpmMeasureUtils::findMultiplierHint(87.0, 174.0);
        require(x2.factor == 2 && x2.relation == BpmMeasureUtils::MultiplierRelation::PowerOfTwo,
                "87 -> 174 must recommend the existing x2 action");

        const auto x3 = BpmMeasureUtils::findMultiplierHint(58.0, 174.0);
        require(x3.factor == 3 && x3.relation == BpmMeasureUtils::MultiplierRelation::NumericalOnly,
                "58 -> 174 must be a numerical x3 match, not family evidence");

        require(!BpmMeasureUtils::findMultiplierHint(174.0, 87.0),
                "a lower suggestion must not invent a divide/0.5x action");
        require(!BpmMeasureUtils::findMultiplierHint(87.0, 180.0),
                "a value outside the named relative tolerance must not produce a hint");
    }

    void testTargetEntryUsesTrueChartStart()
    {
        const QVector<BpmEntry> bpmList = {BpmEntry(0, 0, 1, 120.0)};
        const BpmEntry start = BpmMeasureUtils::makeTargetEntry(
            true, 750.0, bpmList, 0, 174.0);
        require(start.beatNum == 0 && start.numerator == 0 && start.denominator == 1,
                "From Start must create the chart coordinate 0:0/1");
        require(nearlyEqual(start.bpm, 174.0), "target entry must preserve chosen BPM");

        const BpmEntry current = BpmMeasureUtils::makeTargetEntry(
            false, 750.0, bpmList, 0, 150.0);
        require(current.beatNum == 1 && current.numerator == 1 && current.denominator == 2,
                "From Current Time must continue using MathUtils ms-to-beat conversion");
    }

    void testTimingMapProjectionPreventsFixedAndStepDrift()
    {
        const QVector<BpmEntry> existing = {BpmEntry(0, 0, 1, 120.0)};

        AutoTiming2Summary fixed;
        setCoreTempoMap(
            fixed,
            0.125,
            8.125,
            16.0,
            {
                tempoMapAnchor(0.125, 0.0, 120.0),
                tempoMapAnchor(4.125, 8.0, 120.0),
                tempoMapAnchor(8.125, 16.0, 120.0),
            },
            {{0.0, 120.0}});
        const BpmMeasureUtils::TimingMapProposal fixedProposal =
            BpmMeasureUtils::buildTimingMapProposal(fixed, existing, 0);
        require(!fixedProposal.available && !fixedProposal.unavailableReason.isEmpty(),
                "fixed pulse track must abstain from generating a variable-tempo proposal");
        require(!fixedProposal.hasTempoChange,
                "fixed pulse track must not be presented as a tempo change");
        const BpmMeasureUtils::TimingMapProposal offsetFixedProposal =
            BpmMeasureUtils::buildTimingMapProposal(fixed, existing, 87);
        require(!offsetFixedProposal.available,
                "a fixed map must remain unavailable regardless of the current offset");

        AutoTiming2Summary step;
        setCoreTempoMap(
            step,
            0.125,
            28.125,
            58.0,
            {
                tempoMapAnchor(0.125, 0.0, 100.0),
                tempoMapAnchor(12.125, 20.0, 100.0),
                tempoMapAnchor(14.525, 24.0, 100.0),
                tempoMapAnchor(16.125, 28.0, 150.0),
                tempoMapAnchor(28.125, 58.0, 150.0),
            },
            {{0.0, 100.0}, {24.0, 150.0}},
            false,
            true);
        const QVector<BpmEntry> stepExisting = {BpmEntry(0, 0, 1, 100.0)};
        const BpmMeasureUtils::TimingMapProposal stepProposal =
            BpmMeasureUtils::buildTimingMapProposal(step, stepExisting, 0);
        require(stepProposal.available, "tempo-step pulse track must produce a BPM map proposal");
        require(stepProposal.hasTempoChange && stepProposal.hasAbruptChange,
                "tempo-step pulse track must retain an abrupt change in the proposal");
        require(stepProposal.generatedEntryCount >= 1 && stepProposal.bpmList.size() >= 2,
                "tempo-step proposal must contain a boundary BPM in addition to the stable entry");
        require(stepProposal.maximumAnchorResidualMs < 0.1,
                "tempo-step map must not accumulate offset after the change");
    }

    void testStablePhaseProjectsToMalodyOffset()
    {
        AutoTiming2Summary stable;
        setCoreTempoMap(
            stable,
            3.913,
            11.913,
            24.0,
            {
                tempoMapAnchor(3.9130, 0.0, 180.0),
                tempoMapAnchor(7.9134, 12.0, 180.0),
                tempoMapAnchor(11.9128, 24.0, 180.0),
            },
            {{0.0, 180.0}});
        const BpmMeasureUtils::PhaseOffsetProposal phase =
            BpmMeasureUtils::buildPhaseOffsetProposal(stable, 180.0);
        require(phase.available, "stable Core pulse anchors must project to a Malody offset");
        require(std::fabs(phase.offsetMs - 87.0) < 0.5,
                "PIONEER-like 180 BPM anchors must recover the authored 87 ms phase");
        require(phase.maximumAnchorResidualMs < 0.5,
                "stable phase projection must report its cross-anchor residual");

        stable.tempoMap.hasTempoChange = true;
        const BpmMeasureUtils::PhaseOffsetProposal variable =
            BpmMeasureUtils::buildPhaseOffsetProposal(stable, 180.0);
        require(!variable.available,
                "variable-tempo maps must not be collapsed into a single offset");
    }

    void testTimingMapProjectionApproximatesContinuousRampBelowFiveMs()
    {
        constexpr double startTime = 0.125;
        constexpr double startBpm = 90.0;
        constexpr double slope = 1.25;
        AutoTiming2Summary ramp;
        QVector<AutoTiming2TempoMapAnchor> anchors;
        for (int beat = 0; beat <= 96; beat += 8)
        {
            const double localTime = linearRampPulseTime(beat, startBpm, slope);
            anchors.append(tempoMapAnchor(
                startTime + localTime,
                beat,
                startBpm + slope * localTime));
        }
        QVector<AutoTiming2BpmPoint> bpmPoints;
        for (int beat = 0; beat < 96; ++beat)
        {
            const double start = linearRampPulseTime(beat, startBpm, slope);
            const double end = linearRampPulseTime(beat + 1, startBpm, slope);
            bpmPoints.append({double(beat), 60.0 / (end - start)});
        }
        setCoreTempoMap(
            ramp,
            startTime,
            startTime + linearRampPulseTime(96.0, startBpm, slope),
            96.0,
            anchors,
            bpmPoints,
            true,
            false);

        BpmMeasureUtils::TimingMapOptions options;
        options.maximumModelErrorMs = 1.0;
        const QVector<BpmEntry> existing = {BpmEntry(0, 0, 1, 120.0)};
        const BpmMeasureUtils::TimingMapProposal proposal =
            BpmMeasureUtils::buildTimingMapProposal(ramp, existing, 0, options);
        require(proposal.available, "continuous-ramp pulse track must produce a BPM map proposal");
        require(proposal.hasTempoChange && proposal.hasContinuousChange,
                "continuous-ramp proposal must retain continuous tempo motion");
        require(proposal.generatedEntryCount > 2,
                "continuous-ramp projection must not collapse to one BPM");

        double maximumPulseErrorMs = 0.0;
        for (int beat = 0; beat <= 96; ++beat)
        {
            const double expectedMs =
                (startTime + linearRampPulseTime(beat, startBpm, slope)) * 1000.0;
            const double projectedBeat = proposal.firstAnchorBeat + beat;
            int beatNum = 0;
            int numerator = 0;
            int denominator = 1;
            MathUtils::floatToBeat(projectedBeat, beatNum, numerator, denominator, 65536);
            const double actualMs = MathUtils::beatToMs(
                beatNum, numerator, denominator, proposal.bpmList, 0);
            maximumPulseErrorMs = std::max(maximumPulseErrorMs, std::fabs(actualMs - expectedMs));
        }
        require(maximumPulseErrorMs < 5.0,
                "continuous-ramp CCE BPM list exceeded the 5 ms pulse-grid target");
        require(proposal.maximumAnchorResidualMs < 0.1,
                "continuous-ramp map accumulated drift at a source anchor");
    }

    void testTimingMapProjectionKeepsReversingTempo()
    {
        for (bool interiorPeak : {false, true})
        {
            const auto phase = [interiorPeak](double time)
            {
                if (interiorPeak)
                {
                    const double x = time / 8.0;
                    return 16.0 * x + 8.0 * x * x - (16.0 / 3.0) * x * x * x;
                }
                if (time <= 4.0)
                    return 2.0 * time + time * time / 16.0;
                const double local = time - 4.0;
                return 9.0 + 2.5 * local - local * local / 16.0;
            };
            AutoTiming2TempoCurveSegment up;
            up.startSeconds = 0.0;
            up.endSeconds = interiorPeak ? 8.0 : 4.0;
            up.startBeat = 0.0;
            up.endBeat = phase(up.endSeconds);
            up.startBpm = 120.0;
            up.endBpm = interiorPeak ? 120.0 : 150.0;
            up.phaseLinear = interiorPeak ? 16.0 : 8.0;
            up.phaseQuadratic = interiorPeak ? 8.0 : 1.0;
            up.phaseCubic = interiorPeak ? -16.0 / 3.0 : 0.0;
            up.confidence = 0.9;
            up.kind = QStringLiteral("continuous");
            QVector<AutoTiming2TempoCurveSegment> segments = {up};
            if (!interiorPeak)
            {
                AutoTiming2TempoCurveSegment down = up;
                down.startSeconds = 4.0;
                down.endSeconds = 8.0;
                down.startBeat = 9.0;
                down.endBeat = 18.0;
                down.startBpm = 150.0;
                down.endBpm = 120.0;
                down.phaseLinear = 10.0;
                down.phaseQuadratic = -1.0;
                segments.append(down);
            }
            QVector<AutoTiming2BpmPoint> points;
            for (int i = 0; i < 64; ++i)
            {
                const double time = i / 8.0;
                points.append({phase(time), (phase(time + 0.125) - phase(time)) * 480.0});
            }
            points.append({phase(8.0), 120.0});
            AutoTiming2Summary summary;
            setCoreTempoMap(summary, 0.0, 8.0, phase(8.0),
                {tempoMapAnchor(0.0, 0.0, 120.0),
                 tempoMapAnchor(4.0, phase(4.0), 150.0),
                 tempoMapAnchor(8.0, phase(8.0), 120.0)},
                points, true, false, segments);
            const auto proposal = BpmMeasureUtils::buildTimingMapProposal(
                summary, {BpmEntry(0, 0, 1, 120.0)}, 0);
            require(proposal.available && proposal.generatedEntryCount > 2,
                    "a tempo curve returning to its starting BPM must retain its interior changes");
            require(proposal.maximumAnchorResidualMs < 0.1,
                    "reversing tempo must preserve Core's phase anchors after projection");
        }
    }

    void testCoreAudioToChartProjectionAccuracy()
    {
        constexpr int sampleRate = 32000;
        constexpr double duration = 48.0;
        constexpr double firstPulse = 0.1373;
        for (bool decreasing : {false, true})
        {
            const double startBpm = decreasing ? 150.0 : 90.0;
            const double slope = decreasing ? -1.25 : 1.25;
            QVector<float> mono(int(duration * sampleRate));
            std::uint32_t background = 0x243f6a88U;
            for (float &sample : mono)
            {
                background = background * 1664525U + 1013904223U;
                sample = float((double((background >> 8) & 0xffffU) / 32767.5 - 1.0) * 0.0002);
            }
            for (int beat = 0;; ++beat)
            {
                const double pulse = firstPulse + linearRampPulseTime(beat, startBpm, slope);
                if (pulse >= duration)
                    break;
                const int firstSample = int(std::ceil(pulse * sampleRate));
                std::uint32_t noise = 0xb7e15162U + std::uint32_t(beat) * 0x9e3779b9U;
                for (int i = firstSample; i < mono.size() && i < firstSample + int(0.06 * sampleRate); ++i)
                {
                    noise = noise * 1664525U + 1013904223U;
                    const double local = double(i) / sampleRate - pulse;
                    mono[i] += float(0.85 * std::exp(-local * 70.0) *
                        (double((noise >> 8) & 0xffffU) / 32767.5 - 1.0));
                }
            }
            AutoTiming2Options options;
            options.windowSpecs = {{8.0, 4.0}};
            options.preferLocalTempoEvidence = true;
            options.minimumTempoBpm = 70.0;
            options.maximumTempoBpm = 180.0;
            options.tempoMapMaximumTimeErrorMilliseconds = 0.5;
            AutoTiming2Summary summary;
            QString error;
            const bool analyzed = AutoTiming2Bridge::analyzeMono(mono, sampleRate, options, summary, &error);
            require(analyzed && summary.tempoMap.available,
                    "analytic PCM must pass through the pinned Core and CCE bridge: " + error.toStdString());
            if (!analyzed || !summary.tempoMap.available)
                continue;
            require(summary.tempoMap.phaseCoherentIntervalCount > 0,
                    "the bridge must retain the new Core phase-coherence diagnostics");
            const double localStart = summary.tempoMap.startSeconds - firstPulse;
            const double firstBeat = std::round(
                (startBpm * localStart + 0.5 * slope * localStart * localStart) / 60.0);
            double maximumErrorMs = 0.0;
            for (double audioStart : {0.0, 61.375})
            {
                AutoTiming2Summary translated = summary;
                AutoTiming2Bridge::translateTimeline(translated, audioStart);
                require(translated.tempoMap.phaseCoherentIntervalCount ==
                            summary.tempoMap.phaseCoherentIntervalCount,
                        "cropped-audio translation must preserve phase diagnostics");
                for (int offset : {-87, 0, 137})
                {
                    const auto proposal = BpmMeasureUtils::buildTimingMapProposal(
                        translated, {BpmEntry(0, 0, 1, 120.0)}, offset);
                    require(proposal.available,
                            "analytic variable tempo must produce an applicable CCE map: " +
                                proposal.unavailableReason.toStdString());
                    if (!proposal.available)
                        continue;
                    for (double beat = 0.0; beat <= summary.tempoMap.endBeat; beat += 1.0 / 32.0)
                    {
                        int whole = 0, numerator = 0, denominator = 1;
                        MathUtils::floatToBeat(proposal.firstAnchorBeat + beat,
                            whole, numerator, denominator, 65536);
                        const double projectedAudioMs = MathUtils::beatToMs(
                            whole, numerator, denominator, proposal.bpmList, offset) + offset;
                        const double expectedAudioMs = 1000.0 * (audioStart + firstPulse +
                            linearRampPulseTime(firstBeat + beat, startBpm, slope));
                        maximumErrorMs = std::max(maximumErrorMs,
                            std::fabs(projectedAudioMs - expectedAudioMs));
                    }
                }
            }
            std::printf("[bpm-ui-test] %s PCM-to-chart maximum: %.6f ms\n",
                decreasing ? "decelerando" : "accelerando", maximumErrorMs);
            require(maximumErrorMs < 5.0,
                    "audio-to-Core-to-Malody projection must stay below 5 ms at dense interior beats");
        }
    }

    void testTimingMapProjectionKeepsStableRegionsUntouched()
    {
        const QVector<BpmEntry> existing = {
            BpmEntry(0, 0, 1, 120.0),
            BpmEntry(20, 0, 1, 149.0),
            BpmEntry(24, 0, 1, 151.0),
        };

        AutoTiming2TempoCurveSegment stableBefore;
        stableBefore.startBeat = 0.0;
        stableBefore.endBeat = 8.0;
        stableBefore.startBpm = 120.0;
        stableBefore.endBpm = 120.0;
        stableBefore.confidence = 0.9;
        stableBefore.kind = QStringLiteral("constant");

        AutoTiming2TempoCurveSegment ramp;
        ramp.startBeat = 8.0;
        ramp.endBeat = 16.0;
        ramp.startBpm = 120.0;
        ramp.endBpm = 150.0;
        ramp.confidence = 0.9;
        ramp.kind = QStringLiteral("continuous");

        AutoTiming2TempoCurveSegment stableAfter;
        stableAfter.startBeat = 16.0;
        stableAfter.endBeat = 32.0;
        stableAfter.startBpm = 150.0;
        stableAfter.endBpm = 150.0;
        stableAfter.confidence = 0.9;
        stableAfter.kind = QStringLiteral("constant");

        AutoTiming2Summary summary;
        setCoreTempoMap(
            summary,
            0.0,
            7.594642857142857,
            32.0,
            {
                tempoMapAnchor(0.0, 0.0, 120.0),
                tempoMapAnchor(4.0, 8.0, 120.0),
                tempoMapAnchor(5.9375, 12.0, 128.0),
                tempoMapAnchor(7.594642857142857, 16.0, 150.0),
            },
            {
                {0.0, 120.0},
                {2.0, 119.0},
                {4.0, 121.0},
                {8.0, 120.0},
                {10.0, 128.0},
                {12.0, 140.0},
                {14.0, 150.0},
                {16.0, 150.0},
                {20.0, 149.0},
                {24.0, 151.0},
                {32.0, 150.0},
            },
            true,
            false,
            {stableBefore, ramp, stableAfter});

        const BpmMeasureUtils::TimingMapProposal proposal =
            BpmMeasureUtils::buildTimingMapProposal(summary, existing, 0);
        require(proposal.available,
                "a credible continuous segment must produce a timing-map proposal");
        require(proposal.generatedEntryCount == 4,
                "only BPM points inside the continuous segment should be generated");
        require(proposal.bpmList.size() == 7,
                "stable chart entries and variable-segment entries must be merged");
        require(nearlyEqual(proposal.bpmList[proposal.bpmList.size() - 2].bpm, 149.0),
                "stable BPM entries outside the variable segment must be preserved");
        require(nearlyEqual(proposal.bpmList.first().bpm, 120.0) &&
                    nearlyEqual(proposal.bpmList.last().bpm, 151.0),
                "stable BPM entries on both sides of the variable segment must be preserved");
        require(proposal.maximumAnchorResidualMs < 0.1,
                "anchor residual validation must remain limited to the replaced segment");
    }

    void testTimingMapProjectionAbstainsWithoutPhaseCoverage()
    {
        AutoTiming2Summary summary;
        const QVector<BpmEntry> existing = {BpmEntry(0, 0, 1, 120.0)};
        const BpmMeasureUtils::TimingMapProposal proposal =
            BpmMeasureUtils::buildTimingMapProposal(summary, existing, 0);
        require(!proposal.available && !proposal.unavailableReason.isEmpty(),
                "insufficient phase coverage must abstain instead of fabricating a BPM list");
    }

    void testV2SuggestionRequiresExplicitUse()
    {
        BpmMeasureDialog dialog;
        dialog.setMeasuring(true);
        dialog.setLegacyUnavailable();
        dialog.setAutoTimingSuggestion(174.0, QStringLiteral("(uncertain)"));
        dialog.setAutoTimingPhaseOffset(87);
        dialog.setMeasurementComplete();

        require(nearlyEqual(dialog.measuredBpm(), 0.0),
                "V2-only result must not alter legacy Measured BPM");
        require(nearlyEqual(dialog.finalBpm(), 0.0),
                "V2 suggestion must not automatically alter BPM to Add");
        require(nearlyEqual(dialog.finalOffset(), 0.0) && !dialog.applyOffset(),
                "V2 suggestion must not alter or enable offset application");

        QPushButton *useButton = dialog.findChild<QPushButton *>(
            QStringLiteral("useAutoTiming2SuggestionButton"));
        require(useButton != nullptr && useButton->isEnabled(),
                "completed V2 suggestion must provide an explicit Use suggestion action");
        if (useButton)
            useButton->click();

        require(nearlyEqual(dialog.finalBpm(), 174.0),
                "Use suggestion must copy only the V2 BPM into BPM to Add");
        require(nearlyEqual(dialog.measuredBpm(), 0.0),
                "Use suggestion must not relabel V2 as legacy Measured BPM");
        require(nearlyEqual(dialog.finalOffset(), 87.0) && dialog.applyOffset(),
                "Use suggestion must explicitly adopt the paired V2 phase offset");
    }

    void testTimingMapRequiresExplicitSelectionAndExcludesLegacyOffset()
    {
        BpmMeasureDialog dialog;
        dialog.setMeasuring(true);
        dialog.setMeasuredBpm(120.0);
        dialog.setMeasuredOffset(125);
        dialog.setAutoTimingSuggestion(150.0, QStringLiteral("(supported)"));
        dialog.setAutoTimingMapSuggestion(QStringLiteral("6 generated BPM points"));
        dialog.setMeasurementComplete();

        QCheckBox *mapCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("applyAutoTiming2MapCheck"));
        require(mapCheck != nullptr && mapCheck->isEnabled() && !mapCheck->isChecked(),
                "a timing-map proposal must remain an explicit opt-in action");
        require(!dialog.applyAutoTimingMap(),
                "presenting a timing map must not select it automatically");
        require(dialog.applyOffset(),
                "legacy offset remains available until the timing map is selected");

        if (mapCheck)
            mapCheck->setChecked(true);
        require(dialog.applyAutoTimingMap(),
                "checking the timing-map action must select the full map");
        require(!dialog.applyOffset(),
                "a phase-anchored BPM map must not also apply an independently measured legacy offset");

        QDoubleSpinBox *singleBpm = dialog.findChild<QDoubleSpinBox *>(
            QStringLiteral("bpmToAddSpin"));
        require(singleBpm != nullptr && !singleBpm->isEnabled(),
                "single-BPM editing must be disabled while the full timing map is selected");
    }

    void testLegacyWorkflowRemainsDefault()
    {
        BpmMeasureDialog dialog;
        dialog.setMeasuring(true);
        dialog.setMeasuredBpm(87.0);
        dialog.setMeasuredOffset(125);
        dialog.setAutoTimingSuggestion(174.0, QStringLiteral("(supported)"));
        dialog.setMeasurementComplete();

        require(nearlyEqual(dialog.measuredBpm(), 87.0),
                "legacy BPM must remain the measured result");
        require(nearlyEqual(dialog.finalBpm(), 87.0),
                "legacy BPM must keep auto-filling BPM to Add by default");
        require(dialog.finalOffset() == 125 && dialog.applyOffset(),
                "From Start must retain the legacy offset workflow");

        QPushButton *x2Button = dialog.findChild<QPushButton *>(QStringLiteral("multiply2Button"));
        require(x2Button != nullptr && x2Button->isEnabled(),
                "legacy result must keep existing quick multiply actions enabled");
        if (x2Button)
            x2Button->click();
        require(nearlyEqual(dialog.finalBpm(), 174.0),
                "existing x2 action must still multiply the legacy measured BPM");

        QCheckBox *offsetCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("applyLegacyOffsetCheck"));
        require(offsetCheck != nullptr && offsetCheck->isEnabled(),
                "legacy offset control must be available in From Start mode");
    }

    void testParameterChangesInvalidateCompletedResult()
    {
        BpmMeasureDialog dialog;
        dialog.setMeasuring(true);
        dialog.setMeasuredBpm(120.0);
        dialog.setMeasuredOffset(80);
        dialog.setAutoTimingSuggestion(240.0, QStringLiteral("(supported)"));
        dialog.setMeasurementComplete();

        QCheckBox *complexSubdivisionCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("enableComplexSubdivisionCheck"));
        require(complexSubdivisionCheck != nullptr,
                "complex subdivision option must be discoverable for regression tests");
        if (complexSubdivisionCheck)
        {
            require(!complexSubdivisionCheck->isChecked() &&
                        !dialog.enableComplexSubdivisionAnalysis(),
                    "complex subdivision analysis must default to disabled");
            complexSubdivisionCheck->setChecked(true);
            require(dialog.enableComplexSubdivisionAnalysis(),
                    "complex subdivision option must follow the GUI checkbox");
            require(nearlyEqual(dialog.measuredBpm(), 0.0) &&
                        nearlyEqual(dialog.finalBpm(), 0.0),
                        "changing complex subdivision analysis must invalidate the completed result");
        }

        QCheckBox *localTempoCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("preferLocalTempoEvidenceCheck"));
        require(localTempoCheck != nullptr,
                "local tempo evidence option must be discoverable for regression tests");
        if (localTempoCheck)
        {
            require(!localTempoCheck->isChecked() &&
                        !dialog.preferLocalTempoEvidence(),
                    "local tempo evidence preference must default to disabled");
            localTempoCheck->setChecked(true);
            require(dialog.preferLocalTempoEvidence(),
                    "local tempo evidence preference must follow the GUI checkbox");
            require(nearlyEqual(dialog.measuredBpm(), 0.0) &&
                        nearlyEqual(dialog.finalBpm(), 0.0),
                    "changing local tempo evidence preference must invalidate the completed result");
        }

        QVector<BpmEntry> previewEntries = {
            BpmEntry(0, 0, 1, 120.0),
            BpmEntry(8, 0, 1, 150.0),
        };
        dialog.setAutoTimingMapPreview(previewEntries);
        QTableWidget *preview = dialog.findChild<QTableWidget *>(
            QStringLiteral("autoTiming2MapPreview"));
        require(preview != nullptr && !preview->isHidden() && preview->rowCount() == 2,
                "variable-tempo BPM preview must show a scrollable generated list");
        dialog.clearAutoTimingMapPreview();
        require(preview != nullptr && preview->isHidden() && preview->rowCount() == 0,
                "clearing the BPM preview must hide and remove generated rows");

        QComboBox *modeCombo = dialog.findChild<QComboBox *>(QStringLiteral("measureModeCombo"));
        require(modeCombo != nullptr, "measure mode combo must be discoverable for regression tests");
        if (modeCombo)
            modeCombo->setCurrentIndex(1);

        require(nearlyEqual(dialog.measuredBpm(), 0.0) && nearlyEqual(dialog.finalBpm(), 0.0),
                "changing mode must invalidate the completed BPM result");
        require(nearlyEqual(dialog.autoTimingSuggestionBpm(), 0.0),
                "changing mode must invalidate the old V2 suggestion");
        require(!dialog.applyOffset(),
                "changing to From Current Time must clear legacy offset application");

        dialog.setMeasuring(true);
        dialog.setMeasuredBpm(120.0);
        dialog.setMeasurementComplete();
        QSpinBox *durationSpin = dialog.findChild<QSpinBox *>(QStringLiteral("measureDurationSpin"));
        require(durationSpin != nullptr, "duration spin must be discoverable for regression tests");
        if (durationSpin)
        {
            require(durationSpin->maximum() > 120,
                    "BPM measurement duration must not retain the old 120-second cap");
            durationSpin->setValue(durationSpin->value() - 1);
        }
        require(nearlyEqual(dialog.measuredBpm(), 0.0) && nearlyEqual(dialog.finalBpm(), 0.0),
                "changing duration must invalidate the completed BPM result");
    }

    void testMeasurementDurationIsLimitedByAudioLength()
    {
        BpmMeasureDialog dialog;
        QSpinBox *durationSpin = dialog.findChild<QSpinBox *>(QStringLiteral("measureDurationSpin"));
        require(durationSpin != nullptr, "duration spin must be discoverable for audio-length limit test");
        if (!durationSpin)
            return;

        dialog.setAudioDurationMs(60000);
        require(durationSpin->maximum() == 60,
                "measurement duration maximum must match the audio length in seconds");
        durationSpin->setValue(60);
        durationSpin->setValue(120);
        require(durationSpin->value() == 60,
                "measurement duration must be clamped when it exceeds the audio length");
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const auto runTest = [](const char *name, void (*fn)())
    {
        std::printf("[bpm-ui-test] %s ...\n", name);
        std::fflush(stdout);
        fn();
    };

    runTest("recommendation_selection", testRecommendationSelectionAndUncertainty);
    runTest("multiplier_hints", testMultiplierHintsMatchOnlyExistingActions);
    runTest("chart_start_target", testTargetEntryUsesTrueChartStart);
    runTest("timing_map_fixed_and_step", testTimingMapProjectionPreventsFixedAndStepDrift);
    runTest("timing_map_continuous_ramp", testTimingMapProjectionApproximatesContinuousRampBelowFiveMs);
    runTest("timing_map_reversing_tempo", testTimingMapProjectionKeepsReversingTempo);
    runTest("core_audio_to_chart_accuracy", testCoreAudioToChartProjectionAccuracy);
    runTest("timing_map_stable_regions", testTimingMapProjectionKeepsStableRegionsUntouched);
    runTest("stable_phase_offset", testStablePhaseProjectsToMalodyOffset);
    runTest("timing_map_abstains", testTimingMapProjectionAbstainsWithoutPhaseCoverage);
    runTest("v2_explicit_use", testV2SuggestionRequiresExplicitUse);
    runTest("timing_map_explicit_use", testTimingMapRequiresExplicitSelectionAndExcludesLegacyOffset);
    runTest("legacy_default_workflow", testLegacyWorkflowRemainsDefault);
    runTest("parameter_change_invalidation", testParameterChangesInvalidateCompletedResult);
    runTest("duration_audio_length_limit", testMeasurementDurationIsLimitedByAudioLength);

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("BPM measurement tests passed\n");
    return 0;
}

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

#include <cmath>
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

    AutoTiming2TrackPoint trackPoint(double pulseTimeSeconds, double bpm)
    {
        AutoTiming2TrackPoint point;
        point.timeSeconds = pulseTimeSeconds;
        point.pulseTimeSeconds = pulseTimeSeconds;
        point.bpm = bpm;
        point.confidence = 0.9;
        point.phaseConfidence = 0.9;
        point.state = QStringLiteral("observed");
        return point;
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
        fixed.tempoTrack = {
            trackPoint(0.125, 120.0),
            trackPoint(4.125, 120.0),
            trackPoint(8.125, 120.0),
        };
        const BpmMeasureUtils::TimingMapProposal fixedProposal =
            BpmMeasureUtils::buildTimingMapProposal(fixed, existing, 0);
        require(fixedProposal.available, "fixed pulse track must produce a BPM map proposal");
        require(fixedProposal.maximumAnchorResidualMs < 0.1,
                "fixed BPM map must retain all pulse anchors without cumulative drift");
        require(!fixedProposal.hasTempoChange,
                "fixed pulse track must not be presented as a tempo change");

        AutoTiming2Summary step;
        step.tempoTrack = {
            trackPoint(0.125, 100.0),
            trackPoint(12.125, 100.0),
            trackPoint(16.125, 150.0),
            trackPoint(28.125, 150.0),
        };
        const BpmMeasureUtils::TimingMapProposal stepProposal =
            BpmMeasureUtils::buildTimingMapProposal(step, existing, 0);
        require(stepProposal.available, "tempo-step pulse track must produce a BPM map proposal");
        require(stepProposal.hasTempoChange && stepProposal.hasAbruptChange,
                "tempo-step pulse track must retain an abrupt change in the proposal");
        require(stepProposal.generatedEntryCount >= 2,
                "tempo-step proposal must contain more than a single global BPM");
        require(stepProposal.maximumAnchorResidualMs < 0.1,
                "tempo-step map must not accumulate offset after the change");
    }

    void testTimingMapProjectionApproximatesContinuousRampBelowFiveMs()
    {
        constexpr double startTime = 0.125;
        constexpr double startBpm = 90.0;
        constexpr double slope = 1.25;
        AutoTiming2Summary ramp;
        for (int beat = 0; beat <= 96; beat += 8)
        {
            const double localTime = linearRampPulseTime(beat, startBpm, slope);
            ramp.tempoTrack.append(trackPoint(
                startTime + localTime,
                startBpm + slope * localTime));
        }

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

    void testTimingMapProjectionAbstainsWithoutPhaseCoverage()
    {
        AutoTiming2Summary summary;
        AutoTiming2TrackPoint uncertain = trackPoint(2.0, 120.0);
        uncertain.state = QStringLiteral("uncertain");
        summary.tempoTrack.append(uncertain);
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
        require(nearlyEqual(dialog.finalOffset(), 0.0) && !dialog.applyOffset(),
                "Use suggestion must not apply V2 phase as an offset");
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
            durationSpin->setValue(durationSpin->value() - 1);
        require(nearlyEqual(dialog.measuredBpm(), 0.0) && nearlyEqual(dialog.finalBpm(), 0.0),
                "changing duration must invalidate the completed BPM result");
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
    runTest("timing_map_abstains", testTimingMapProjectionAbstainsWithoutPhaseCoverage);
    runTest("v2_explicit_use", testV2SuggestionRequiresExplicitUse);
    runTest("legacy_default_workflow", testLegacyWorkflowRemainsDefault);
    runTest("parameter_change_invalidation", testParameterChangesInvalidateCompletedResult);

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("BPM measurement tests passed\n");
    return 0;
}

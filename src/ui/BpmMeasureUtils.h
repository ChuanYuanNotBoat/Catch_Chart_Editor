#pragma once

#include "audio/BpmDetector.h"
#include "model/BpmEntry.h"

#include <QVector>

namespace BpmMeasureUtils
{
    // These thresholds classify presentation only. AutoTiming 2 remains a
    // suggestion layer and is never applied automatically at either quality.
    inline constexpr double kSupportedOverallConfidence = 0.55;
    inline constexpr double kSupportedTempoConfidence = 0.55;
    inline constexpr double kMinimumReliableCoverage = 0.10;

    inline constexpr double kMultiplierRelativeTolerance = 0.01;

    enum class EvidenceQuality
    {
        Unavailable,
        Uncertain,
        Supported,
    };

    struct Recommendation
    {
        bool available = false;
        double bpm = 0.0;
        double candidateScore = 0.0;
        qsizetype candidateIndex = -1;
        qsizetype familyId = -1;
        double familyReferenceBpm = 0.0;
        double familyScore = 0.0;
        QVector<double> familyBpms;
        EvidenceQuality quality = EvidenceQuality::Unavailable;
    };

    enum class MultiplierRelation
    {
        None,
        PowerOfTwo,
        NumericalOnly,
    };

    struct MultiplierHint
    {
        int factor = 0;
        MultiplierRelation relation = MultiplierRelation::None;

        explicit operator bool() const noexcept { return factor > 0; }
    };

    // Controls only the CCE projection of AutoTimingCore's host-independent
    // BPM points into Malody BpmEntry coordinates. Curve fitting, phase-error
    // correction and bounded-error interpolation stay in AutoTimingCore.
    struct TimingMapOptions
    {
        double maximumModelErrorMs = 2.0;
        double maximumAcceptedAnchorResidualMs = 5.0;
        int maximumEntries = 2048;
        int maximumBeatDenominator = 65536;
    };

    struct TimingMapProposal
    {
        bool available = false;
        QVector<BpmEntry> bpmList;
        QString unavailableReason;

        int sourceAnchorCount = 0;
        int generatedEntryCount = 0;
        double sourceStartSeconds = 0.0;
        double sourceEndSeconds = 0.0;
        double firstAnchorBeat = 0.0;
        double maximumAnchorResidualMs = 0.0;
        double maximumModelErrorMs = 0.0;
        bool hasTempoChange = false;
        bool hasContinuousChange = false;
        bool hasAbruptChange = false;
    };

    // Converts AutoTimingCore's stable pulse anchors into Malody's cyclic
    // offset convention. Phase estimation remains in Core; this is only the
    // host-coordinate projection needed by CCE.
    struct PhaseOffsetOptions
    {
        double maximumAcceptedResidualMs = 5.0;
    };

    struct PhaseOffsetProposal
    {
        bool available = false;
        double offsetMs = 0.0;
        double maximumAnchorResidualMs = 0.0;
        int sourceAnchorCount = 0;
        QString unavailableReason;
    };

    Recommendation selectRecommendation(const BpmDetector::DetectionResult &result);

    // Only returns factors backed by existing dialog buttons. A lower V2 value
    // never produces a divide/0.5x suggestion because the UI cannot perform it.
    MultiplierHint findMultiplierHint(double legacyBpm, double suggestedBpm);

    // Builds a complete BPM list by preserving entries before the first
    // detected pulse anchor and replacing the remainder with a phase-locked
    // AutoTiming 2 projection. Every accepted source anchor is validated
    // through MathUtils::beatToMs so cumulative drift cannot be hidden by a
    // locally plausible BPM value. This function never mutates the chart.
    TimingMapProposal buildTimingMapProposal(
        const AutoTiming2Summary &summary,
        const QVector<BpmEntry> &existingBpmList,
        int offsetMs,
        const TimingMapOptions &options = {});

    PhaseOffsetProposal buildPhaseOffsetProposal(
        const AutoTiming2Summary &summary,
        double bpm,
        const PhaseOffsetOptions &options = {});

    // Centralizes the chart-start invariant used by the confirmation workflow.
    BpmEntry makeTargetEntry(bool fromStart,
                             double chartTimeMs,
                             const QVector<BpmEntry> &bpmList,
                             int offsetMs,
                             double bpm);
}

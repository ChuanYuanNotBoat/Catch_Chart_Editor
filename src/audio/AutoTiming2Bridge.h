#pragma once

#include <QString>
#include <QVector>

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// AutoTiming2Bridge
//
// CCE-side adapter for the pinned AutoTimingCore submodule
// (third_party/AutoTimingCore, autotiming::analyze). The bridge mirrors the
// upstream contract into Qt-friendly PODs so UI/tests do not need to include
// dependency headers, and maps upstream exceptions into QString errors.
//
// Semantics preserved from the upstream contract (docs/AUTOTIMING_2_DESIGN.md):
//   - pulseTimeSeconds is an absolute time on the supplied AudioView timeline,
//     legacyOffsetMilliseconds is the legacy delay-style offset; the two must
//     never be merged. Direct bridge calls start that timeline at zero;
//     BpmDetector translates location fields to whole-file time.
//   - PeriodicityLayer rational values are evidence only, not subdivision or
//     polyrhythm assertions.
//   - averageObjectiveCost is a diagnostic cost, not a calibrated probability.
//   - Low confidence must be reported as uncertainty, not as an analysis error.
// ---------------------------------------------------------------------------

struct AutoTiming2Candidate
{
    double bpm = 0.0;
    double rawBpm = 0.0;
    double periodSeconds = 0.0;
    // Absolute time of a reference pulse on the current analysis timeline;
    // NOT the legacy offset. Global candidates do not carry phase and leave
    // hasPulseTime false.
    double pulseTimeSeconds = 0.0;
    bool hasPulseTime = false;
    double legacyOffsetMilliseconds = 0.0;
    double rawBpmUncertainty = 0.0;
    double phaseConfidence = 0.0;
    double score = 0.0;
    double harmonicRatio = 1.0;
    QString origin; // LegacyWindowEstimate | OnsetEnvelopePeriodicity | HarmonicAlias
    quint32 signature = 0;
    quint32 division = 0;
    // Global-candidate fields (diagnostics.windows rows leave these defaulted).
    qsizetype harmonicFamilyId = -1;
    double harmonicRatioToFamily = 1.0;
    QVector<qsizetype> supportingWindowIds;
};

struct AutoTiming2Window
{
    qsizetype id = 0;
    QString scale; // Short | Medium | Long
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double tempoEvidence = 0.0;
    double legacyTempoEvidence = 0.0;
    double onsetPeriodicityEvidence = 0.0;
    double crossScaleConsistency = 0.0;
    qsizetype consistencyPeerCount = 0;
    double reliability = 0.0;
    bool selectedAsAnchor = false;
    QString estimatorMessage;
    QVector<AutoTiming2Candidate> tempoCandidates;
};

struct AutoTiming2Family
{
    qsizetype id = -1;
    qsizetype primaryCandidateIndex = 0;
    double referenceBpm = 0.0;
    double relativeScore = 0.0;
    QVector<qsizetype> memberCandidateIndices;
};

struct AutoTiming2TrackPoint
{
    double timeSeconds = 0.0;
    double bpm = 0.0;
    // Absolute audio time of the predicted pulse nearest timeSeconds.
    double pulseTimeSeconds = 0.0;
    double confidence = 0.0;
    double phaseConfidence = 0.0;
    qsizetype harmonicFamilyId = -1;
    double harmonicRatioToFamily = 1.0;
    qsizetype sourceWindowId = 0;
    QString state;               // Observed | Propagated | Uncertain
    QString propagationReason;   // see TempoPropagationReason toString
    bool multiScalePhaseRefined = false;
};

struct AutoTiming2Hypothesis
{
    QString kind; // GloballyRegularized | LocalEvidenceCurve
    double averageObjectiveCost = 0.0;
    bool selected = false;
    QVector<AutoTiming2TrackPoint> track;
};

struct AutoTiming2Layer
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double observedRateBpm = 0.0;
    double referenceTempoBpm = 0.0;
    double relativeRate = 1.0;
    quint32 ratioNumerator = 0;
    quint32 ratioDenominator = 0;
    double confidence = 0.0;
    QString relation; // Harmonic | RationalApproximation | Unresolved
    QVector<qsizetype> supportingWindowIds;
};

struct AutoTiming2Region
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double confidence = 0.0;
    QString reason; // LowEvidence | NoTempoCandidate | ConflictingTempoEvidence
};

struct AutoTiming2Anchor
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double tempoBpm = 0.0;
    double reliability = 0.0;
    qsizetype harmonicFamilyId = -1;
    double harmonicRatioToFamily = 1.0;
    QVector<qsizetype> supportingWindowIds;
};

struct AutoTiming2Confidence
{
    double tempo = 0.0;
    double tempoFamily = 0.0;
    double reliableCoverage = 0.0;
    double overall = 0.0;
};

// Minimal tunable subset of autotiming::AnalysisOptions; empty fields use the
// upstream defaults.
struct AutoTiming2Options
{
    // (durationSeconds, hopSeconds) pairs; empty -> upstream default scales
    // (8s/4s + 24s/12s + 48s/24s).
    QVector<QPair<double, double>> windowSpecs;
    double minimumWindowSeconds = 4.0;
    double minimumTempoBpm = 30.0;
    double maximumTempoBpm = 480.0;
    double anchorReliabilityThreshold = 0.62;
    double maximumTrackedGapSeconds = 12.0;
};

struct AutoTiming2Summary
{
    bool valid = false;
    double durationSeconds = 0.0;
    int sampleRate = 0;
    int channels = 0;

    QVector<AutoTiming2Candidate> tempoCandidates;
    QVector<AutoTiming2Family> tempoFamilies;
    QVector<AutoTiming2TrackPoint> tempoTrack;
    QVector<AutoTiming2Hypothesis> tempoHypotheses;
    QVector<AutoTiming2Layer> periodicityLayers;
    QVector<AutoTiming2Region> uncertainRegions;
    QVector<AutoTiming2Anchor> anchors;
    QVector<AutoTiming2Window> windows;
    AutoTiming2Confidence confidence;

    // Diagnostics counters (probe-style).
    int windowCount = 0;
    int anchorSelectedWindowCount = 0;
    int multiScalePhaseRefinedCount = 0;
};

class AutoTiming2Bridge
{
public:
    // Analyze mono float PCM. sampleRate must be 32000/44100/48000 Hz; callers
    // must resample other rates beforehand (BpmDetector::analyzeFromFileDetailed
    // does this via the existing linear resampler). A structurally valid
    // analysis with no candidate or zero confidence still returns true. False
    // is reserved for invalid input/options or an analysis exception and fills
    // *outError (Chinese user-facing message).
    static bool analyzeMono(const QVector<float> &mono,
                            int sampleRate,
                            const AutoTiming2Options &options,
                            AutoTiming2Summary &outSummary,
                            QString *outError = nullptr);

    // Translate every absolute *location* in a mapped summary by offsetSeconds.
    // Durations, periods, lags and legacy offsets are deliberately unchanged.
    // BpmDetector uses this once after analyzing a cropped PCM range so callers
    // receive whole-file audio coordinates. Kept public as a small testable
    // adapter operation; UI code must not call it.
    static void translateTimeline(AutoTiming2Summary &summary, double offsetSeconds);
};

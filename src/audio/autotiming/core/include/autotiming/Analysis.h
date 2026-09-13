// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace autotiming {

inline constexpr std::size_t NoTempoFamily = static_cast<std::size_t>(-1);

struct AudioView {
    const float* interleavedSamples = nullptr;
    std::size_t frameCount = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;

    double durationSeconds() const noexcept;
};

enum class WindowScale {
    Short,
    Medium,
    Long,
};

struct WindowSpec {
    WindowScale scale = WindowScale::Medium;
    double durationSeconds = 24.0;
    double hopSeconds = 12.0;
};

struct AnalysisOptions {
    std::vector<WindowSpec> windowSpecs = {
        {WindowScale::Short, 8.0, 4.0},
        {WindowScale::Medium, 24.0, 12.0},
        {WindowScale::Long, 48.0, 24.0},
    };

    double minimumWindowSeconds = 4.0;
    double minimumTempoBpm = 30.0;
    double maximumTempoBpm = 480.0;
    double tempoAgreementTolerance = 0.015;
    double anchorReliabilityThreshold = 0.62;
    double minimumOnsetPeriodicityScore = 0.18;
    std::size_t maximumLocalTempoCandidates = 5;
    std::size_t maximumGlobalTempoCandidates = 6;

    // Low-confidence local gaps are interpolated only when bounded by direct
    // observations. A globally supported stable family may be carried through
    // a local rhythm layer; pickup/intro phase back-propagation is later work.
    double maximumTrackedGapSeconds = 12.0;
    double maximumPhaseBackPropagationSeconds = 60.0;
    double maximumContinuousTempoSlopeOctavesPerSecond = 0.08;
    double tempoTransitionPenalty = 2.0;
    double tempoSlopeChangePenalty = 4.0;
    double phaseTransitionPenalty = 1.25;
    std::size_t maximumPeriodicityLayers = 48;
};

enum class CandidateOrigin {
    LegacyWindowEstimate,
    OnsetEnvelopePeriodicity,
    HarmonicAlias,
};

struct TempoCandidate {
    double bpm = 0.0;
    double rawBpm = 0.0;
    double periodSeconds = 0.0;

    // Absolute audio time of a reference pulse in or near this analysis
    // window. This is not the legacy delay-style offset.
    double pulseTimeSeconds = 0.0;
    double legacyOffsetMilliseconds = 0.0;
    double rawBpmUncertainty = 0.0;
    double phaseConfidence = 0.0;

    double score = 0.0;
    double harmonicRatio = 1.0;
    CandidateOrigin origin = CandidateOrigin::LegacyWindowEstimate;

    std::uint32_t signature = 0;
    std::uint32_t division = 0;
};

struct SignalMetrics {
    double rmsDb = 0.0;
    double peakAmplitude = 0.0;
    double silenceRatio = 0.0;
    double clippingRatio = 0.0;
    double dynamicRangeDb = 0.0;
    double transientDensityHz = 0.0;

    double signalScore = 0.0;
    double transientScore = 0.0;
};

enum class EvidenceReason {
    LowSignal,
    SparseTransients,
    ExcessiveClipping,
    NoTempoCandidate,
    HighTempoUncertainty,
    CrossScaleDisagreement,
    EstimatorFailure,
    AnchorSelected,
};

struct AnalysisWindow {
    std::size_t id = 0;
    WindowScale scale = WindowScale::Medium;
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    SignalMetrics signal;
    std::vector<TempoCandidate> tempoCandidates;
    std::vector<EvidenceReason> reasons;
    std::string estimatorMessage;

    double tempoEvidence = 0.0;
    double legacyTempoEvidence = 0.0;
    double onsetPeriodicityEvidence = 0.0;
    double crossScaleConsistency = 0.0;
    std::size_t consistencyPeerCount = 0;
    double reliability = 0.0;
    bool selectedAsAnchor = false;
};

struct AnchorRegion {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double tempoBpm = 0.0;
    double reliability = 0.0;
    std::size_t harmonicFamilyId = NoTempoFamily;
    double harmonicRatioToFamily = 1.0;
    std::vector<std::size_t> supportingWindowIds;
};

enum class UncertaintyReason {
    LowEvidence,
    NoTempoCandidate,
    ConflictingTempoEvidence,
};

struct UncertainRegion {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double confidence = 0.0;
    UncertaintyReason reason = UncertaintyReason::LowEvidence;
};

struct GlobalTempoCandidate {
    double bpm = 0.0;
    double relativeScore = 0.0;
    std::size_t harmonicFamilyId = NoTempoFamily;
    double harmonicRatioToFamily = 1.0;
    std::vector<std::size_t> supportingWindowIds;
};

struct GlobalTempoFamily {
    std::size_t id = NoTempoFamily;
    std::size_t primaryCandidateIndex = 0;
    double referenceBpm = 0.0;
    double relativeScore = 0.0;
    std::vector<std::size_t> memberCandidateIndices;
};

enum class TempoTrackState {
    Observed,
    Propagated,
    Uncertain,
};

enum class TempoPropagationReason {
    None,
    BoundedGapInterpolation,
    StableFamilyCarry,
    RationalRhythmProjection,
    StablePhaseBackPropagation,
    LocalPhaseAnchor,
};

struct TempoTrackPoint {
    double timeSeconds = 0.0;
    double bpm = 0.0;

    // Absolute time of the predicted pulse nearest timeSeconds. This remains
    // separate from the legacy delay-style offset.
    double pulseTimeSeconds = 0.0;
    double confidence = 0.0;
    double phaseConfidence = 0.0;

    std::size_t harmonicFamilyId = NoTempoFamily;
    double harmonicRatioToFamily = 1.0;
    std::size_t sourceWindowId = 0;
    TempoTrackState state = TempoTrackState::Uncertain;
    TempoPropagationReason propagationReason = TempoPropagationReason::None;
    bool multiScalePhaseRefined = false;
};

struct TempoSegment {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double startBpm = 0.0;
    double endBpm = 0.0;
    double pulseTimeAtStartSeconds = 0.0;
    double confidence = 0.0;

    std::size_t harmonicFamilyId = NoTempoFamily;
    TempoTrackState state = TempoTrackState::Uncertain;
    bool isContinuousChange = false;
    std::vector<std::size_t> supportingWindowIds;
};

struct TempoTrack {
    std::vector<TempoTrackPoint> points;
    std::vector<TempoSegment> segments;
};

enum class TempoHypothesisKind {
    GloballyRegularized,
    LocalEvidenceCurve,
};

struct TempoTrackHypothesis {
    TempoHypothesisKind kind = TempoHypothesisKind::GloballyRegularized;

    // Diagnostic mean path cost. Candidate spaces differ by hypothesis kind,
    // so this is not yet a calibrated probability or cross-kind selector.
    double averageObjectiveCost = 0.0;
    bool selected = false;
    TempoTrack track;
};

enum class PeriodicityRelationKind {
    Harmonic,
    RationalApproximation,
    Unresolved,
};

// A local periodicity related to the selected structural tempo hypothesis.
// This is evidence for a future rhythm/subdivision model, not yet a semantic
// assertion that the relation is a subdivision or polyrhythm. Rational values
// are bounded approximations and may still be coincidental.
struct PeriodicityLayer {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double observedRateBpm = 0.0;
    double referenceTempoBpm = 0.0;
    double relativeRate = 1.0;
    std::uint32_t ratioNumerator = 0;
    std::uint32_t ratioDenominator = 0;
    double confidence = 0.0;
    PeriodicityRelationKind relation = PeriodicityRelationKind::Unresolved;
    std::vector<std::size_t> supportingWindowIds;
};

struct AnalysisConfidence {
    double tempo = 0.0;
    double tempoFamily = 0.0;
    double reliableCoverage = 0.0;
    double overall = 0.0;
};

struct AnalysisMetadata {
    std::size_t frameCount = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    double durationSeconds = 0.0;
};

struct AnalysisDiagnostics {
    std::vector<AnalysisWindow> windows;
    std::vector<AnchorRegion> anchorRegions;
};

// This is an intentionally extensible early contract. Tempo maps, pulse maps,
// meter, warp, and rhythm layers will be added without projecting them into the
// legacy AutoTimingResult fields.
struct AnalysisResult {
    AnalysisMetadata metadata;
    std::vector<GlobalTempoCandidate> tempoCandidates;
    std::vector<GlobalTempoFamily> tempoFamilies;
    TempoTrack tempoTrack;
    std::vector<TempoTrackHypothesis> tempoHypotheses;
    std::vector<PeriodicityLayer> periodicityLayers;
    std::vector<UncertainRegion> uncertainRegions;
    AnalysisConfidence confidence;
    AnalysisDiagnostics diagnostics;
};

AnalysisResult analyze(
    const AudioView& audio,
    const AnalysisOptions& options = AnalysisOptions{});

const char* toString(WindowScale scale) noexcept;
const char* toString(TempoTrackState state) noexcept;
const char* toString(TempoPropagationReason reason) noexcept;
const char* toString(TempoHypothesisKind kind) noexcept;
const char* toString(PeriodicityRelationKind kind) noexcept;
const char* toString(EvidenceReason reason) noexcept;
const char* toString(UncertaintyReason reason) noexcept;

} // namespace autotiming

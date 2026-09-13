// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#include "autotiming/Analysis.h"

#include "AutoTiming.h"
#include "Periodicity.h"
#include "RhythmLayers.h"
#include "TempoTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace autotiming {
namespace {

constexpr int LegacyFmodPcmFloat = 5;
constexpr double MinimumDecibels = -120.0;

double clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

double linearScore(double value, double zeroAt, double oneAt)
{
    if (oneAt <= zeroAt) {
        return value >= oneAt ? 1.0 : 0.0;
    }
    return clamp01((value - zeroAt) / (oneAt - zeroAt));
}

double amplitudeToDb(double amplitude)
{
    if (!(amplitude > 0.0) || !std::isfinite(amplitude)) {
        return MinimumDecibels;
    }
    return std::max(MinimumDecibels, 20.0 * std::log10(amplitude));
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) {
        return MinimumDecibels;
    }
    std::sort(values.begin(), values.end());
    const double position = clamp01(fraction) * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    const double mix = position - static_cast<double>(lower);
    return values[lower] * (1.0 - mix) + values[upper] * mix;
}

void validate(const AudioView& audio, const AnalysisOptions& options)
{
    if (audio.frameCount > 0 && audio.interleavedSamples == nullptr) {
        throw std::invalid_argument("analyze: samples are null");
    }
    if (audio.sampleRate == 0) {
        throw std::invalid_argument("analyze: sample rate is zero");
    }
    if (audio.channels == 0 || audio.channels > 64) {
        throw std::invalid_argument("analyze: channel count is outside 1..64");
    }
    if (audio.sampleRate != 32000 && audio.sampleRate != 44100 && audio.sampleRate != 48000) {
        throw std::invalid_argument(
            "analyze: the initial legacy evidence bridge supports only 32000, 44100, or 48000 Hz");
    }
    if (!(options.minimumWindowSeconds > 0.0)) {
        throw std::invalid_argument("analyze: minimum window duration must be positive");
    }
    if (!(options.minimumTempoBpm > 0.0) ||
        !(options.maximumTempoBpm > options.minimumTempoBpm)) {
        throw std::invalid_argument("analyze: invalid tempo range");
    }
    if (!(options.tempoAgreementTolerance > 0.0) || options.tempoAgreementTolerance > 0.25) {
        throw std::invalid_argument("analyze: tempo agreement tolerance is outside (0, 0.25]");
    }
    if (options.anchorReliabilityThreshold < 0.0 || options.anchorReliabilityThreshold > 1.0) {
        throw std::invalid_argument("analyze: anchor reliability threshold is outside [0, 1]");
    }
    if (options.minimumOnsetPeriodicityScore < 0.0 ||
        options.minimumOnsetPeriodicityScore > 1.0) {
        throw std::invalid_argument("analyze: onset periodicity threshold is outside [0, 1]");
    }
    if (options.maximumLocalTempoCandidates == 0) {
        throw std::invalid_argument("analyze: maximum local tempo candidate count is zero");
    }
    if (options.maximumGlobalTempoCandidates == 0) {
        throw std::invalid_argument("analyze: maximum global tempo candidate count is zero");
    }
    if (options.maximumPeriodicityLayers == 0) {
        throw std::invalid_argument("analyze: maximum periodicity layer count is zero");
    }
    if (!(options.maximumTrackedGapSeconds > 0.0)) {
        throw std::invalid_argument("analyze: maximum tracked gap must be positive");
    }
    if (options.maximumPhaseBackPropagationSeconds < 0.0) {
        throw std::invalid_argument("analyze: maximum phase back-propagation must not be negative");
    }
    if (!(options.maximumContinuousTempoSlopeOctavesPerSecond > 0.0)) {
        throw std::invalid_argument("analyze: maximum continuous tempo slope must be positive");
    }
    if (options.tempoTransitionPenalty < 0.0 ||
        options.tempoSlopeChangePenalty < 0.0 ||
        options.phaseTransitionPenalty < 0.0) {
        throw std::invalid_argument("analyze: tracker penalties must not be negative");
    }
    if (options.windowSpecs.empty()) {
        throw std::invalid_argument("analyze: at least one window scale is required");
    }
    for (const WindowSpec& spec : options.windowSpecs) {
        if (!(spec.durationSeconds > 0.0) || !(spec.hopSeconds > 0.0)) {
            throw std::invalid_argument("analyze: window duration and hop must be positive");
        }
    }
}

std::vector<float> mixToMono(const AudioView& audio)
{
    std::vector<float> mono(audio.frameCount, 0.0f);
    const double channelScale = 1.0 / static_cast<double>(audio.channels);
    for (std::size_t frame = 0; frame < audio.frameCount; ++frame) {
        double sum = 0.0;
        for (std::uint32_t channel = 0; channel < audio.channels; ++channel) {
            const float sample = audio.interleavedSamples[frame * audio.channels + channel];
            if (std::isfinite(sample)) {
                sum += sample;
            }
        }
        mono[frame] = static_cast<float>(sum * channelScale);
    }
    return mono;
}

SignalMetrics measureSignal(
    const std::vector<float>& mono,
    std::size_t firstFrame,
    std::size_t frameCount,
    std::uint32_t sampleRate)
{
    SignalMetrics metrics;
    if (frameCount == 0) {
        metrics.rmsDb = MinimumDecibels;
        return metrics;
    }

    double squareSum = 0.0;
    std::size_t silentSamples = 0;
    std::size_t clippedSamples = 0;
    for (std::size_t i = 0; i < frameCount; ++i) {
        const double sample = mono[firstFrame + i];
        const double magnitude = std::abs(sample);
        squareSum += sample * sample;
        metrics.peakAmplitude = std::max(metrics.peakAmplitude, magnitude);
        silentSamples += magnitude < 1.0e-4 ? 1U : 0U;
        clippedSamples += magnitude >= 0.999 ? 1U : 0U;
    }

    const double rms = std::sqrt(squareSum / static_cast<double>(frameCount));
    metrics.rmsDb = amplitudeToDb(rms);
    metrics.silenceRatio = static_cast<double>(silentSamples) / static_cast<double>(frameCount);
    metrics.clippingRatio = static_cast<double>(clippedSamples) / static_cast<double>(frameCount);

    const std::size_t blockFrames = std::max<std::size_t>(1, sampleRate / 50U);
    std::vector<double> blockDb;
    for (std::size_t offset = 0; offset < frameCount; offset += blockFrames) {
        const std::size_t count = std::min(blockFrames, frameCount - offset);
        double blockSquares = 0.0;
        for (std::size_t j = 0; j < count; ++j) {
            const double sample = mono[firstFrame + offset + j];
            blockSquares += sample * sample;
        }
        blockDb.push_back(amplitudeToDb(std::sqrt(blockSquares / static_cast<double>(count))));
    }

    metrics.dynamicRangeDb = std::max(0.0, percentile(blockDb, 0.9) - percentile(blockDb, 0.1));

    std::size_t transientCount = 0;
    for (std::size_t i = 1; i < blockDb.size(); ++i) {
        if (blockDb[i] > -55.0 && blockDb[i] - blockDb[i - 1] >= 3.0) {
            ++transientCount;
        }
    }
    const double durationSeconds = static_cast<double>(frameCount) / sampleRate;
    metrics.transientDensityHz = durationSeconds > 0.0
        ? static_cast<double>(transientCount) / durationSeconds
        : 0.0;

    const double levelScore = linearScore(metrics.rmsDb, -62.0, -28.0);
    const double activeScore = 1.0 - linearScore(metrics.silenceRatio, 0.85, 0.995);
    metrics.signalScore = clamp01(0.7 * levelScore + 0.3 * activeScore);

    const double densityScore = linearScore(metrics.transientDensityHz, 0.15, 1.5);
    const double rangeScore = linearScore(metrics.dynamicRangeDb, 2.0, 15.0);
    metrics.transientScore = clamp01(0.65 * densityScore + 0.35 * rangeScore);
    return metrics;
}

double tempoEvidenceScore(const AutoTiming::AutoTimingResult& result)
{
    if (!(result.bpm > 0.0) || !std::isfinite(result.bpm) ||
        !(result.rawBpm > 0.0) || !std::isfinite(result.rawBpm)) {
        return 0.0;
    }

    const double uncertainty = std::isfinite(result.rawBpmUncertainty)
        ? std::abs(result.rawBpmUncertainty)
        : result.rawBpm;
    const double relativeUncertainty = uncertainty / result.rawBpm;
    return clamp01(1.0 / (1.0 + relativeUncertainty * 4000.0));
}

TempoCandidate makePrimaryCandidate(
    const AutoTiming::AutoTimingResult& legacy,
    double windowStartSeconds,
    double score)
{
    TempoCandidate candidate;
    candidate.bpm = legacy.bpm;
    candidate.rawBpm = legacy.rawBpm;
    candidate.periodSeconds = 60.0 / legacy.bpm;
    candidate.legacyOffsetMilliseconds = legacy.offset;
    candidate.rawBpmUncertainty = legacy.rawBpmUncertainty;
    candidate.phaseConfidence = score;
    candidate.score = score;
    candidate.signature = legacy.signature;
    candidate.division = legacy.division;

    double localPulse = std::fmod(
        candidate.periodSeconds - legacy.offset / 1000.0,
        candidate.periodSeconds);
    if (localPulse < 0.0) {
        localPulse += candidate.periodSeconds;
    }
    if (std::abs(localPulse - candidate.periodSeconds) < 1.0e-9) {
        localPulse = 0.0;
    }
    candidate.pulseTimeSeconds = windowStartSeconds + localPulse;
    return candidate;
}

TempoCandidate makePeriodicityCandidate(
    const detail::PeriodicityEstimate& estimate,
    double windowStartSeconds)
{
    TempoCandidate candidate;
    candidate.bpm = estimate.bpm;
    candidate.rawBpm = estimate.bpm;
    candidate.periodSeconds = 60.0 / estimate.bpm;
    candidate.pulseTimeSeconds = windowStartSeconds + estimate.phaseSeconds;
    candidate.legacyOffsetMilliseconds = std::numeric_limits<double>::quiet_NaN();
    candidate.rawBpmUncertainty = estimate.uncertaintyBpm;
    candidate.phaseConfidence = estimate.phaseConfidence;
    candidate.score = estimate.score;
    candidate.origin = CandidateOrigin::OnsetEnvelopePeriodicity;
    return candidate;
}

TempoCandidate makeAlias(const TempoCandidate& primary, double ratio)
{
    TempoCandidate alias = primary;
    alias.bpm *= ratio;
    alias.periodSeconds = 60.0 / alias.bpm;
    alias.score *= 0.35;
    alias.harmonicRatio = ratio;
    alias.origin = CandidateOrigin::HarmonicAlias;
    return alias;
}

void addReason(AnalysisWindow& window, EvidenceReason reason)
{
    if (std::find(window.reasons.begin(), window.reasons.end(), reason) == window.reasons.end()) {
        window.reasons.push_back(reason);
    }
}

AnalysisWindow analyzeWindow(
    const std::vector<float>& mono,
    std::size_t id,
    WindowScale scale,
    std::size_t firstFrame,
    std::size_t frameCount,
    std::uint32_t sampleRate,
    const AnalysisOptions& options)
{
    AnalysisWindow window;
    window.id = id;
    window.scale = scale;
    window.startSeconds = static_cast<double>(firstFrame) / sampleRate;
    window.endSeconds = static_cast<double>(firstFrame + frameCount) / sampleRate;
    window.signal = measureSignal(mono, firstFrame, frameCount, sampleRate);

    if (window.signal.signalScore < 0.25) {
        addReason(window, EvidenceReason::LowSignal);
    }
    if (window.signal.transientScore < 0.25) {
        addReason(window, EvidenceReason::SparseTransients);
    }
    if (window.signal.clippingRatio > 0.05) {
        addReason(window, EvidenceReason::ExcessiveClipping);
    }

    std::vector<float> samples(
        mono.begin() + static_cast<std::ptrdiff_t>(firstFrame),
        mono.begin() + static_cast<std::ptrdiff_t>(firstFrame + frameCount));
    const std::size_t byteCount = samples.size() * sizeof(float);
    if (byteCount > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("analyze: one analysis window exceeds the legacy bridge size limit");
    }

    const auto periodicityEstimates = detail::estimateOnsetPeriodicity(
        samples.data(),
        samples.size(),
        sampleRate,
        options.minimumTempoBpm,
        options.maximumTempoBpm,
        options.minimumOnsetPeriodicityScore,
        options.maximumLocalTempoCandidates);
    for (const detail::PeriodicityEstimate& estimate : periodicityEstimates) {
        TempoCandidate candidate = makePeriodicityCandidate(
            estimate,
            window.startSeconds);
        const double signalGate = 0.35 + 0.65 * window.signal.signalScore;
        const double transientGate = 0.30 + 0.70 * window.signal.transientScore;
        candidate.score *= signalGate * transientGate;
        if (candidate.score >= options.minimumOnsetPeriodicityScore * 0.5) {
            window.onsetPeriodicityEvidence = std::max(
                window.onsetPeriodicityEvidence,
                candidate.score);
            window.tempoCandidates.push_back(candidate);
        }
    }

    try {
        const AutoTiming::AutoTimingResult legacy = AutoTiming::detect(
            reinterpret_cast<const char*>(samples.data()),
            static_cast<std::uint32_t>(byteCount),
            LegacyFmodPcmFloat,
            static_cast<int>(sampleRate),
            1);
        window.legacyTempoEvidence = tempoEvidenceScore(legacy);

        if (window.legacyTempoEvidence > 0.0 &&
            legacy.bpm >= options.minimumTempoBpm &&
            legacy.bpm <= options.maximumTempoBpm) {
            TempoCandidate primary = makePrimaryCandidate(
                legacy,
                window.startSeconds,
                window.legacyTempoEvidence);
            const TempoCandidate* phaseSupport = nullptr;
            for (const TempoCandidate& candidate : window.tempoCandidates) {
                if (candidate.origin != CandidateOrigin::OnsetEnvelopePeriodicity ||
                    std::abs(candidate.bpm / primary.bpm - 1.0) >
                        options.tempoAgreementTolerance) {
                    continue;
                }
                if (phaseSupport == nullptr ||
                    candidate.phaseConfidence * candidate.score >
                        phaseSupport->phaseConfidence * phaseSupport->score) {
                    phaseSupport = &candidate;
                }
            }
            if (phaseSupport != nullptr) {
                primary.pulseTimeSeconds = phaseSupport->pulseTimeSeconds;
                primary.phaseConfidence = std::max(
                    primary.phaseConfidence,
                    phaseSupport->phaseConfidence);
            }
            window.tempoCandidates.push_back(primary);

            for (double ratio : {0.5, 2.0}) {
                const double aliasBpm = primary.bpm * ratio;
                if (aliasBpm >= options.minimumTempoBpm && aliasBpm <= options.maximumTempoBpm) {
                    window.tempoCandidates.push_back(makeAlias(primary, ratio));
                }
            }
        }
    } catch (const std::exception& error) {
        window.estimatorMessage = error.what();
        window.legacyTempoEvidence = 0.0;
        addReason(window, EvidenceReason::EstimatorFailure);
    }

    window.tempoEvidence = std::max(
        window.legacyTempoEvidence,
        window.onsetPeriodicityEvidence);
    const bool hasRootCandidate = std::any_of(
        window.tempoCandidates.begin(),
        window.tempoCandidates.end(),
        [](const TempoCandidate& candidate) {
            return candidate.origin != CandidateOrigin::HarmonicAlias;
        });
    if (!hasRootCandidate) {
        addReason(window, EvidenceReason::NoTempoCandidate);
    }

    if (window.tempoEvidence > 0.0 && window.tempoEvidence < 0.5) {
        addReason(window, EvidenceReason::HighTempoUncertainty);
    }

    const double clippingPenalty = 1.0 - std::min(0.75, window.signal.clippingRatio * 5.0);
    window.reliability = clamp01(
        (0.25 * window.signal.signalScore +
         0.35 * window.signal.transientScore +
         0.40 * window.tempoEvidence) * clippingPenalty);
    if (window.signal.signalScore < 0.1) {
        window.reliability *= 0.2;
    }
    if (window.signal.transientScore < 0.1) {
        window.reliability *= 0.5;
    }
    return window;
}

std::vector<std::pair<std::size_t, std::size_t>> makeWindowFrames(
    std::size_t totalFrames,
    std::uint32_t sampleRate,
    const WindowSpec& spec,
    double minimumWindowSeconds)
{
    std::vector<std::pair<std::size_t, std::size_t>> windows;
    const std::size_t minimumFrames = static_cast<std::size_t>(
        std::ceil(minimumWindowSeconds * sampleRate));
    if (totalFrames < minimumFrames) {
        return windows;
    }

    const std::size_t requestedFrames = std::max<std::size_t>(
        minimumFrames,
        static_cast<std::size_t>(std::llround(spec.durationSeconds * sampleRate)));
    const std::size_t windowFrames = std::min(totalFrames, requestedFrames);
    const std::size_t hopFrames = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::llround(spec.hopSeconds * sampleRate)));
    const std::size_t lastStart = totalFrames - windowFrames;

    for (std::size_t start = 0; start < lastStart;) {
        windows.emplace_back(start, windowFrames);
        if (lastStart - start <= hopFrames) {
            break;
        }
        start += hopFrames;
    }
    if (windows.empty() || windows.back().first != lastStart) {
        windows.emplace_back(lastStart, windowFrames);
    }
    return windows;
}

const TempoCandidate* primaryCandidate(const AnalysisWindow& window)
{
    const TempoCandidate* best = nullptr;
    for (const TempoCandidate& candidate : window.tempoCandidates) {
        if (candidate.origin == CandidateOrigin::HarmonicAlias) {
            continue;
        }
        if (best == nullptr || candidate.score > best->score) {
            best = &candidate;
        }
    }
    return best;
}

bool windowsOverlap(const AnalysisWindow& left, const AnalysisWindow& right)
{
    return std::min(left.endSeconds, right.endSeconds) >
        std::max(left.startSeconds, right.startSeconds);
}

bool temposAgree(double leftBpm, double rightBpm, double tolerance)
{
    if (!(leftBpm > 0.0) || !(rightBpm > 0.0)) {
        return false;
    }
    const double ratio = leftBpm / rightBpm;
    for (double expected : {0.5, 1.0, 2.0}) {
        if (std::abs(ratio / expected - 1.0) <= tolerance) {
            return true;
        }
    }
    return false;
}

void applyCrossScaleConsistency(
    std::vector<AnalysisWindow>& windows,
    const AnalysisOptions& options)
{
    for (AnalysisWindow& window : windows) {
        const TempoCandidate* primary = primaryCandidate(window);
        if (primary == nullptr) {
            window.crossScaleConsistency = 0.0;
            window.consistencyPeerCount = 0;
            window.reliability = std::min(window.reliability, 0.2);
            continue;
        }

        double agreeingWeight = 0.0;
        double totalWeight = 0.0;
        for (const AnalysisWindow& peer : windows) {
            if (peer.id == window.id || peer.scale == window.scale || !windowsOverlap(window, peer)) {
                continue;
            }
            const TempoCandidate* peerPrimary = primaryCandidate(peer);
            if (peerPrimary == nullptr) {
                continue;
            }

            const double overlap = std::min(window.endSeconds, peer.endSeconds) -
                std::max(window.startSeconds, peer.startSeconds);
            const double peerDuration = peer.endSeconds - peer.startSeconds;
            const double overlapWeight = clamp01(overlap / std::max(0.001, peerDuration));
            const double weight = std::max(0.05, peer.tempoEvidence) *
                (0.25 + 0.75 * overlapWeight);
            totalWeight += weight;
            ++window.consistencyPeerCount;
            if (temposAgree(primary->bpm, peerPrimary->bpm, options.tempoAgreementTolerance)) {
                agreeingWeight += weight;
            }
        }

        window.crossScaleConsistency = totalWeight > 0.0
            ? clamp01(agreeingWeight / totalWeight)
            : 0.5;

        const double clippingPenalty =
            1.0 - std::min(0.75, window.signal.clippingRatio * 5.0);
        window.reliability = clamp01(
            (0.18 * window.signal.signalScore +
             0.22 * window.signal.transientScore +
             0.25 * window.tempoEvidence +
             0.35 * window.crossScaleConsistency) * clippingPenalty);
        if (window.signal.signalScore < 0.1) {
            window.reliability *= 0.2;
        }
        if (window.signal.transientScore < 0.1) {
            window.reliability *= 0.5;
        }
        if (window.consistencyPeerCount > 0 && window.crossScaleConsistency < 0.45) {
            addReason(window, EvidenceReason::CrossScaleDisagreement);
        }
    }
}

double shortestConfiguredDuration(const AnalysisOptions& options)
{
    double duration = std::numeric_limits<double>::infinity();
    for (const WindowSpec& spec : options.windowSpecs) {
        duration = std::min(duration, spec.durationSeconds);
    }
    return duration;
}

double normalizeTempoNear(double bpm, double referenceBpm)
{
    double best = bpm;
    double bestDistance = std::abs(bpm - referenceBpm);
    for (double ratio : {0.5, 2.0}) {
        const double value = bpm * ratio;
        const double distance = std::abs(value - referenceBpm);
        if (distance < bestDistance) {
            best = value;
            bestDistance = distance;
        }
    }
    return best;
}

std::vector<AnchorRegion> selectAnchorRegions(
    std::vector<AnalysisWindow>& windows,
    const AnalysisOptions& options)
{
    const double shortestDuration = shortestConfiguredDuration(options);
    std::vector<AnalysisWindow*> selected;
    for (AnalysisWindow& window : windows) {
        const double duration = window.endSeconds - window.startSeconds;
        const bool shortestScale = duration <= shortestDuration + 1.0e-6;
        if (shortestScale && primaryCandidate(window) != nullptr &&
            window.reliability >= options.anchorReliabilityThreshold) {
            selected.push_back(&window);
        }
    }

    // For audio shorter than every configured scale, all scales collapse to the
    // same full-length window. Select only the strongest duplicate.
    if (selected.empty()) {
        auto best = std::max_element(
            windows.begin(),
            windows.end(),
            [](const AnalysisWindow& left, const AnalysisWindow& right) {
                return left.reliability < right.reliability;
            });
        if (best != windows.end() && primaryCandidate(*best) != nullptr &&
            best->reliability >= options.anchorReliabilityThreshold) {
            selected.push_back(&*best);
        }
    }

    std::sort(selected.begin(), selected.end(), [](const AnalysisWindow* left, const AnalysisWindow* right) {
        if (left->startSeconds != right->startSeconds) {
            return left->startSeconds < right->startSeconds;
        }
        return left->endSeconds < right->endSeconds;
    });

    std::vector<AnchorRegion> regions;
    double regionWeight = 0.0;
    for (AnalysisWindow* window : selected) {
        window->selectedAsAnchor = true;
        addReason(*window, EvidenceReason::AnchorSelected);
        const TempoCandidate* primary = primaryCandidate(*window);
        const double weight = std::max(0.001, window->reliability);

        const bool extendsCurrent = !regions.empty() &&
            window->startSeconds <= regions.back().endSeconds + 1.0e-6 &&
            temposAgree(primary->bpm, regions.back().tempoBpm, options.tempoAgreementTolerance);
        if (!extendsCurrent) {
            AnchorRegion region;
            region.startSeconds = window->startSeconds;
            region.endSeconds = window->endSeconds;
            region.tempoBpm = primary->bpm;
            region.reliability = window->reliability;
            region.supportingWindowIds.push_back(window->id);
            regions.push_back(std::move(region));
            regionWeight = weight;
            continue;
        }

        AnchorRegion& region = regions.back();
        const double normalizedBpm = normalizeTempoNear(primary->bpm, region.tempoBpm);
        region.tempoBpm = (region.tempoBpm * regionWeight + normalizedBpm * weight) /
            (regionWeight + weight);
        region.reliability = (region.reliability * regionWeight + window->reliability * weight) /
            (regionWeight + weight);
        region.endSeconds = std::max(region.endSeconds, window->endSeconds);
        region.supportingWindowIds.push_back(window->id);
        regionWeight += weight;
    }
    return regions;
}

struct TempoCluster {
    double weightedBpm = 0.0;
    double locationWeight = 0.0;
    double evidenceWeight = 0.0;
    std::vector<std::size_t> supportingWindowIds;
};

void addToCluster(
    std::vector<TempoCluster>& clusters,
    double bpm,
    double evidenceWeight,
    double uncertaintyBpm,
    std::size_t windowId,
    double tolerance)
{
    auto cluster = std::find_if(clusters.begin(), clusters.end(), [=](const TempoCluster& value) {
        return value.locationWeight > 0.0 &&
            std::abs(bpm / (value.weightedBpm / value.locationWeight) - 1.0) <= tolerance;
    });
    const double precision = 1.0 / std::max(0.25, std::abs(uncertaintyBpm));
    const double locationWeight = evidenceWeight * std::min(16.0, precision * precision);
    if (cluster == clusters.end()) {
        TempoCluster value;
        value.weightedBpm = bpm * locationWeight;
        value.locationWeight = locationWeight;
        value.evidenceWeight = evidenceWeight;
        value.supportingWindowIds.push_back(windowId);
        clusters.push_back(std::move(value));
        return;
    }

    cluster->weightedBpm += bpm * locationWeight;
    cluster->locationWeight += locationWeight;
    cluster->evidenceWeight += evidenceWeight;
    if (std::find(
            cluster->supportingWindowIds.begin(),
            cluster->supportingWindowIds.end(),
            windowId) == cluster->supportingWindowIds.end()) {
        cluster->supportingWindowIds.push_back(windowId);
    }
}

std::vector<GlobalTempoCandidate> buildGlobalTempoCandidates(
    const std::vector<AnalysisWindow>& windows,
    const AnalysisOptions& options)
{
    const bool hasAnchors = std::any_of(windows.begin(), windows.end(), [](const AnalysisWindow& window) {
        return window.selectedAsAnchor;
    });

    std::vector<TempoCluster> clusters;
    for (const AnalysisWindow& window : windows) {
        if ((hasAnchors && !window.selectedAsAnchor) ||
            (!hasAnchors && window.reliability < 0.3)) {
            continue;
        }
        for (const TempoCandidate& candidate : window.tempoCandidates) {
            const double evidenceWeight = window.reliability * candidate.score;
            if (evidenceWeight > 0.0) {
                const double uncertaintyScale = candidate.origin == CandidateOrigin::HarmonicAlias
                    ? candidate.harmonicRatio
                    : 1.0;
                addToCluster(
                    clusters,
                    candidate.bpm,
                    evidenceWeight,
                    candidate.rawBpmUncertainty * uncertaintyScale,
                    window.id,
                    options.tempoAgreementTolerance);
            }
        }
    }

    std::sort(clusters.begin(), clusters.end(), [](const TempoCluster& left, const TempoCluster& right) {
        return left.evidenceWeight > right.evidenceWeight;
    });
    if (clusters.size() > options.maximumGlobalTempoCandidates) {
        clusters.resize(options.maximumGlobalTempoCandidates);
    }

    std::vector<GlobalTempoCandidate> candidates;
    const double bestWeight = clusters.empty() ? 0.0 : clusters.front().evidenceWeight;
    for (const TempoCluster& cluster : clusters) {
        GlobalTempoCandidate candidate;
        candidate.bpm = cluster.weightedBpm / cluster.locationWeight;
        candidate.relativeScore = bestWeight > 0.0
            ? cluster.evidenceWeight / bestWeight
            : 0.0;
        candidate.supportingWindowIds = cluster.supportingWindowIds;
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

double powerOfTwoResidual(double bpm, double referenceBpm)
{
    if (!(bpm > 0.0) || !(referenceBpm > 0.0)) {
        return std::numeric_limits<double>::infinity();
    }
    const double ratio = bpm / referenceBpm;
    const double nearestPower = std::exp2(std::round(std::log2(ratio)));
    return std::abs(ratio / nearestPower - 1.0);
}

std::vector<GlobalTempoFamily> assignHarmonicFamilies(
    std::vector<GlobalTempoCandidate>& candidates,
    std::vector<AnchorRegion>& anchors,
    double tolerance)
{
    std::vector<GlobalTempoFamily> families;
    for (std::size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
        GlobalTempoCandidate& candidate = candidates[candidateIndex];
        auto family = std::find_if(
            families.begin(),
            families.end(),
            [&](const GlobalTempoFamily& value) {
                return powerOfTwoResidual(candidate.bpm, value.referenceBpm) <= tolerance;
            });
        if (family == families.end()) {
            GlobalTempoFamily value;
            value.id = families.size();
            value.primaryCandidateIndex = candidateIndex;
            value.referenceBpm = candidate.bpm;
            value.relativeScore = candidate.relativeScore;
            value.memberCandidateIndices.push_back(candidateIndex);
            families.push_back(std::move(value));
            family = families.end() - 1;
        } else {
            family->relativeScore += candidate.relativeScore;
            family->memberCandidateIndices.push_back(candidateIndex);
        }
        candidate.harmonicFamilyId = family->id;
        candidate.harmonicRatioToFamily = candidate.bpm / family->referenceBpm;
    }

    double strongestFamily = 0.0;
    for (const GlobalTempoFamily& family : families) {
        strongestFamily = std::max(strongestFamily, family.relativeScore);
    }
    if (strongestFamily > 0.0) {
        for (GlobalTempoFamily& family : families) {
            family.relativeScore /= strongestFamily;
        }
    }

    for (AnchorRegion& anchor : anchors) {
        const GlobalTempoFamily* bestFamily = nullptr;
        double bestResidual = std::numeric_limits<double>::infinity();
        for (const GlobalTempoFamily& family : families) {
            const double residual = powerOfTwoResidual(anchor.tempoBpm, family.referenceBpm);
            if (residual < bestResidual) {
                bestResidual = residual;
                bestFamily = &family;
            }
        }
        if (bestFamily != nullptr && bestResidual <= tolerance) {
            anchor.harmonicFamilyId = bestFamily->id;
            anchor.harmonicRatioToFamily = anchor.tempoBpm / bestFamily->referenceBpm;
        }
    }
    return families;
}

UncertaintyReason uncertaintyReasonFor(
    const std::vector<const AnalysisWindow*>& coveringWindows)
{
    const bool hasCandidate = std::any_of(
        coveringWindows.begin(),
        coveringWindows.end(),
        [](const AnalysisWindow* window) {
            return primaryCandidate(*window) != nullptr;
        });
    if (!hasCandidate) {
        return UncertaintyReason::NoTempoCandidate;
    }
    const bool hasConflict = std::any_of(
        coveringWindows.begin(),
        coveringWindows.end(),
        [](const AnalysisWindow* window) {
            return std::find(
                window->reasons.begin(),
                window->reasons.end(),
                EvidenceReason::CrossScaleDisagreement) != window->reasons.end();
        });
    return hasConflict
        ? UncertaintyReason::ConflictingTempoEvidence
        : UncertaintyReason::LowEvidence;
}

std::vector<UncertainRegion> buildUncertainRegions(
    const std::vector<AnalysisWindow>& windows,
    double durationSeconds,
    const AnalysisOptions& options)
{
    if (!(durationSeconds > 0.0)) {
        return {};
    }

    const double shortestDuration = shortestConfiguredDuration(options);
    std::vector<const AnalysisWindow*> localWindows;
    std::vector<double> boundaries = {0.0, durationSeconds};
    for (const AnalysisWindow& window : windows) {
        const double windowDuration = window.endSeconds - window.startSeconds;
        if (windowDuration <= shortestDuration + 1.0e-6) {
            localWindows.push_back(&window);
            boundaries.push_back(window.startSeconds);
            boundaries.push_back(window.endSeconds);
        }
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(
        std::unique(boundaries.begin(), boundaries.end(), [](double left, double right) {
            return std::abs(left - right) < 1.0e-9;
        }),
        boundaries.end());

    std::vector<UncertainRegion> regions;
    for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
        const double start = boundaries[i];
        const double end = boundaries[i + 1];
        if (!(end > start)) {
            continue;
        }
        const double midpoint = (start + end) * 0.5;
        std::vector<const AnalysisWindow*> covering;
        for (const AnalysisWindow* window : localWindows) {
            if (midpoint >= window->startSeconds && midpoint < window->endSeconds) {
                covering.push_back(window);
            }
        }

        const bool coveredByAnchor = std::any_of(
            covering.begin(),
            covering.end(),
            [](const AnalysisWindow* window) {
                return window->selectedAsAnchor;
            });
        if (coveredByAnchor) {
            continue;
        }

        double localConfidence = 0.0;
        for (const AnalysisWindow* window : covering) {
            localConfidence = std::max(localConfidence, window->reliability);
        }
        const UncertaintyReason reason = uncertaintyReasonFor(covering);

        if (!regions.empty() && regions.back().reason == reason &&
            std::abs(regions.back().endSeconds - start) < 1.0e-9) {
            const double previousDuration = regions.back().endSeconds - regions.back().startSeconds;
            const double addedDuration = end - start;
            regions.back().confidence =
                (regions.back().confidence * previousDuration + localConfidence * addedDuration) /
                (previousDuration + addedDuration);
            regions.back().endSeconds = end;
        } else {
            UncertainRegion region;
            region.startSeconds = start;
            region.endSeconds = end;
            region.confidence = localConfidence;
            region.reason = reason;
            regions.push_back(region);
        }
    }
    return regions;
}

AnalysisConfidence calculateConfidence(
    const std::vector<AnchorRegion>& anchors,
    const std::vector<GlobalTempoCandidate>& candidates,
    const std::vector<GlobalTempoFamily>& families,
    double durationSeconds)
{
    AnalysisConfidence confidence;
    if (anchors.empty() || candidates.empty() || !(durationSeconds > 0.0)) {
        return confidence;
    }

    double coveredSeconds = 0.0;
    double reliabilitySeconds = 0.0;
    for (const AnchorRegion& region : anchors) {
        const double regionDuration = region.endSeconds - region.startSeconds;
        coveredSeconds += regionDuration;
        reliabilitySeconds += regionDuration * region.reliability;
    }
    confidence.reliableCoverage = clamp01(coveredSeconds / durationSeconds);
    const double meanReliability = coveredSeconds > 0.0
        ? reliabilitySeconds / coveredSeconds
        : 0.0;
    const double runnerUp = candidates.size() > 1 ? candidates[1].relativeScore : 0.0;
    const double margin = clamp01(1.0 - runnerUp);
    const double coverageScore = linearScore(confidence.reliableCoverage, 0.05, 0.4);
    confidence.tempo = clamp01(
        0.55 * meanReliability + 0.25 * margin + 0.20 * coverageScore);

    double bestFamilyScore = 0.0;
    double secondFamilyScore = 0.0;
    for (const GlobalTempoFamily& family : families) {
        if (family.relativeScore >= bestFamilyScore) {
            secondFamilyScore = bestFamilyScore;
            bestFamilyScore = family.relativeScore;
        } else {
            secondFamilyScore = std::max(secondFamilyScore, family.relativeScore);
        }
    }
    const double familyMargin = clamp01(bestFamilyScore - secondFamilyScore);
    confidence.tempoFamily = clamp01(
        0.55 * meanReliability + 0.25 * familyMargin + 0.20 * coverageScore);
    confidence.overall = clamp01(0.7 * confidence.tempo + 0.3 * confidence.reliableCoverage);
    return confidence;
}

} // namespace

double AudioView::durationSeconds() const noexcept
{
    return sampleRate == 0 ? 0.0 : static_cast<double>(frameCount) / sampleRate;
}

AnalysisResult analyze(const AudioView& audio, const AnalysisOptions& options)
{
    validate(audio, options);

    AnalysisResult result;
    result.metadata.frameCount = audio.frameCount;
    result.metadata.sampleRate = audio.sampleRate;
    result.metadata.channels = audio.channels;
    result.metadata.durationSeconds = audio.durationSeconds();

    const std::vector<float> mono = mixToMono(audio);
    for (const WindowSpec& spec : options.windowSpecs) {
        const auto windows = makeWindowFrames(
            audio.frameCount,
            audio.sampleRate,
            spec,
            options.minimumWindowSeconds);
        for (const auto& frames : windows) {
            const std::size_t id = result.diagnostics.windows.size();
            result.diagnostics.windows.push_back(analyzeWindow(
                mono,
                id,
                spec.scale,
                frames.first,
                frames.second,
                audio.sampleRate,
                options));
        }
    }
    applyCrossScaleConsistency(result.diagnostics.windows, options);
    result.diagnostics.anchorRegions = selectAnchorRegions(
        result.diagnostics.windows,
        options);
    result.tempoCandidates = buildGlobalTempoCandidates(
        result.diagnostics.windows,
        options);
    result.tempoFamilies = assignHarmonicFamilies(
        result.tempoCandidates,
        result.diagnostics.anchorRegions,
        options.tempoAgreementTolerance);
    detail::TempoTrackingOutput tracking = detail::buildTempoTracks(
        result.diagnostics.windows,
        result.tempoFamilies,
        result.metadata.durationSeconds,
        options);
    result.tempoTrack = std::move(tracking.selectedTrack);
    result.tempoHypotheses = std::move(tracking.hypotheses);
    result.periodicityLayers = detail::buildPeriodicityLayers(
        result.diagnostics.windows,
        result.tempoTrack,
        options);
    result.uncertainRegions = buildUncertainRegions(
        result.diagnostics.windows,
        result.metadata.durationSeconds,
        options);
    result.confidence = calculateConfidence(
        result.diagnostics.anchorRegions,
        result.tempoCandidates,
        result.tempoFamilies,
        result.metadata.durationSeconds);
    return result;
}

const char* toString(WindowScale scale) noexcept
{
    switch (scale) {
    case WindowScale::Short:
        return "short";
    case WindowScale::Medium:
        return "medium";
    case WindowScale::Long:
        return "long";
    }
    return "unknown";
}

const char* toString(TempoTrackState state) noexcept
{
    switch (state) {
    case TempoTrackState::Observed:
        return "observed";
    case TempoTrackState::Propagated:
        return "propagated";
    case TempoTrackState::Uncertain:
        return "uncertain";
    }
    return "unknown";
}

const char* toString(TempoPropagationReason reason) noexcept
{
    switch (reason) {
    case TempoPropagationReason::None:
        return "none";
    case TempoPropagationReason::BoundedGapInterpolation:
        return "bounded_gap_interpolation";
    case TempoPropagationReason::StableFamilyCarry:
        return "stable_family_carry";
    case TempoPropagationReason::RationalRhythmProjection:
        return "rational_rhythm_projection";
    case TempoPropagationReason::StablePhaseBackPropagation:
        return "stable_phase_back_propagation";
    case TempoPropagationReason::LocalPhaseAnchor:
        return "local_phase_anchor";
    }
    return "unknown";
}

const char* toString(TempoHypothesisKind kind) noexcept
{
    switch (kind) {
    case TempoHypothesisKind::GloballyRegularized:
        return "globally_regularized";
    case TempoHypothesisKind::LocalEvidenceCurve:
        return "local_evidence_curve";
    }
    return "unknown";
}

const char* toString(PeriodicityRelationKind kind) noexcept
{
    switch (kind) {
    case PeriodicityRelationKind::Harmonic:
        return "harmonic";
    case PeriodicityRelationKind::RationalApproximation:
        return "rational_approximation";
    case PeriodicityRelationKind::Unresolved:
        return "unresolved";
    }
    return "unknown";
}

const char* toString(EvidenceReason reason) noexcept
{
    switch (reason) {
    case EvidenceReason::LowSignal:
        return "low_signal";
    case EvidenceReason::SparseTransients:
        return "sparse_transients";
    case EvidenceReason::ExcessiveClipping:
        return "excessive_clipping";
    case EvidenceReason::NoTempoCandidate:
        return "no_tempo_candidate";
    case EvidenceReason::HighTempoUncertainty:
        return "high_tempo_uncertainty";
    case EvidenceReason::CrossScaleDisagreement:
        return "cross_scale_disagreement";
    case EvidenceReason::EstimatorFailure:
        return "estimator_failure";
    case EvidenceReason::AnchorSelected:
        return "anchor_selected";
    }
    return "unknown";
}

const char* toString(UncertaintyReason reason) noexcept
{
    switch (reason) {
    case UncertaintyReason::LowEvidence:
        return "low_evidence";
    case UncertaintyReason::NoTempoCandidate:
        return "no_tempo_candidate";
    case UncertaintyReason::ConflictingTempoEvidence:
        return "conflicting_tempo_evidence";
    }
    return "unknown";
}

} // namespace autotiming

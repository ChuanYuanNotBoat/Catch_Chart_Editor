// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#include "RhythmLayers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace autotiming::detail {
namespace {

double clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

struct RationalRelation {
    std::uint32_t numerator = 0;
    std::uint32_t denominator = 0;
    double residual = std::numeric_limits<double>::infinity();
};

RationalRelation approximateRational(double ratio)
{
    RationalRelation best;
    std::uint32_t bestComplexity = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t denominator = 1; denominator <= 16; ++denominator) {
        for (std::uint32_t numerator = 1; numerator <= 32; ++numerator) {
            const double value = static_cast<double>(numerator) / denominator;
            const double residual = std::abs(ratio / value - 1.0);
            const std::uint32_t complexity = numerator + denominator;
            if (residual < best.residual - 1.0e-9 ||
                (std::abs(residual - best.residual) <= 1.0e-9 &&
                    complexity < bestComplexity)) {
                best.numerator = numerator;
                best.denominator = denominator;
                best.residual = residual;
                bestComplexity = complexity;
            }
        }
    }
    return best;
}

RationalRelation harmonicRelation(double ratio)
{
    RationalRelation relation;
    if (!(ratio > 0.0)) {
        return relation;
    }
    const int octave = static_cast<int>(std::llround(std::log2(ratio)));
    const double harmonic = std::exp2(static_cast<double>(octave));
    relation.residual = std::abs(ratio / harmonic - 1.0);
    if (octave >= 0 && octave <= 5) {
        relation.numerator = 1U << static_cast<unsigned>(octave);
        relation.denominator = 1;
    } else if (octave < 0 && octave >= -5) {
        relation.numerator = 1;
        relation.denominator = 1U << static_cast<unsigned>(-octave);
    }
    return relation;
}

const TempoTrackPoint* nearestTrackPoint(const TempoTrack& track, double timeSeconds)
{
    const TempoTrackPoint* nearest = nullptr;
    double nearestDistance = std::numeric_limits<double>::infinity();
    for (const TempoTrackPoint& point : track.points) {
        if (point.state == TempoTrackState::Uncertain || !(point.bpm > 0.0)) {
            continue;
        }
        const double distance = std::abs(point.timeSeconds - timeSeconds);
        if (distance < nearestDistance) {
            nearest = &point;
            nearestDistance = distance;
        }
    }
    return nearest;
}

double shortestWindowDuration(
    const std::vector<AnalysisWindow>& windows,
    const AnalysisOptions& options)
{
    double shortest = std::numeric_limits<double>::infinity();
    for (const WindowSpec& spec : options.windowSpecs) {
        shortest = std::min(
            shortest,
            std::max(options.minimumWindowSeconds, spec.durationSeconds));
    }
    if (std::isfinite(shortest)) {
        return shortest;
    }
    for (const AnalysisWindow& window : windows) {
        shortest = std::min(shortest, window.endSeconds - window.startSeconds);
    }
    return shortest;
}

struct LayerEvidence {
    double observedRateBpm = 0.0;
    double referenceTempoBpm = 0.0;
    double relativeRate = 1.0;
    double confidence = 0.0;
    std::uint32_t numerator = 0;
    std::uint32_t denominator = 0;
    PeriodicityRelationKind relation = PeriodicityRelationKind::Unresolved;
};

LayerEvidence classify(
    const TempoCandidate& candidate,
    const AnalysisWindow& window,
    double referenceBpm,
    double tolerance)
{
    LayerEvidence evidence;
    evidence.observedRateBpm = candidate.bpm;
    evidence.referenceTempoBpm = referenceBpm;
    evidence.relativeRate = candidate.bpm / referenceBpm;
    evidence.confidence = clamp01(candidate.score * (0.35 + 0.65 * window.reliability));

    const RationalRelation harmonic = harmonicRelation(evidence.relativeRate);
    if (harmonic.numerator > 0 && harmonic.residual <= tolerance) {
        evidence.numerator = harmonic.numerator;
        evidence.denominator = harmonic.denominator;
        evidence.relation = PeriodicityRelationKind::Harmonic;
        return evidence;
    }

    const RationalRelation rational = approximateRational(evidence.relativeRate);
    if (rational.residual <= tolerance) {
        evidence.numerator = rational.numerator;
        evidence.denominator = rational.denominator;
        evidence.relation = PeriodicityRelationKind::RationalApproximation;
    }
    return evidence;
}

struct LayerKey {
    PeriodicityRelationKind relation = PeriodicityRelationKind::Unresolved;
    std::uint32_t numerator = 0;
    std::uint32_t denominator = 0;
    int unresolvedBucket = 0;

    bool operator<(const LayerKey& other) const noexcept
    {
        if (relation != other.relation) {
            return relation < other.relation;
        }
        if (numerator != other.numerator) {
            return numerator < other.numerator;
        }
        if (denominator != other.denominator) {
            return denominator < other.denominator;
        }
        return unresolvedBucket < other.unresolvedBucket;
    }
};

LayerKey keyFor(const LayerEvidence& evidence)
{
    LayerKey key;
    key.relation = evidence.relation;
    key.numerator = evidence.numerator;
    key.denominator = evidence.denominator;
    if (evidence.relation == PeriodicityRelationKind::Unresolved) {
        key.unresolvedBucket = static_cast<int>(
            std::llround(std::log2(evidence.relativeRate) * 24.0));
    }
    return key;
}

struct LayerAccumulator {
    LayerKey key;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double weightedObservedRate = 0.0;
    double weightedReferenceTempo = 0.0;
    double weight = 0.0;
    double confidenceSum = 0.0;
    std::vector<std::size_t> windowIds;
};

} // namespace

std::vector<PeriodicityLayer> buildPeriodicityLayers(
    const std::vector<AnalysisWindow>& windows,
    const TempoTrack& tempoTrack,
    const AnalysisOptions& options)
{
    const double shortest = shortestWindowDuration(windows, options);
    std::vector<const AnalysisWindow*> localWindows;
    for (const AnalysisWindow& window : windows) {
        if (window.endSeconds - window.startSeconds <= shortest + 1.0e-6) {
            localWindows.push_back(&window);
        }
    }
    std::sort(localWindows.begin(), localWindows.end(), [](const AnalysisWindow* left, const AnalysisWindow* right) {
        return left->startSeconds < right->startSeconds;
    });

    std::vector<LayerAccumulator> accumulators;
    const double relationTolerance = std::max(0.012, options.tempoAgreementTolerance);
    for (const AnalysisWindow* window : localWindows) {
        const double center = (window->startSeconds + window->endSeconds) * 0.5;
        const TempoTrackPoint* trackPoint = nearestTrackPoint(tempoTrack, center);
        if (trackPoint == nullptr ||
            std::abs(trackPoint->timeSeconds - center) > shortest ||
            !(trackPoint->bpm > 0.0)) {
            continue;
        }

        std::map<LayerKey, LayerEvidence> strongestByRelation;
        for (const TempoCandidate& candidate : window->tempoCandidates) {
            if (candidate.origin == CandidateOrigin::HarmonicAlias ||
                candidate.score < 0.16 || !(candidate.bpm > 0.0)) {
                continue;
            }
            const double relativeRate = candidate.bpm / trackPoint->bpm;
            if (std::abs(relativeRate - 1.0) <= relationTolerance) {
                continue;
            }
            LayerEvidence evidence = classify(
                candidate,
                *window,
                trackPoint->bpm,
                relationTolerance);
            const LayerKey key = keyFor(evidence);
            auto existing = strongestByRelation.find(key);
            if (existing == strongestByRelation.end() ||
                evidence.confidence > existing->second.confidence) {
                strongestByRelation[key] = evidence;
            }
        }

        for (const auto& item : strongestByRelation) {
            const LayerKey& key = item.first;
            const LayerEvidence& evidence = item.second;
            auto accumulator = std::find_if(
                accumulators.rbegin(),
                accumulators.rend(),
                [&](const LayerAccumulator& value) {
                    return !(key < value.key) && !(value.key < key) &&
                        window->startSeconds <= value.endSeconds + 1.0e-6;
                });
            if (accumulator == accumulators.rend()) {
                LayerAccumulator value;
                value.key = key;
                value.startSeconds = window->startSeconds;
                value.endSeconds = window->endSeconds;
                value.weightedObservedRate =
                    evidence.observedRateBpm * evidence.confidence;
                value.weightedReferenceTempo =
                    evidence.referenceTempoBpm * evidence.confidence;
                value.weight = evidence.confidence;
                value.confidenceSum = evidence.confidence;
                value.windowIds.push_back(window->id);
                accumulators.push_back(std::move(value));
                continue;
            }

            accumulator->endSeconds = std::max(accumulator->endSeconds, window->endSeconds);
            accumulator->weightedObservedRate +=
                evidence.observedRateBpm * evidence.confidence;
            accumulator->weightedReferenceTempo +=
                evidence.referenceTempoBpm * evidence.confidence;
            accumulator->weight += evidence.confidence;
            accumulator->confidenceSum += evidence.confidence;
            accumulator->windowIds.push_back(window->id);
        }
    }

    std::vector<PeriodicityLayer> layers;
    for (const LayerAccumulator& accumulator : accumulators) {
        if (accumulator.windowIds.size() < 2 || !(accumulator.weight > 0.0)) {
            continue;
        }
        PeriodicityLayer layer;
        layer.startSeconds = accumulator.startSeconds;
        layer.endSeconds = accumulator.endSeconds;
        layer.observedRateBpm = accumulator.weightedObservedRate / accumulator.weight;
        layer.referenceTempoBpm = accumulator.weightedReferenceTempo / accumulator.weight;
        layer.relativeRate = layer.observedRateBpm / layer.referenceTempoBpm;
        layer.ratioNumerator = accumulator.key.numerator;
        layer.ratioDenominator = accumulator.key.denominator;
        layer.confidence = accumulator.confidenceSum /
            static_cast<double>(accumulator.windowIds.size());
        layer.relation = accumulator.key.relation;
        layer.supportingWindowIds = accumulator.windowIds;
        layers.push_back(std::move(layer));
    }
    std::sort(layers.begin(), layers.end(), [](const PeriodicityLayer& left, const PeriodicityLayer& right) {
        if (left.confidence != right.confidence) {
            return left.confidence > right.confidence;
        }
        return left.startSeconds < right.startSeconds;
    });
    if (layers.size() > options.maximumPeriodicityLayers) {
        layers.resize(options.maximumPeriodicityLayers);
    }
    return layers;
}

} // namespace autotiming::detail

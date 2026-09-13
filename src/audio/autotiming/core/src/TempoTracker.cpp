// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#include "TempoTracker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace autotiming::detail {
namespace {

double clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
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

double nearestPulse(double pulseTime, double referenceTime, double bpm)
{
    if (!(bpm > 0.0) || !std::isfinite(pulseTime)) {
        return referenceTime;
    }
    const double period = 60.0 / bpm;
    return pulseTime + std::round((referenceTime - pulseTime) / period) * period;
}

double phaseResidual(
    double firstPulse,
    double firstBpm,
    double secondPulse,
    double secondBpm)
{
    if (!(firstBpm > 0.0) || !(secondBpm > 0.0)) {
        return 0.5;
    }
    const double integratedBeats =
        (secondPulse - firstPulse) * (firstBpm + secondBpm) / 120.0;
    return std::abs(integratedBeats - std::round(integratedBeats));
}

struct LocalWindow {
    const AnalysisWindow* window = nullptr;
    double centerSeconds = 0.0;
};

std::vector<LocalWindow> collectLocalWindows(
    const std::vector<AnalysisWindow>& windows,
    const AnalysisOptions& options)
{
    double shortestDuration = std::numeric_limits<double>::infinity();
    for (const WindowSpec& spec : options.windowSpecs) {
        shortestDuration = std::min(
            shortestDuration,
            std::max(options.minimumWindowSeconds, spec.durationSeconds));
    }

    std::vector<LocalWindow> local;
    for (const AnalysisWindow& window : windows) {
        const double duration = window.endSeconds - window.startSeconds;
        if (duration > shortestDuration + 1.0e-6) {
            continue;
        }
        const double center = (window.startSeconds + window.endSeconds) * 0.5;
        auto duplicate = std::find_if(
            local.begin(),
            local.end(),
            [&](const LocalWindow& value) {
                return std::abs(value.window->startSeconds - window.startSeconds) < 1.0e-9 &&
                    std::abs(value.window->endSeconds - window.endSeconds) < 1.0e-9;
            });
        if (duplicate == local.end()) {
            local.push_back({&window, center});
        } else if (window.reliability > duplicate->window->reliability) {
            duplicate->window = &window;
            duplicate->centerSeconds = center;
        }
    }
    std::sort(local.begin(), local.end(), [](const LocalWindow& left, const LocalWindow& right) {
        return left.centerSeconds < right.centerSeconds;
    });
    return local;
}

struct Observation {
    double bpm = 0.0;
    double pulseTimeSeconds = 0.0;
    double quality = 0.0;
    double confidence = 0.0;
    double phaseConfidence = 0.0;
    std::size_t harmonicFamilyId = NoTempoFamily;
    double harmonicRatioToFamily = 1.0;
    TempoTrackState state = TempoTrackState::Observed;
    TempoPropagationReason propagationReason = TempoPropagationReason::None;
};

struct DominantFamily {
    const GlobalTempoFamily* family = nullptr;
    double runnerUpScore = 0.0;
};

DominantFamily findDominantFamily(
    const std::vector<GlobalTempoFamily>& families,
    const std::vector<LocalWindow>& localWindows,
    double familyTolerance)
{
    DominantFamily result;
    for (const GlobalTempoFamily& family : families) {
        if (result.family == nullptr || family.relativeScore > result.family->relativeScore) {
            result.runnerUpScore = result.family == nullptr
                ? result.runnerUpScore
                : result.family->relativeScore;
            result.family = &family;
        } else {
            result.runnerUpScore = std::max(result.runnerUpScore, family.relativeScore);
        }
    }
    if (result.family == nullptr || result.runnerUpScore > 0.72 || localWindows.empty()) {
        result.family = nullptr;
        return result;
    }

    const double firstCenter = localWindows.front().centerSeconds;
    const double lastCenter = localWindows.back().centerSeconds;
    const double span = std::max(0.0, lastCenter - firstCenter);
    const double earlyEnd = firstCenter + span * 0.30;
    const double lateStart = firstCenter + span * 0.70;
    bool hasEarlySupport = false;
    bool hasLateSupport = false;
    for (const LocalWindow& local : localWindows) {
        const bool isEarly = local.centerSeconds <= earlyEnd;
        const bool isLate = local.centerSeconds >= lateStart;
        if (!isEarly && !isLate) {
            continue;
        }
        const bool supportsFamily = std::any_of(
            local.window->tempoCandidates.begin(),
            local.window->tempoCandidates.end(),
            [&](const TempoCandidate& candidate) {
                return candidate.origin != CandidateOrigin::HarmonicAlias &&
                    candidate.score >= 0.30 &&
                    powerOfTwoResidual(
                        candidate.bpm,
                        result.family->referenceBpm) <= familyTolerance;
            });
        hasEarlySupport = hasEarlySupport || (isEarly && supportsFamily);
        hasLateSupport = hasLateSupport || (isLate && supportsFamily);
    }
    if (!hasEarlySupport || !hasLateSupport) {
        result.family = nullptr;
    }
    return result;
}

double simpleRationalResidual(double ratio)
{
    if (!(ratio > 0.0)) {
        return std::numeric_limits<double>::infinity();
    }
    double best = std::numeric_limits<double>::infinity();
    for (int denominator = 1; denominator <= 12; ++denominator) {
        for (int numerator = 1; numerator <= 12; ++numerator) {
            const double rational = static_cast<double>(numerator) / denominator;
            best = std::min(best, std::abs(ratio / rational - 1.0));
        }
    }
    return best;
}

Observation makeObservation(
    const TempoCandidate& candidate,
    const AnalysisWindow& window,
    const std::vector<GlobalTempoFamily>& families,
    double familyTolerance,
    bool normalizeFamilyRate)
{
    Observation observation;
    observation.bpm = candidate.bpm;
    observation.phaseConfidence = clamp01(candidate.phaseConfidence);

    const GlobalTempoFamily* bestFamily = nullptr;
    double bestResidual = std::numeric_limits<double>::infinity();
    for (const GlobalTempoFamily& family : families) {
        const double residual = powerOfTwoResidual(candidate.bpm, family.referenceBpm);
        if (residual < bestResidual) {
            bestResidual = residual;
            bestFamily = &family;
        }
    }
    if (bestFamily != nullptr && bestResidual <= familyTolerance) {
        const double ratio = candidate.bpm / bestFamily->referenceBpm;
        const double harmonicRatio = std::exp2(std::round(std::log2(ratio)));
        if (normalizeFamilyRate) {
            observation.bpm = candidate.bpm / harmonicRatio;
        }
        observation.harmonicFamilyId = bestFamily->id;
        observation.harmonicRatioToFamily = harmonicRatio;
    }

    const double center = (window.startSeconds + window.endSeconds) * 0.5;
    observation.pulseTimeSeconds = nearestPulse(
        candidate.pulseTimeSeconds,
        center,
        observation.bpm);
    observation.quality = clamp01(
        candidate.score *
        (0.25 + 0.75 * window.reliability) *
        (0.65 + 0.35 * observation.phaseConfidence));
    observation.confidence = clamp01(
        0.55 * candidate.score +
        0.30 * window.reliability +
        0.15 * observation.phaseConfidence);
    return observation;
}

bool equivalentObservation(const Observation& left, const Observation& right)
{
    if (!(left.bpm > 0.0) || !(right.bpm > 0.0) ||
        std::abs(left.bpm / right.bpm - 1.0) > 0.008) {
        return false;
    }
    return phaseResidual(
        left.pulseTimeSeconds,
        left.bpm,
        right.pulseTimeSeconds,
        right.bpm) < 0.12;
}

std::vector<Observation> collectObservations(
    const LocalWindow& local,
    const std::vector<GlobalTempoFamily>& families,
    const DominantFamily& dominant,
    const AnalysisOptions& options)
{
    std::vector<Observation> observations;
    if (local.window->reliability < 0.08 || local.window->signal.signalScore < 0.08) {
        return observations;
    }

    const double familyTolerance = std::max(0.025, options.tempoAgreementTolerance * 2.0);
    const auto addCandidate = [&](const TempoCandidate& candidate, bool normalizeFamilyRate) {
        Observation observation = makeObservation(
            candidate,
            *local.window,
            families,
            familyTolerance,
            normalizeFamilyRate);
        const double rawQuality = observation.quality;
        const double rawConfidence = observation.confidence;
        bool plausibleRhythmLayer = false;
        if (dominant.family != nullptr) {
            if (observation.harmonicFamilyId == dominant.family->id) {
                observation.quality = clamp01(observation.quality * 1.08);
            } else {
                plausibleRhythmLayer = simpleRationalResidual(
                    candidate.bpm / dominant.family->referenceBpm) <= 0.006;
                observation.quality *= plausibleRhythmLayer ? 0.42 : 0.72;
                observation.confidence *= plausibleRhythmLayer ? 0.72 : 0.88;
            }
        }
        auto duplicate = std::find_if(
            observations.begin(),
            observations.end(),
            [&](const Observation& value) {
                return equivalentObservation(value, observation);
            });
        if (duplicate == observations.end()) {
            observations.push_back(observation);
        } else if (observation.quality > duplicate->quality) {
            *duplicate = observation;
        }

        // A stable global pulse may remain valid while a local rhythm layer
        // presents a simple p:q periodicity. Preserve that lower-complexity
        // explanation as an inferred alternative for global model selection.
        if (dominant.family != nullptr && plausibleRhythmLayer) {
            Observation projection;
            projection.bpm = dominant.family->referenceBpm;
            projection.pulseTimeSeconds = nearestPulse(
                candidate.pulseTimeSeconds,
                local.centerSeconds,
                projection.bpm);
            projection.quality = clamp01(rawQuality * 0.58);
            projection.confidence = clamp01(rawConfidence * 0.65);
            projection.phaseConfidence = clamp01(candidate.phaseConfidence * 0.5);
            projection.harmonicFamilyId = dominant.family->id;
            projection.state = TempoTrackState::Propagated;
            projection.propagationReason =
                TempoPropagationReason::RationalRhythmProjection;
            auto projectedDuplicate = std::find_if(
                observations.begin(),
                observations.end(),
                [&](const Observation& value) {
                    return equivalentObservation(value, projection);
                });
            if (projectedDuplicate == observations.end()) {
                observations.push_back(projection);
            } else if (projection.quality > projectedDuplicate->quality) {
                *projectedDuplicate = projection;
            }
        }
    };

    for (const TempoCandidate& candidate : local.window->tempoCandidates) {
        if (candidate.origin == CandidateOrigin::HarmonicAlias ||
            !(candidate.bpm > 0.0) || candidate.score < 0.05) {
            continue;
        }
        addCandidate(candidate, true);

        // Periodicity peaks often expose the same pulse layer at adjacent
        // octaves. Keep explicit tracker alternatives so a curve cannot be
        // forced to switch branches merely because one octave crosses the
        // configured estimator range.
        for (double ratio : {0.5, 2.0}) {
            TempoCandidate alternative = candidate;
            alternative.bpm *= ratio;
            if (alternative.bpm < options.minimumTempoBpm ||
                alternative.bpm > options.maximumTempoBpm) {
                continue;
            }
            alternative.rawBpm *= ratio;
            alternative.rawBpmUncertainty *= ratio;
            alternative.periodSeconds = 60.0 / alternative.bpm;
            const bool plausibleStructuralHalfRate = ratio == 0.5 &&
                alternative.bpm >= 90.0 && alternative.bpm <= 240.0;
            const double octaveEvidenceFactor = plausibleStructuralHalfRate
                ? (candidate.bpm >= 200.0 ? 1.0 : 0.88)
                : 0.72;
            alternative.score *= octaveEvidenceFactor;
            addCandidate(alternative, false);
        }
    }

    // Keep an explicit low-confidence carry state. It lets the model abstain
    // from reinterpreting a brief texture/outro periodicity as structural
    // tempo, while sustained high-quality change evidence can still win.
    if (dominant.family != nullptr) {
        Observation carry;
        carry.bpm = dominant.family->referenceBpm;
        carry.pulseTimeSeconds = local.centerSeconds;
        carry.quality = clamp01(0.035 + 0.10 * local.window->reliability);
        carry.confidence = carry.quality;
        carry.harmonicFamilyId = dominant.family->id;
        carry.state = TempoTrackState::Propagated;
        carry.propagationReason = TempoPropagationReason::StableFamilyCarry;
        const Observation* localPhaseAnchor = nullptr;
        for (const Observation& observation : observations) {
            if (!(observation.bpm > 0.0) || observation.phaseConfidence <= 0.0 ||
                std::abs(observation.bpm / carry.bpm - 1.0) > 0.12) {
                continue;
            }
            if (localPhaseAnchor == nullptr ||
                observation.quality * observation.phaseConfidence >
                    localPhaseAnchor->quality * localPhaseAnchor->phaseConfidence) {
                localPhaseAnchor = &observation;
            }
        }
        if (localPhaseAnchor != nullptr) {
            carry.pulseTimeSeconds = localPhaseAnchor->pulseTimeSeconds;
            carry.phaseConfidence = clamp01(
                localPhaseAnchor->phaseConfidence * 0.45);
            carry.propagationReason = TempoPropagationReason::LocalPhaseAnchor;
        }
        const bool alreadyHasDominant = std::any_of(
            observations.begin(),
            observations.end(),
            [&](const Observation& value) {
                return value.harmonicFamilyId == dominant.family->id &&
                    std::abs(value.bpm / carry.bpm - 1.0) <= 0.008;
            });
        if (!alreadyHasDominant) {
            observations.push_back(carry);
        }
    }

    Observation dominantAlternative;
    bool hasDominantAlternative = false;
    if (dominant.family != nullptr) {
        for (const Observation& observation : observations) {
            if (observation.harmonicFamilyId == dominant.family->id &&
                (!hasDominantAlternative || observation.quality > dominantAlternative.quality)) {
                dominantAlternative = observation;
                hasDominantAlternative = true;
            }
        }
    }
    std::sort(observations.begin(), observations.end(), [](const Observation& left, const Observation& right) {
        return left.quality > right.quality;
    });
    const std::size_t limit = std::max<std::size_t>(2, options.maximumLocalTempoCandidates);
    if (observations.size() > limit) {
        observations.resize(limit);
    }
    if (hasDominantAlternative && !std::any_of(
            observations.begin(),
            observations.end(),
            [&](const Observation& observation) {
                return observation.harmonicFamilyId == dominant.family->id;
            })) {
        observations.back() = dominantAlternative;
    }
    return observations;
}

double transitionCost(
    const Observation* previousPrevious,
    double previousPreviousTime,
    const Observation& previous,
    double previousTime,
    const Observation& current,
    double currentTime,
    const AnalysisOptions& options)
{
    const double elapsed = std::max(0.001, currentTime - previousTime);
    const double octaveChange = std::abs(std::log2(current.bpm / previous.bpm));
    const double slope = octaveChange / elapsed;
    double smoothTempoCost = options.tempoTransitionPenalty * octaveChange;
    if (slope > options.maximumContinuousTempoSlopeOctavesPerSecond) {
        smoothTempoCost += 4.0 *
            (slope - options.maximumContinuousTempoSlopeOctavesPerSecond) * elapsed;
    }
    const double changePointCost = 1.10 + 0.40 * octaveChange;
    double cost = std::min(smoothTempoCost, changePointCost);

    if (previousPrevious != nullptr) {
        const double previousElapsed = std::max(0.001, previousTime - previousPreviousTime);
        const double previousSlope =
            std::log2(previous.bpm / previousPrevious->bpm) / previousElapsed;
        const double currentSlope =
            std::log2(current.bpm / previous.bpm) / elapsed;
        const bool continuousTransition =
            std::abs(previousSlope) <= options.maximumContinuousTempoSlopeOctavesPerSecond &&
            std::abs(currentSlope) <= options.maximumContinuousTempoSlopeOctavesPerSecond;
        if (continuousTransition) {
            const double slopeChange = std::abs(currentSlope - previousSlope);
            cost += options.tempoSlopeChangePenalty * slopeChange * elapsed;
            if (previousSlope * currentSlope < 0.0 &&
                std::abs(previousSlope) > 0.01 && std::abs(currentSlope) > 0.01) {
                cost += 0.75;
            }
        }
    }

    if (previous.harmonicFamilyId != NoTempoFamily &&
        current.harmonicFamilyId != NoTempoFamily &&
        previous.harmonicFamilyId != current.harmonicFamilyId) {
        cost += 0.12;
    }

    double phaseWeight = std::sqrt(
        previous.phaseConfidence * current.phaseConfidence);
    if (previous.harmonicFamilyId == NoTempoFamily ||
        current.harmonicFamilyId == NoTempoFamily ||
        previous.harmonicFamilyId != current.harmonicFamilyId) {
        // A phase peak attached to a different periodicity family may denote a
        // subdivision rather than the same pulse. Keep it as weak evidence
        // until the rhythm-layer model can explain the ratio explicitly.
        phaseWeight *= 0.25;
    }
    const double residual = phaseResidual(
        previous.pulseTimeSeconds,
        previous.bpm,
        current.pulseTimeSeconds,
        current.bpm);
    cost += options.phaseTransitionPenalty * phaseWeight * residual * 2.0;
    return cost;
}

struct ObservationFrame {
    std::size_t localIndex = 0;
    std::vector<Observation> observations;
};

struct PathState {
    double score = -std::numeric_limits<double>::infinity();
    std::vector<std::size_t> choices;
};

struct PathSelection {
    std::vector<std::pair<std::size_t, Observation>> path;
    double objectiveScore = 0.0;
};

PathSelection selectObservationPath(
    const std::vector<LocalWindow>& localWindows,
    const std::vector<GlobalTempoFamily>& families,
    bool useGlobalRegularization,
    const AnalysisOptions& options)
{
    std::vector<ObservationFrame> frames;
    const double familyTolerance = std::max(0.025, options.tempoAgreementTolerance * 2.0);
    const DominantFamily dominant = useGlobalRegularization
        ? findDominantFamily(families, localWindows, familyTolerance)
        : DominantFamily{};
    for (std::size_t localIndex = 0; localIndex < localWindows.size(); ++localIndex) {
        std::vector<Observation> observations = collectObservations(
            localWindows[localIndex],
            families,
            dominant,
            options);
        if (observations.empty()) {
            continue;
        }

        frames.push_back({localIndex, std::move(observations)});
    }

    if (frames.empty()) {
        return {};
    }

    std::vector<PathState> states;
    for (std::size_t observationIndex = 0;
         observationIndex < frames.front().observations.size();
         ++observationIndex) {
        PathState state;
        state.score = std::log(
            0.10 + 0.90 * frames.front().observations[observationIndex].quality);
        state.choices.push_back(observationIndex);
        states.push_back(std::move(state));
    }

    for (std::size_t frameIndex = 1; frameIndex < frames.size(); ++frameIndex) {
        const ObservationFrame& currentFrame = frames[frameIndex];
        const ObservationFrame& previousFrame = frames[frameIndex - 1];
        const double currentTime = localWindows[currentFrame.localIndex].centerSeconds;
        const double previousTime = localWindows[previousFrame.localIndex].centerSeconds;
        std::vector<PathState> nextStates;
        nextStates.reserve(
            previousFrame.observations.size() * currentFrame.observations.size());
        for (std::size_t previousIndex = 0;
             previousIndex < previousFrame.observations.size();
             ++previousIndex) {
            for (std::size_t currentIndex = 0;
                 currentIndex < currentFrame.observations.size();
                 ++currentIndex) {
                PathState best;
                for (const PathState& state : states) {
                    if (state.choices.back() != previousIndex) {
                        continue;
                    }
                    const Observation& previous =
                        previousFrame.observations[previousIndex];
                    const Observation& current =
                        currentFrame.observations[currentIndex];
                    const Observation* previousPrevious = nullptr;
                    double previousPreviousTime = previousTime;
                    if (frameIndex >= 2) {
                        const ObservationFrame& previousPreviousFrame = frames[frameIndex - 2];
                        previousPrevious = &previousPreviousFrame.observations[
                            state.choices[state.choices.size() - 2]];
                        previousPreviousTime =
                            localWindows[previousPreviousFrame.localIndex].centerSeconds;
                    }
                    const double score = state.score +
                        std::log(0.10 + 0.90 * current.quality) -
                        transitionCost(
                            previousPrevious,
                            previousPreviousTime,
                            previous,
                            previousTime,
                            current,
                            currentTime,
                            options);
                    if (score > best.score) {
                        best = state;
                        best.score = score;
                    }
                }
                if (!best.choices.empty()) {
                    best.choices.push_back(currentIndex);
                    nextStates.push_back(std::move(best));
                }
            }
        }
        states = std::move(nextStates);
    }

    const PathState& selected = *std::max_element(
        states.begin(),
        states.end(),
        [](const PathState& left, const PathState& right) {
            return left.score < right.score;
        });
    PathSelection selection;
    selection.objectiveScore = selected.score;
    selection.path.reserve(frames.size());
    for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        const ObservationFrame& frame = frames[frameIndex];
        selection.path.push_back({
            frame.localIndex,
            frame.observations[selected.choices[frameIndex]],
        });
    }
    return selection;
}

TempoTrackPoint observedPoint(
    const LocalWindow& local,
    const Observation& observation)
{
    TempoTrackPoint point;
    point.timeSeconds = local.centerSeconds;
    point.bpm = observation.bpm;
    point.pulseTimeSeconds = observation.pulseTimeSeconds;
    point.confidence = observation.confidence;
    point.phaseConfidence = observation.phaseConfidence;
    point.harmonicFamilyId = observation.harmonicFamilyId;
    point.harmonicRatioToFamily = observation.harmonicRatioToFamily;
    point.sourceWindowId = local.window->id;
    point.state = observation.state;
    point.propagationReason = observation.propagationReason;
    return point;
}

TempoTrackPoint uncertainPoint(const LocalWindow& local)
{
    TempoTrackPoint point;
    point.timeSeconds = local.centerSeconds;
    point.sourceWindowId = local.window->id;
    point.confidence = local.window->reliability;
    point.state = TempoTrackState::Uncertain;
    return point;
}

void interpolateBoundedGaps(
    std::vector<TempoTrackPoint>& points,
    double maximumGapSeconds,
    double maximumSlope)
{
    std::size_t index = 0;
    while (index < points.size()) {
        if (points[index].state != TempoTrackState::Uncertain) {
            ++index;
            continue;
        }
        const std::size_t gapStart = index;
        while (index < points.size() && points[index].state == TempoTrackState::Uncertain) {
            ++index;
        }
        if (gapStart == 0 || index == points.size()) {
            continue;
        }

        const TempoTrackPoint previous = points[gapStart - 1];
        const TempoTrackPoint next = points[index];
        const double elapsed = next.timeSeconds - previous.timeSeconds;
        if (!(elapsed > 0.0) || elapsed > maximumGapSeconds) {
            continue;
        }
        const double slope = std::abs(std::log2(next.bpm / previous.bpm)) / elapsed;
        if (slope > maximumSlope * 2.0) {
            continue;
        }

        for (std::size_t fill = gapStart; fill < index; ++fill) {
            TempoTrackPoint& point = points[fill];
            const double mix = (point.timeSeconds - previous.timeSeconds) / elapsed;
            point.bpm = previous.bpm * std::exp2(
                std::log2(next.bpm / previous.bpm) * mix);
            const double averageBpm = (previous.bpm + point.bpm) * 0.5;
            point.pulseTimeSeconds = nearestPulse(
                previous.pulseTimeSeconds,
                point.timeSeconds,
                averageBpm);
            point.confidence = clamp01(
                std::min(previous.confidence, next.confidence) * 0.65);
            point.phaseConfidence = clamp01(
                std::min(previous.phaseConfidence, next.phaseConfidence) * 0.55);
            point.harmonicFamilyId = previous.harmonicFamilyId == next.harmonicFamilyId
                ? previous.harmonicFamilyId
                : NoTempoFamily;
            point.harmonicRatioToFamily = 1.0;
            point.state = TempoTrackState::Propagated;
            point.propagationReason = TempoPropagationReason::BoundedGapInterpolation;
        }
    }
}

void stabilizePropagatedPhases(std::vector<TempoTrackPoint>& points)
{
    for (std::size_t i = 1; i < points.size(); ++i) {
        TempoTrackPoint& point = points[i];
        const TempoTrackPoint& previous = points[i - 1];
        if (point.state != TempoTrackState::Propagated ||
            point.propagationReason == TempoPropagationReason::LocalPhaseAnchor ||
            !(previous.bpm > 0.0)) {
            continue;
        }
        const double averageBpm = (previous.bpm + point.bpm) * 0.5;
        point.pulseTimeSeconds = nearestPulse(
            previous.pulseTimeSeconds,
            point.timeSeconds,
            averageBpm);
    }
}

void backPropagateStablePhase(
    std::vector<TempoTrackPoint>& points,
    double maximumSeconds,
    double tempoTolerance)
{
    if (!(maximumSeconds > 0.0)) {
        return;
    }
    const auto firstObserved = std::find_if(
        points.begin(),
        points.end(),
        [](const TempoTrackPoint& point) {
            return point.state == TempoTrackState::Observed && point.bpm > 0.0;
        });
    if (firstObserved == points.end() || firstObserved == points.begin()) {
        return;
    }

    const std::size_t firstIndex = static_cast<std::size_t>(
        std::distance(points.begin(), firstObserved));
    std::vector<const TempoTrackPoint*> support;
    for (std::size_t i = firstIndex; i < points.size() && support.size() < 6; ++i) {
        const TempoTrackPoint& point = points[i];
        if (point.state != TempoTrackState::Observed || !(point.bpm > 0.0)) {
            continue;
        }
        if (std::abs(point.bpm / firstObserved->bpm - 1.0) > tempoTolerance) {
            break;
        }
        support.push_back(&point);
    }
    if (support.size() < 3) {
        return;
    }

    double bpmWeight = 0.0;
    double weightedBpm = 0.0;
    double meanPhaseConfidence = 0.0;
    for (const TempoTrackPoint* point : support) {
        const double weight = std::max(0.05, point->confidence);
        weightedBpm += point->bpm * weight;
        bpmWeight += weight;
        meanPhaseConfidence += point->phaseConfidence;
    }
    const double bpm = weightedBpm / bpmWeight;
    meanPhaseConfidence /= static_cast<double>(support.size());
    if (meanPhaseConfidence < 0.40) {
        return;
    }

    const double anchorPulse = nearestPulse(
        firstObserved->pulseTimeSeconds,
        firstObserved->timeSeconds,
        bpm);
    double meanResidual = 0.0;
    for (const TempoTrackPoint* point : support) {
        meanResidual += phaseResidual(
            anchorPulse,
            bpm,
            point->pulseTimeSeconds,
            point->bpm);
    }
    meanResidual /= static_cast<double>(support.size());
    if (meanResidual > 0.14) {
        return;
    }

    for (std::size_t i = firstIndex; i-- > 0;) {
        TempoTrackPoint& point = points[i];
        const double distance = firstObserved->timeSeconds - point.timeSeconds;
        if (distance > maximumSeconds) {
            break;
        }
        const double decay = std::exp(-distance / maximumSeconds);
        point.bpm = bpm;
        point.pulseTimeSeconds = nearestPulse(anchorPulse, point.timeSeconds, bpm);
        point.confidence = clamp01(
            std::min(0.45, firstObserved->confidence * 0.55 * decay));
        point.phaseConfidence = clamp01(meanPhaseConfidence * 0.55 * decay);
        point.harmonicFamilyId = firstObserved->harmonicFamilyId;
        point.harmonicRatioToFamily = firstObserved->harmonicRatioToFamily;
        point.state = TempoTrackState::Propagated;
        point.propagationReason = TempoPropagationReason::StablePhaseBackPropagation;
    }
}

struct PhaseSupport {
    double pulseTimeSeconds = 0.0;
    double weight = 0.0;
    WindowScale scale = WindowScale::Short;
};

unsigned scaleBit(WindowScale scale)
{
    switch (scale) {
    case WindowScale::Short:
        return 1U;
    case WindowScale::Medium:
        return 2U;
    case WindowScale::Long:
        return 4U;
    }
    return 0U;
}

std::size_t bitCount(unsigned value)
{
    std::size_t count = 0;
    while (value != 0U) {
        count += value & 1U;
        value >>= 1U;
    }
    return count;
}

void refineTrackPhaseFromMultipleScales(
    std::vector<TempoTrackPoint>& points,
    const std::vector<AnalysisWindow>& windows,
    double tempoTolerance)
{
    constexpr double ClusterSeconds = 0.006;
    for (TempoTrackPoint& point : points) {
        if (point.state == TempoTrackState::Uncertain || !(point.bpm > 0.0) ||
            point.phaseConfidence >= 0.70) {
            continue;
        }

        std::vector<PhaseSupport> support;
        double totalWeight = 0.0;
        for (const AnalysisWindow& window : windows) {
            if (point.timeSeconds < window.startSeconds - 1.0e-9 ||
                point.timeSeconds > window.endSeconds + 1.0e-9) {
                continue;
            }

            const TempoCandidate* best = nullptr;
            double bestWeight = 0.0;
            double normalizedBpm = 0.0;
            for (const TempoCandidate& candidate : window.tempoCandidates) {
                if (candidate.origin == CandidateOrigin::HarmonicAlias ||
                    !(candidate.bpm > 0.0) || candidate.phaseConfidence <= 0.0) {
                    continue;
                }
                const double candidateBpm = candidate.bpm;
                if (std::abs(candidateBpm / point.bpm - 1.0) > tempoTolerance) {
                    continue;
                }
                const double weight = candidate.score *
                    (0.20 + 0.80 * window.reliability) *
                    (0.15 + 0.85 * candidate.phaseConfidence);
                if (best == nullptr || weight > bestWeight) {
                    best = &candidate;
                    bestWeight = weight;
                    normalizedBpm = candidateBpm;
                }
            }
            if (best == nullptr || !(bestWeight > 0.0)) {
                continue;
            }
            support.push_back({
                nearestPulse(
                    best->pulseTimeSeconds,
                    point.timeSeconds,
                    normalizedBpm),
                bestWeight,
                window.scale,
            });
            totalWeight += bestWeight;
        }
        if (support.size() < 3 || !(totalWeight > 0.0)) {
            continue;
        }

        const double periodSeconds = 60.0 / point.bpm;
        std::size_t medoid = 0;
        double bestCost = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < support.size(); ++i) {
            double cost = 0.0;
            for (const PhaseSupport& candidate : support) {
                cost += candidate.weight * std::abs(std::remainder(
                    candidate.pulseTimeSeconds - support[i].pulseTimeSeconds,
                    periodSeconds));
            }
            if (cost < bestCost) {
                bestCost = cost;
                medoid = i;
            }
        }

        double inlierWeight = 0.0;
        double weightedDelta = 0.0;
        double weightedAbsoluteDelta = 0.0;
        std::size_t inlierCount = 0;
        unsigned scaleMask = 0U;
        for (const PhaseSupport& candidate : support) {
            const double delta = std::remainder(
                candidate.pulseTimeSeconds - support[medoid].pulseTimeSeconds,
                periodSeconds);
            if (std::abs(delta) > ClusterSeconds) {
                continue;
            }
            inlierWeight += candidate.weight;
            weightedDelta += candidate.weight * delta;
            weightedAbsoluteDelta += candidate.weight * std::abs(delta);
            ++inlierCount;
            scaleMask |= scaleBit(candidate.scale);
        }
        if (inlierCount < 3 || bitCount(scaleMask) < 2 ||
            inlierWeight < totalWeight * 0.35) {
            continue;
        }

        const double consensusPulse = support[medoid].pulseTimeSeconds +
            weightedDelta / inlierWeight;
        const double spread = weightedAbsoluteDelta / inlierWeight;
        if (std::abs(std::remainder(
                consensusPulse - point.pulseTimeSeconds,
                periodSeconds)) < 0.001) {
            continue;
        }
        point.pulseTimeSeconds = nearestPulse(
            consensusPulse,
            point.timeSeconds,
            point.bpm);
        point.phaseConfidence = std::max(
            point.phaseConfidence,
            clamp01((inlierWeight / totalWeight) *
                (1.0 - spread / ClusterSeconds)));
        point.multiScalePhaseRefined = true;
    }
}

double pointBoundary(
    const std::vector<TempoTrackPoint>& points,
    std::size_t index,
    double durationSeconds)
{
    if (index == 0) {
        return 0.0;
    }
    if (index >= points.size()) {
        return durationSeconds;
    }
    return (points[index - 1].timeSeconds + points[index].timeSeconds) * 0.5;
}

TempoSegment makeSegment(
    const std::vector<TempoTrackPoint>& points,
    std::size_t first,
    std::size_t end,
    double durationSeconds)
{
    TempoSegment segment;
    segment.startSeconds = pointBoundary(points, first, durationSeconds);
    segment.endSeconds = pointBoundary(points, end, durationSeconds);
    segment.state = points[first].state;
    segment.harmonicFamilyId = points[first].harmonicFamilyId;

    double confidenceSum = 0.0;
    bool sawPropagated = false;
    for (std::size_t i = first; i < end; ++i) {
        const TempoTrackPoint& point = points[i];
        confidenceSum += point.confidence;
        sawPropagated = sawPropagated || point.state == TempoTrackState::Propagated;
        if (point.harmonicFamilyId != segment.harmonicFamilyId) {
            segment.harmonicFamilyId = NoTempoFamily;
        }
        segment.supportingWindowIds.push_back(point.sourceWindowId);
    }
    segment.confidence = confidenceSum / static_cast<double>(end - first);
    if (segment.state != TempoTrackState::Uncertain && sawPropagated) {
        segment.state = TempoTrackState::Propagated;
    }

    if (points[first].state != TempoTrackState::Uncertain) {
        segment.startBpm = points[first].bpm;
        segment.endBpm = points[end - 1].bpm;
        segment.isContinuousChange =
            std::abs(segment.endBpm / segment.startBpm - 1.0) > 0.015;
        segment.pulseTimeAtStartSeconds = nearestPulse(
            points[first].pulseTimeSeconds,
            segment.startSeconds,
            segment.startBpm);
    }
    return segment;
}

std::vector<TempoSegment> buildSegments(
    const std::vector<TempoTrackPoint>& points,
    double durationSeconds,
    double maximumContinuousSlope)
{
    std::vector<TempoSegment> segments;
    if (points.empty()) {
        return segments;
    }

    std::size_t first = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const TempoTrackPoint& previous = points[i - 1];
        const TempoTrackPoint& current = points[i];
        bool split = previous.state == TempoTrackState::Uncertain ||
            current.state == TempoTrackState::Uncertain;
        if (previous.state == TempoTrackState::Uncertain &&
            current.state == TempoTrackState::Uncertain) {
            split = false;
        }
        if (!split) {
            const double elapsed = std::max(0.001, current.timeSeconds - previous.timeSeconds);
            const double slope = std::abs(std::log2(current.bpm / previous.bpm)) / elapsed;
            split = slope > maximumContinuousSlope;
        }
        if (split) {
            segments.push_back(makeSegment(points, first, i, durationSeconds));
            first = i;
        }
    }
    segments.push_back(makeSegment(points, first, points.size(), durationSeconds));
    return segments;
}

TempoTrack makeTrack(
    const std::vector<LocalWindow>& localWindows,
    const std::vector<AnalysisWindow>& allWindows,
    const PathSelection& selection,
    double audioDurationSeconds,
    bool allowPhaseBackPropagation,
    const AnalysisOptions& options)
{
    TempoTrack track;
    std::vector<const Observation*> selected(localWindows.size(), nullptr);
    for (const auto& item : selection.path) {
        selected[item.first] = &item.second;
    }

    // The pointers above refer to selection storage, which remains alive and
    // is not modified while this track is assembled.
    track.points.reserve(localWindows.size());
    for (std::size_t i = 0; i < localWindows.size(); ++i) {
        if (selected[i] != nullptr) {
            track.points.push_back(observedPoint(localWindows[i], *selected[i]));
        } else {
            track.points.push_back(uncertainPoint(localWindows[i]));
        }
    }
    interpolateBoundedGaps(
        track.points,
        options.maximumTrackedGapSeconds,
        options.maximumContinuousTempoSlopeOctavesPerSecond);
    stabilizePropagatedPhases(track.points);
    if (allowPhaseBackPropagation) {
        backPropagateStablePhase(
            track.points,
            options.maximumPhaseBackPropagationSeconds,
            std::max(0.02, options.tempoAgreementTolerance * 1.5));
    }
    refineTrackPhaseFromMultipleScales(
        track.points,
        allWindows,
        std::max(0.012, options.tempoAgreementTolerance));
    track.segments = buildSegments(
        track.points,
        audioDurationSeconds,
        options.maximumContinuousTempoSlopeOctavesPerSecond);
    return track;
}

bool materiallyDifferent(const TempoTrack& left, const TempoTrack& right)
{
    if (left.points.size() != right.points.size()) {
        return true;
    }
    for (std::size_t i = 0; i < left.points.size(); ++i) {
        const TempoTrackPoint& leftPoint = left.points[i];
        const TempoTrackPoint& rightPoint = right.points[i];
        if ((leftPoint.bpm > 0.0) != (rightPoint.bpm > 0.0)) {
            return true;
        }
        if (leftPoint.bpm > 0.0 &&
            std::abs(leftPoint.bpm / rightPoint.bpm - 1.0) > 0.02) {
            return true;
        }
    }
    return false;
}

} // namespace

TempoTrackingOutput buildTempoTracks(
    const std::vector<AnalysisWindow>& windows,
    const std::vector<GlobalTempoFamily>& families,
    double audioDurationSeconds,
    const AnalysisOptions& options)
{
    TempoTrackingOutput output;
    const std::vector<LocalWindow> localWindows = collectLocalWindows(windows, options);
    if (localWindows.empty()) {
        return output;
    }

    const PathSelection regularized = selectObservationPath(
        localWindows,
        families,
        true,
        options);
    output.selectedTrack = makeTrack(
        localWindows,
        windows,
        regularized,
        audioDurationSeconds,
        true,
        options);
    if (!regularized.path.empty()) {
        TempoTrackHypothesis hypothesis;
        hypothesis.kind = TempoHypothesisKind::GloballyRegularized;
        hypothesis.averageObjectiveCost =
            -regularized.objectiveScore / static_cast<double>(regularized.path.size());
        hypothesis.selected = true;
        hypothesis.track = output.selectedTrack;
        output.hypotheses.push_back(std::move(hypothesis));
    }

    const PathSelection localEvidence = selectObservationPath(
        localWindows,
        families,
        false,
        options);
    const TempoTrack localEvidenceTrack = makeTrack(
        localWindows,
        windows,
        localEvidence,
        audioDurationSeconds,
        false,
        options);
    if (!localEvidence.path.empty() &&
        materiallyDifferent(output.selectedTrack, localEvidenceTrack)) {
        TempoTrackHypothesis hypothesis;
        hypothesis.kind = TempoHypothesisKind::LocalEvidenceCurve;
        hypothesis.averageObjectiveCost =
            -localEvidence.objectiveScore / static_cast<double>(localEvidence.path.size());
        hypothesis.track = localEvidenceTrack;
        output.hypotheses.push_back(std::move(hypothesis));
    }
    return output;
}

} // namespace autotiming::detail

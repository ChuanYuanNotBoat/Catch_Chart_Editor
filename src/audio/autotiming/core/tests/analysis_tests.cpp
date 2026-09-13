#include "autotiming/Analysis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<float> makePulseTrain(
    std::uint32_t sampleRate,
    double durationSeconds,
    double bpm,
    double firstPulseSeconds,
    double lastPulseSeconds = std::numeric_limits<double>::infinity())
{
    const std::size_t sampleCount = static_cast<std::size_t>(
        std::ceil(durationSeconds * sampleRate));
    std::vector<float> signal(sampleCount, 0.0f);

    std::uint32_t noiseState = 0x8f7011eeU;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        noiseState = noiseState * 1664525U + 1013904223U;
        const double noise = static_cast<double>((noiseState >> 8) & 0xffffU) / 32767.5 - 1.0;
        signal[i] = static_cast<float>(noise * 0.0005);
    }

    const auto addPulse = [&](double beat) {
        const std::size_t onset = static_cast<std::size_t>(
            std::llround(beat * sampleRate));
        const std::size_t burstLength = static_cast<std::size_t>(0.08 * sampleRate);
        for (std::size_t j = 0; j < burstLength && onset + j < sampleCount; ++j) {
            const double t = static_cast<double>(j) / sampleRate;
            const double envelope = std::exp(-t * 55.0);
            const double low = std::sin(2.0 * 3.14159265358979323846 * 90.0 * t);
            const double high = std::sin(2.0 * 3.14159265358979323846 * 1800.0 * t);
            signal[onset + j] += static_cast<float>(envelope * (0.72 * low + 0.28 * high));
        }
    };

    const double secondsPerBeat = 60.0 / bpm;
    const double pulseEnd = std::min(durationSeconds, lastPulseSeconds);
    for (double beat = firstPulseSeconds; beat < pulseEnd; beat += secondsPerBeat) {
        addPulse(beat);
    }
    return signal;
}

std::vector<float> makeTempoStep(
    std::uint32_t sampleRate,
    double durationSeconds,
    double changeSeconds,
    double firstBpm,
    double secondBpm)
{
    std::vector<float> signal = makePulseTrain(
        sampleRate,
        durationSeconds,
        firstBpm,
        0.125,
        changeSeconds);

    const std::size_t burstLength = static_cast<std::size_t>(0.08 * sampleRate);
    for (double beat = changeSeconds + 0.125;
         beat < durationSeconds;
         beat += 60.0 / secondBpm) {
        const std::size_t onset = static_cast<std::size_t>(
            std::llround(beat * sampleRate));
        for (std::size_t j = 0; j < burstLength && onset + j < signal.size(); ++j) {
            const double t = static_cast<double>(j) / sampleRate;
            const double envelope = std::exp(-t * 55.0);
            const double low = std::sin(2.0 * 3.14159265358979323846 * 90.0 * t);
            const double high = std::sin(2.0 * 3.14159265358979323846 * 1800.0 * t);
            signal[onset + j] += static_cast<float>(envelope * (0.72 * low + 0.28 * high));
        }
    }
    return signal;
}

std::vector<float> makeTempoRamp(
    std::uint32_t sampleRate,
    double durationSeconds,
    double firstBpm,
    double lastBpm)
{
    const std::size_t sampleCount = static_cast<std::size_t>(
        std::ceil(durationSeconds * sampleRate));
    std::vector<float> signal(sampleCount, 0.0f);

    std::uint32_t noiseState = 0x4b1d3a27U;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        noiseState = noiseState * 1664525U + 1013904223U;
        const double noise = static_cast<double>((noiseState >> 8) & 0xffffU) / 32767.5 - 1.0;
        signal[i] = static_cast<float>(noise * 0.0005);
    }

    const std::size_t burstLength = static_cast<std::size_t>(0.08 * sampleRate);
    for (double beat = 0.125; beat < durationSeconds;) {
        const std::size_t onset = static_cast<std::size_t>(
            std::llround(beat * sampleRate));
        for (std::size_t j = 0; j < burstLength && onset + j < signal.size(); ++j) {
            const double t = static_cast<double>(j) / sampleRate;
            const double envelope = std::exp(-t * 55.0);
            const double low = std::sin(2.0 * 3.14159265358979323846 * 90.0 * t);
            const double high = std::sin(2.0 * 3.14159265358979323846 * 1800.0 * t);
            signal[onset + j] += static_cast<float>(
                envelope * (0.72 * low + 0.28 * high));
        }
        const double mix = std::min(1.0, beat / durationSeconds);
        const double bpm = firstBpm + (lastBpm - firstBpm) * mix;
        beat += 60.0 / bpm;
    }
    return signal;
}

std::vector<float> makeLayeredTempoRamp(
    std::uint32_t sampleRate,
    double durationSeconds,
    double baseBpm,
    double firstLayerBpm,
    double lastLayerBpm,
    double layerStartSeconds,
    double layerEndSeconds)
{
    std::vector<float> base = makePulseTrain(
        sampleRate,
        durationSeconds,
        baseBpm,
        0.125);
    const double layerDuration = layerEndSeconds - layerStartSeconds;
    const std::vector<float> layer = makeTempoRamp(
        sampleRate,
        layerDuration,
        firstLayerBpm,
        lastLayerBpm);
    for (float& sample : base) {
        sample *= 0.45f;
    }
    const std::size_t layerStartFrame = static_cast<std::size_t>(
        std::llround(layerStartSeconds * sampleRate));
    for (std::size_t i = 0; i < layer.size() && layerStartFrame + i < base.size(); ++i) {
        base[layerStartFrame + i] += layer[i] * 1.5f;
    }
    return base;
}

std::vector<float> makeConcurrentPulseLayers(
    std::uint32_t sampleRate,
    double durationSeconds,
    double baseBpm,
    double layerBpm,
    double layerStartSeconds,
    double layerEndSeconds)
{
    std::vector<float> base = makePulseTrain(
        sampleRate,
        durationSeconds,
        baseBpm,
        0.125);
    const std::vector<float> layer = makePulseTrain(
        sampleRate,
        durationSeconds,
        layerBpm,
        layerStartSeconds + 0.125,
        layerEndSeconds);
    for (std::size_t i = 0; i < base.size(); ++i) {
        base[i] = base[i] * 0.60f + layer[i] * 1.40f;
    }
    return base;
}

double distanceToInteger(double value)
{
    return std::abs(value - std::round(value));
}

const autotiming::TempoCandidate* bestOnsetCandidate(
    const autotiming::AnalysisWindow& window)
{
    const autotiming::TempoCandidate* best = nullptr;
    for (const autotiming::TempoCandidate& candidate : window.tempoCandidates) {
        if (candidate.origin != autotiming::CandidateOrigin::OnsetEnvelopePeriodicity) {
            continue;
        }
        if (best == nullptr || candidate.score > best->score) {
            best = &candidate;
        }
    }
    return best;
}

void testInputValidation()
{
    autotiming::AudioView invalid;
    invalid.frameCount = 10;
    invalid.sampleRate = 44100;
    invalid.channels = 1;

    bool threw = false;
    try {
        (void)autotiming::analyze(invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "analysis accepted a null non-empty AudioView");
}

void testMultiScaleEvidence()
{
    constexpr std::uint32_t sampleRate = 44100;
    const std::vector<float> audio = makePulseTrain(sampleRate, 56.0, 120.0, 0.125);
    const autotiming::AudioView view{
        audio.data(),
        audio.size(),
        sampleRate,
        1,
    };

    const autotiming::AnalysisResult result = autotiming::analyze(view);
    require(result.metadata.frameCount == audio.size(), "analysis metadata frame count changed");
    require(result.diagnostics.windows.size() >= 3, "analysis did not emit multiple windows");

    bool sawShort = false;
    bool sawMedium = false;
    bool sawLong = false;
    bool sawPrimary120 = false;
    bool sawOnsetPeriodicity120 = false;
    bool sawHalfAlias = false;
    bool sawDoubleAlias = false;
    for (const autotiming::AnalysisWindow& window : result.diagnostics.windows) {
        sawShort = sawShort || window.scale == autotiming::WindowScale::Short;
        sawMedium = sawMedium || window.scale == autotiming::WindowScale::Medium;
        sawLong = sawLong || window.scale == autotiming::WindowScale::Long;
        require(window.endSeconds > window.startSeconds, "analysis emitted an empty window");
        require(window.reliability >= 0.0 && window.reliability <= 1.0,
            "window reliability is outside [0, 1]");

        for (const autotiming::TempoCandidate& candidate : window.tempoCandidates) {
            if (candidate.origin == autotiming::CandidateOrigin::LegacyWindowEstimate &&
                std::abs(candidate.bpm - 120.0) <= 0.5) {
                sawPrimary120 = true;
            }
            if (candidate.origin == autotiming::CandidateOrigin::OnsetEnvelopePeriodicity &&
                std::abs(candidate.bpm - 120.0) <= 1.0 &&
                candidate.phaseConfidence > 0.0) {
                sawOnsetPeriodicity120 = true;
            }
            if (candidate.origin == autotiming::CandidateOrigin::HarmonicAlias &&
                std::abs(candidate.harmonicRatio - 0.5) < 1.0e-9) {
                sawHalfAlias = true;
            }
            if (candidate.origin == autotiming::CandidateOrigin::HarmonicAlias &&
                std::abs(candidate.harmonicRatio - 2.0) < 1.0e-9) {
                sawDoubleAlias = true;
            }
        }
    }

    require(sawShort && sawMedium && sawLong, "analysis omitted a configured time scale");
    require(sawPrimary120, "analysis did not preserve the legacy 120 BPM window estimate");
    require(sawOnsetPeriodicity120,
        "onset periodicity estimator did not recover the 120 BPM fixture");
    require(sawHalfAlias, "analysis did not expose the half-tempo family alternative");
    require(sawDoubleAlias, "analysis did not expose the double-tempo family alternative");
    require(!result.diagnostics.anchorRegions.empty(),
        "fixed-tempo analysis did not select an anchor region");
    require(!result.tempoCandidates.empty(),
        "fixed-tempo analysis did not aggregate a global tempo candidate");
    require(std::abs(result.tempoCandidates.front().bpm - 120.0) <= 0.5,
        "fixed-tempo global candidate changed");
    require(!result.tempoFamilies.empty(),
        "fixed-tempo alternatives were not grouped into harmonic families");

    const autotiming::GlobalTempoCandidate* familyBase = nullptr;
    const autotiming::GlobalTempoCandidate* familyAlias = nullptr;
    for (const autotiming::GlobalTempoCandidate& candidate : result.tempoCandidates) {
        if (std::abs(candidate.bpm - 120.0) <= 0.5) {
            familyBase = &candidate;
        }
        if (std::abs(candidate.bpm - 60.0) <= 0.5 ||
            std::abs(candidate.bpm - 240.0) <= 1.0) {
            familyAlias = &candidate;
        }
    }
    require(familyBase != nullptr && familyAlias != nullptr,
        "fixed-tempo global harmonic alternatives are incomplete");
    require(familyBase->harmonicFamilyId == familyAlias->harmonicFamilyId,
        "half/double alternatives were split into unrelated tempo families");
    require(std::all_of(
        result.diagnostics.anchorRegions.begin(),
        result.diagnostics.anchorRegions.end(),
        [](const autotiming::AnchorRegion& region) {
            return region.harmonicFamilyId != autotiming::NoTempoFamily;
        }),
        "an anchor region was not linked to a global tempo family");
    require(result.confidence.tempoFamily + 1.0e-12 >= result.confidence.tempo,
        "tempo-family confidence is unexpectedly lower than exact-rate confidence");
    require(result.confidence.tempo > 0.0,
        "fixed-tempo analysis did not report tempo confidence");

    std::size_t observedTrackPoints = 0;
    for (const autotiming::TempoTrackPoint& point : result.tempoTrack.points) {
        if (point.state != autotiming::TempoTrackState::Observed) {
            continue;
        }
        ++observedTrackPoints;
        require(std::abs(point.bpm - 120.0) <= 1.5,
            "fixed-tempo tracker jumped to a half/double-rate alternative");
        require(distanceToInteger((point.pulseTimeSeconds - 0.125) / 0.5) <= 0.12,
            "fixed-tempo tracker lost pulse phase continuity");
    }
    require(observedTrackPoints >= 6,
        "fixed-tempo tracker emitted too few direct observations");
    require(!result.tempoTrack.segments.empty(),
        "fixed-tempo tracker emitted no tempo segment");
}

void testReliableRegionSelection()
{
    constexpr std::uint32_t sampleRate = 44100;
    const std::vector<float> audio = makePulseTrain(
        sampleRate,
        56.0,
        120.0,
        16.125,
        40.0);
    const autotiming::AudioView view{
        audio.data(),
        audio.size(),
        sampleRate,
        1,
    };

    const autotiming::AnalysisResult result = autotiming::analyze(view);
    require(!result.diagnostics.anchorRegions.empty(),
        "rhythmic middle section did not produce an anchor island");
    require(!result.tempoCandidates.empty(),
        "anchor island did not produce a global tempo candidate");
    require(std::abs(result.tempoCandidates.front().bpm - 120.0) <= 0.5,
        "anchor island selected the wrong global tempo");

    double introReliability = 0.0;
    double rhythmicReliability = 0.0;
    double outroReliability = 0.0;
    bool sawSelectedLocalWindow = false;
    for (const autotiming::AnalysisWindow& window : result.diagnostics.windows) {
        if (window.scale != autotiming::WindowScale::Short) {
            continue;
        }
        if (window.endSeconds <= 12.0) {
            introReliability = std::max(introReliability, window.reliability);
        }
        if (window.startSeconds >= 16.0 && window.endSeconds <= 40.0) {
            rhythmicReliability = std::max(rhythmicReliability, window.reliability);
        }
        if (window.startSeconds >= 44.0) {
            outroReliability = std::max(outroReliability, window.reliability);
        }
        sawSelectedLocalWindow = sawSelectedLocalWindow || window.selectedAsAnchor;
    }

    require(sawSelectedLocalWindow, "anchor selection did not mark supporting diagnostics");
    require(rhythmicReliability > introReliability + 0.20,
        "reliable rhythm did not outrank the quiet intro");
    require(rhythmicReliability > outroReliability + 0.20,
        "reliable rhythm did not outrank the quiet outro");

    const bool hasIntroUncertainty = std::any_of(
        result.uncertainRegions.begin(),
        result.uncertainRegions.end(),
        [](const autotiming::UncertainRegion& region) {
            return region.startSeconds <= 1.0e-9 && region.endSeconds >= 8.0;
        });
    const bool hasOutroUncertainty = std::any_of(
        result.uncertainRegions.begin(),
        result.uncertainRegions.end(),
        [](const autotiming::UncertainRegion& region) {
            return region.startSeconds <= 48.0 && region.endSeconds >= 55.9;
        });
    require(hasIntroUncertainty, "quiet intro was not surfaced as uncertain");
    require(hasOutroUncertainty, "quiet outro was not surfaced as uncertain");
    require(result.confidence.reliableCoverage > 0.0 &&
        result.confidence.reliableCoverage < 0.8,
        "anchor coverage did not stay local to the rhythmic section");
    require(!result.tempoTrack.points.empty(),
        "reliable-region fixture emitted no tracker diagnostics");
    require(result.tempoTrack.points.front().state == autotiming::TempoTrackState::Propagated,
        "tracker did not back-propagate the later stable phase through the quiet intro");
    require(result.tempoTrack.points.front().propagationReason ==
            autotiming::TempoPropagationReason::StablePhaseBackPropagation,
        "tracker did not label the intro phase propagation reason");
    require(distanceToInteger(
            (result.tempoTrack.points.front().pulseTimeSeconds - 0.125) / 0.5) <= 0.15,
        "back-propagated intro phase left the later stable pulse grid");
    require(result.tempoTrack.points.front().confidence < 0.5,
        "back-propagated intro was reported with direct-observation confidence");
    require(result.tempoTrack.points.back().state == autotiming::TempoTrackState::Uncertain,
        "tracker filled a quiet outro without a following phase constraint");
}

void testOnsetPeriodicityFollowsTempoStep()
{
    constexpr std::uint32_t sampleRate = 44100;
    const std::vector<float> audio = makeTempoStep(
        sampleRate,
        48.0,
        24.0,
        100.0,
        150.0);
    const autotiming::AudioView view{
        audio.data(),
        audio.size(),
        sampleRate,
        1,
    };
    autotiming::AnalysisOptions options;
    options.minimumTempoBpm = 70.0;
    options.maximumTempoBpm = 180.0;

    const autotiming::AnalysisResult result = autotiming::analyze(view, options);
    double earlyBpm = 0.0;
    double earlyScore = 0.0;
    double lateBpm = 0.0;
    double lateScore = 0.0;
    for (const autotiming::AnalysisWindow& window : result.diagnostics.windows) {
        if (window.scale != autotiming::WindowScale::Short) {
            continue;
        }
        const autotiming::TempoCandidate* candidate = bestOnsetCandidate(window);
        if (candidate == nullptr) {
            continue;
        }
        const double center = (window.startSeconds + window.endSeconds) * 0.5;
        if (center <= 16.0 && candidate->score > earlyScore) {
            earlyBpm = candidate->bpm;
            earlyScore = candidate->score;
        }
        if (center >= 32.0 && candidate->score > lateScore) {
            lateBpm = candidate->bpm;
            lateScore = candidate->score;
        }
    }

    require(earlyScore > 0.0, "onset estimator emitted no pre-change candidate");
    require(lateScore > 0.0, "onset estimator emitted no post-change candidate");
    require(std::abs(earlyBpm - 100.0) <= 3.0,
        "onset estimator missed the pre-change 100 BPM section");
    require(std::abs(lateBpm - 150.0) <= 3.0,
        "onset estimator missed the post-change 150 BPM section");

    double earlyTrackSum = 0.0;
    std::size_t earlyTrackCount = 0;
    double lateTrackSum = 0.0;
    std::size_t lateTrackCount = 0;
    for (const autotiming::TempoTrackPoint& point : result.tempoTrack.points) {
        if (point.state != autotiming::TempoTrackState::Observed) {
            continue;
        }
        if (point.timeSeconds <= 16.0) {
            earlyTrackSum += point.bpm;
            ++earlyTrackCount;
        }
        if (point.timeSeconds >= 32.0) {
            lateTrackSum += point.bpm;
            ++lateTrackCount;
        }
    }
    require(earlyTrackCount >= 2 && lateTrackCount >= 2,
        "tempo-step tracker did not retain observations on both sides");
    require(std::abs(earlyTrackSum / earlyTrackCount - 100.0) <= 4.0,
        "tempo-step tracker chose the wrong pre-change path");
    require(std::abs(lateTrackSum / lateTrackCount - 150.0) <= 4.0,
        "tempo-step tracker chose the wrong post-change path");

    std::size_t definiteSegments = 0;
    for (const autotiming::TempoSegment& segment : result.tempoTrack.segments) {
        definiteSegments += segment.state != autotiming::TempoTrackState::Uncertain ? 1U : 0U;
    }
    require(definiteSegments >= 2,
        "abrupt tempo step was incorrectly merged into one continuous segment");
}

void testTempoTrackerFollowsGradualRamp()
{
    constexpr std::uint32_t sampleRate = 44100;
    const std::vector<float> audio = makeTempoRamp(
        sampleRate,
        56.0,
        90.0,
        150.0);
    const autotiming::AudioView view{
        audio.data(),
        audio.size(),
        sampleRate,
        1,
    };
    autotiming::AnalysisOptions options;
    options.minimumTempoBpm = 70.0;
    options.maximumTempoBpm = 180.0;

    const autotiming::AnalysisResult result = autotiming::analyze(view, options);
    double earlySum = 0.0;
    std::size_t earlyCount = 0;
    double lateSum = 0.0;
    std::size_t lateCount = 0;
    for (const autotiming::TempoTrackPoint& point : result.tempoTrack.points) {
        if (point.state != autotiming::TempoTrackState::Observed) {
            continue;
        }
        if (point.timeSeconds <= 18.0) {
            earlySum += point.bpm;
            ++earlyCount;
        }
        if (point.timeSeconds >= 38.0) {
            lateSum += point.bpm;
            ++lateCount;
        }
    }
    require(earlyCount >= 2 && lateCount >= 2,
        "gradual-ramp tracker emitted too few endpoint observations");
    const double earlyMean = earlySum / earlyCount;
    const double lateMean = lateSum / lateCount;
    require(earlyMean >= 85.0 && earlyMean <= 115.0,
        "gradual-ramp tracker missed the early tempo range");
    require(lateMean >= 130.0 && lateMean <= 155.0,
        "gradual-ramp tracker missed the late tempo range");
    require(lateMean >= earlyMean + 25.0,
        "gradual-ramp tracker did not preserve the tempo direction");

    const bool hasContinuousSegment = std::any_of(
        result.tempoTrack.segments.begin(),
        result.tempoTrack.segments.end(),
        [](const autotiming::TempoSegment& segment) {
            return segment.isContinuousChange &&
                segment.state != autotiming::TempoTrackState::Uncertain;
        });
    require(hasContinuousSegment,
        "gradual tempo change was not represented as a continuous segment");
}

void testTempoTrackerKeepsOctaveBranchAcrossWideRamp()
{
    constexpr std::uint32_t sampleRate = 32000;
    const std::vector<float> audio = makeTempoRamp(
        sampleRate,
        64.0,
        100.0,
        300.0);
    const autotiming::AudioView view{
        audio.data(),
        audio.size(),
        sampleRate,
        1,
    };
    autotiming::AnalysisOptions options;
    options.minimumTempoBpm = 70.0;
    options.maximumTempoBpm = 480.0;

    const autotiming::AnalysisResult result = autotiming::analyze(view, options);
    std::vector<double> bpms;
    for (const autotiming::TempoTrackPoint& point : result.tempoTrack.points) {
        if (point.state == autotiming::TempoTrackState::Observed) {
            bpms.push_back(point.bpm);
        }
    }
    require(bpms.size() >= 8,
        "wide-ramp tracker emitted too few observations");
    require(bpms.front() >= 90.0 && bpms.front() <= 125.0,
        "wide-ramp tracker started on the wrong octave branch");
    require(bpms.back() >= 260.0 && bpms.back() <= 320.0,
        "wide-ramp tracker ended on the wrong octave branch");

    std::size_t strongReversals = 0;
    for (std::size_t i = 1; i < bpms.size(); ++i) {
        strongReversals += bpms[i] + 12.0 < bpms[i - 1] ? 1U : 0U;
    }
    require(strongReversals == 0,
        "wide-ramp tracker changed octave through a false downward reversal");
}

void testTempoTrackerPreservesCompetingLayerHypotheses()
{
    constexpr std::uint32_t sampleRate = 32000;
    const std::vector<float> audio = makeLayeredTempoRamp(
        sampleRate,
        56.0,
        120.0,
        240.0,
        360.0,
        16.0,
        40.0);
    const autotiming::AudioView view{
        audio.data(),
        audio.size(),
        sampleRate,
        1,
    };
    autotiming::AnalysisOptions options;
    options.minimumTempoBpm = 80.0;
    options.maximumTempoBpm = 200.0;

    const autotiming::AnalysisResult result = autotiming::analyze(view, options);
    const autotiming::TempoTrackHypothesis* regularized = nullptr;
    const autotiming::TempoTrackHypothesis* localEvidence = nullptr;
    for (const autotiming::TempoTrackHypothesis& hypothesis : result.tempoHypotheses) {
        if (hypothesis.kind == autotiming::TempoHypothesisKind::GloballyRegularized) {
            regularized = &hypothesis;
        }
        if (hypothesis.kind == autotiming::TempoHypothesisKind::LocalEvidenceCurve) {
            localEvidence = &hypothesis;
        }
    }
    require(regularized != nullptr && regularized->selected,
        "layered fixture has no selected globally regularized hypothesis");
    require(localEvidence != nullptr && !localEvidence->selected,
        "layered fixture discarded the competing local-evidence hypothesis");

    const auto middleMean = [](const autotiming::TempoTrack& track) {
        double sum = 0.0;
        std::size_t count = 0;
        for (const autotiming::TempoTrackPoint& point : track.points) {
            if (point.timeSeconds >= 20.0 && point.timeSeconds <= 36.0 && point.bpm > 0.0) {
                sum += point.bpm;
                ++count;
            }
        }
        return count == 0 ? 0.0 : sum / static_cast<double>(count);
    };
    const double regularizedMiddle = middleMean(regularized->track);
    const double localMiddle = middleMean(localEvidence->track);
    require(std::abs(regularizedMiddle - 120.0) <= 4.0,
        "global regularization did not preserve the fixture's stable pulse layer");
    require(localMiddle >= regularizedMiddle + 15.0 && localMiddle <= 190.0,
        "local-evidence hypothesis did not preserve the stronger changing layer");
}

void testPeriodicityLayerKeepsRationalEvidence()
{
    constexpr std::uint32_t sampleRate = 32000;
    const std::vector<float> audio = makeConcurrentPulseLayers(
        sampleRate,
        56.0,
        120.0,
        160.0,
        12.0,
        44.0);
    const autotiming::AudioView view{
        audio.data(),
        audio.size(),
        sampleRate,
        1,
    };
    autotiming::AnalysisOptions options;
    options.minimumTempoBpm = 60.0;
    options.maximumTempoBpm = 240.0;

    const autotiming::AnalysisResult result = autotiming::analyze(view, options);
    const bool hasFourAgainstThree = std::any_of(
        result.periodicityLayers.begin(),
        result.periodicityLayers.end(),
        [](const autotiming::PeriodicityLayer& layer) {
            const bool fourToThree =
                layer.ratioNumerator == 4 && layer.ratioDenominator == 3;
            const bool twoToThree =
                layer.ratioNumerator == 2 && layer.ratioDenominator == 3;
            const bool threeToFour =
                layer.ratioNumerator == 3 && layer.ratioDenominator == 4;
            return layer.relation == autotiming::PeriodicityRelationKind::RationalApproximation &&
                (fourToThree || twoToThree || threeToFour) &&
                layer.startSeconds < 28.0 && layer.endSeconds > 28.0 &&
                layer.supportingWindowIds.size() >= 2;
        });
    require(hasFourAgainstThree,
        "periodicity layer extraction lost the concurrent 4:3 relation");
}

void testSilenceAbstains()
{
    constexpr std::uint32_t sampleRate = 44100;
    const std::vector<float> audio(sampleRate * 8U, 0.0f);
    const autotiming::AudioView view{
        audio.data(),
        audio.size(),
        sampleRate,
        1,
    };

    const autotiming::AnalysisResult result = autotiming::analyze(view);
    require(result.diagnostics.anchorRegions.empty(),
        "silence produced a reliable anchor");
    require(result.tempoCandidates.empty(),
        "silence produced a global tempo candidate");
    require(result.confidence.tempo == 0.0,
        "silence produced non-zero tempo confidence");
    require(result.uncertainRegions.size() == 1,
        "silence was not represented as one uncertain region");
    require(result.uncertainRegions.front().startSeconds <= 1.0e-9 &&
        result.uncertainRegions.front().endSeconds >= 7.99,
        "silence uncertainty does not cover the input");
    require(result.uncertainRegions.front().reason ==
        autotiming::UncertaintyReason::NoTempoCandidate,
        "silence uncertainty has the wrong reason");
}

} // namespace

int main()
{
    try {
        testInputValidation();
        testMultiScaleEvidence();
        testReliableRegionSelection();
        testOnsetPeriodicityFollowsTempoStep();
        testTempoTrackerFollowsGradualRamp();
        testTempoTrackerKeepsOctaveBranchAcrossWideRamp();
        testTempoTrackerPreservesCompetingLayerHypotheses();
        testPeriodicityLayerKeepsRationalEvidence();
        testSilenceAbstains();
    } catch (const std::exception& error) {
        std::cerr << "analysis evidence test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "analysis evidence tests passed\n";
    return 0;
}
